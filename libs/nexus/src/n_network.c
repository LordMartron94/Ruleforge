#include "../nexus.h"

#if NEXUS_PLATFORM_LINUX

#  include <errno.h>
#  include <limits.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <fcntl.h>
#  include <sys/poll.h>
#  include <stdio.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>

struct NexusNetworkSocket {
  int                  fd;
  NexusNetworkFamily   family;
  NexusNetworkProtocol protocol;
  boolean              connecting;
};

struct NexusNetworkWaitSet {
  struct pollfd *descriptors;
  uint32         capacity;
};

/*
Summary: translates the owned endpoint representation at the native boundary.
*/
static socklen_t n_network_address_native_get(const NexusNetworkAddress *address, struct sockaddr_storage *native) {
  nexus_memory_bytes_clear(native, NEXUS_SIZEOF(*native));

  if (address->family == NNF_IPV4) {
    struct sockaddr_in *ipv4;

    ipv4             = (struct sockaddr_in *)(void *)native;
    ipv4->sin_family = AF_INET;
    ipv4->sin_port   = htons(address->port);
    nexus_memory_bytes_copy(address->bytes, &ipv4->sin_addr, 4U);

    return (socklen_t)sizeof(*ipv4);
  }

  {
    struct sockaddr_in6 *ipv6;

    ipv6                = (struct sockaddr_in6 *)(void *)native;
    ipv6->sin6_family   = AF_INET6;
    ipv6->sin6_port     = htons(address->port);
    ipv6->sin6_scope_id = address->scope_id;
    nexus_memory_bytes_copy(address->bytes, &ipv6->sin6_addr, 16U);

    return (socklen_t)sizeof(*ipv6);
  }
}

static void n_network_address_from_native(const struct sockaddr *native, NexusNetworkAddress *address) {
  nexus_memory_bytes_clear(address, NEXUS_SIZEOF(*address));

  if (native->sa_family == AF_INET) {
    const struct sockaddr_in *ipv4;

    ipv4            = (const struct sockaddr_in *)(const void *)native;
    address->family = NNF_IPV4;
    address->port   = ntohs(ipv4->sin_port);
    nexus_memory_bytes_copy(&ipv4->sin_addr, address->bytes, 4U);
  } else {
    const struct sockaddr_in6 *ipv6;

    NEXUS_PROGRAM_ASSERT_ALWAYS(native->sa_family == AF_INET6, "network: unexpected native address family");
    ipv6              = (const struct sockaddr_in6 *)(const void *)native;
    address->family   = NNF_IPV6;
    address->port     = ntohs(ipv6->sin6_port);
    address->scope_id = ipv6->sin6_scope_id;
    nexus_memory_bytes_copy(&ipv6->sin6_addr, address->bytes, 16U);
  }
}

NexusError *nexus_network_address_parse(const char *host, uint16 port, uint32 scope_id, NexusNetworkAddress *out_address) {
  NexusNetworkAddress address;

  NEXUS_PROGRAM_PRECONDITION(host != NULL && out_address != NULL, "network_address_parse: NULL pointer");
  nexus_memory_bytes_clear(&address, NEXUS_SIZEOF(address));
  address.port = port;

  if (inet_pton(AF_INET, host, address.bytes) == 1 && scope_id == 0U) {
    address.family = NNF_IPV4;
  } else if (inet_pton(AF_INET6, host, address.bytes) == 1) {
    address.family   = NNF_IPV6;
    address.scope_id = scope_id;
  } else {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_NETWORK_ERROR_INVALID_ADDRESS, "invalid numeric Internet address '%s'", host);
  }

  *out_address = address;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_address_format(const NexusNetworkAddress *address, char *out_host, uint_large capacity) {
  char       host[NEXUS_NETWORK_ADDRESS_TEXT_CAPACITY];
  uint_large length;

  NEXUS_PROGRAM_PRECONDITION(address != NULL && out_host != NULL, "network_address_format: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(address->family == NNF_IPV4 || address->family == NNF_IPV6, "network_address_format: invalid family");
  NEXUS_PROGRAM_PRECONDITION(capacity > 0U, "network_address_format: zero capacity");
  out_host[0] = '\0';

  if (inet_ntop(address->family == NNF_IPV4 ? AF_INET : AF_INET6, address->bytes, host, (socklen_t)sizeof(host)) == NULL) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_address_format: inet_ntop failed");
  }

  length = nexus_strings_string_length_get(host);

  if (capacity <= length) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_NETWORK_ERROR_BUFFER_TOO_SMALL, "network_address_format: insufficient capacity");
  }

  nexus_memory_bytes_copy(host, out_host, length + 1U);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_addresses_resolve(const char *host, uint16 port, NexusNetworkFamily family, NexusNetworkProtocol protocol, boolean passive,
                                            NexusNetworkAddress *out_addresses, uint32 capacity, uint32 *out_count) {
  struct addrinfo  hints;
  struct addrinfo *results;
  struct addrinfo *entry;
  char             service[6];
  int              result;
  uint32           count;
  uint32           index;
  NexusErrorCode   code;

  NEXUS_PROGRAM_PRECONDITION(family == NNF_ANY || family == NNF_IPV4 || family == NNF_IPV6, "network_addresses_resolve: invalid family");
  NEXUS_PROGRAM_PRECONDITION(protocol == NNP_TCP || protocol == NNP_UDP, "network_addresses_resolve: invalid protocol");
  NEXUS_PROGRAM_PRECONDITION(out_count != NULL && (out_addresses != NULL || capacity == 0U), "network_addresses_resolve: invalid outputs");
  *out_count = 0U;
  nexus_memory_bytes_clear(&hints, NEXUS_SIZEOF(hints));
  hints.ai_family = AF_UNSPEC;

  if (family == NNF_IPV4) {
    hints.ai_family = AF_INET;
  } else if (family == NNF_IPV6) {
    hints.ai_family = AF_INET6;
  }

  hints.ai_socktype = protocol == NNP_TCP ? SOCK_STREAM : SOCK_DGRAM;
  hints.ai_protocol = protocol == NNP_TCP ? IPPROTO_TCP : IPPROTO_UDP;
  hints.ai_flags    = AI_NUMERICSERV | (passive ? AI_PASSIVE : 0);
  (void)snprintf(service, sizeof(service), "%u", (unsigned int)port);
  results = NULL;
  result  = getaddrinfo(host, service, &hints, &results);

  if (result != 0) {
    if (result == EAI_SYSTEM) {
      return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_addresses_resolve: getaddrinfo failed");
    }

    code = GLOBAL_NEXUS_NETWORK_ERROR_RESOLUTION_FAILED;

    if (result == EAI_AGAIN) {
      code = GLOBAL_NEXUS_NETWORK_ERROR_RESOLUTION_TEMPORARY;
    } else if (result == EAI_NONAME) {
      code = GLOBAL_NEXUS_NETWORK_ERROR_NAME_NOT_FOUND;
    }

    return NEXUS_PROGRAM_ERROR_FAILURE(code, "getaddrinfo: %s (%d)", gai_strerror(result), result);
  }

  count = 0U;

  for (entry = results; entry != NULL; entry = entry->ai_next) {
    if (entry->ai_family == AF_INET || entry->ai_family == AF_INET6) {
      if (count == (uint32)0xffffffffU) {
        freeaddrinfo(results);

        return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_NETWORK_ERROR_RESOLUTION_FAILED, "network_addresses_resolve: too many results");
      }

      count++;
    }
  }

  *out_count = count;

  if (count > capacity) {
    freeaddrinfo(results);

    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_NETWORK_ERROR_BUFFER_TOO_SMALL, "network_addresses_resolve: need %u endpoints", count);
  }

  index = 0U;

  for (entry = results; entry != NULL; entry = entry->ai_next) {
    if (entry->ai_family == AF_INET || entry->ai_family == AF_INET6) {
      n_network_address_from_native(entry->ai_addr, &out_addresses[index]);
      index++;
    }
  }

  freeaddrinfo(results);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_create(NexusNetworkFamily family, NexusNetworkProtocol protocol, boolean nonblocking,
                                        NexusNetworkSocket **out_socket) {
  NexusNetworkSocket *created;
  NexusError         *error;
  int                 descriptor;
  int                 enabled;

  NEXUS_PROGRAM_PRECONDITION(family == NNF_IPV4 || family == NNF_IPV6, "network_socket_create: invalid family");
  NEXUS_PROGRAM_PRECONDITION(protocol == NNP_TCP || protocol == NNP_UDP, "network_socket_create: invalid protocol");
  NEXUS_PROGRAM_PRECONDITION(out_socket != NULL, "network_socket_create: NULL output");
  *out_socket = NULL;
  descriptor  = socket(family == NNF_IPV4 ? AF_INET : AF_INET6,
                      (protocol == NNP_TCP ? SOCK_STREAM : SOCK_DGRAM) | SOCK_CLOEXEC | (nonblocking ? SOCK_NONBLOCK : 0), 0);

  if (descriptor < 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_create: socket failed");
  }

  if (family == NNF_IPV6) {
    enabled = 1;

    if (setsockopt(descriptor, IPPROTO_IPV6, IPV6_V6ONLY, &enabled, (socklen_t)sizeof(enabled)) != 0) {
      error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_create: IPV6_V6ONLY failed");
      (void)close(descriptor);

      return error;
    }
  }

  created             = (NexusNetworkSocket *)nexus_memory_allocate(NEXUS_SIZEOF(*created));
  created->fd         = descriptor;
  created->family     = family;
  created->protocol   = protocol;
  created->connecting = FALSE;
  *out_socket         = created;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_close(NexusNetworkSocket **socket) {
  NexusError         *error;
  NexusNetworkSocket *owned;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && *socket != NULL, "network_socket_close: NULL handle");
  owned   = *socket;
  *socket = NULL;
  error   = NEXUS_PROGRAM_ERROR_SUCCESS();

  /* Linux releases the fd even on EINTR: retrying could close a reused fd. */
  if (close(owned->fd) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_close: close failed");
  }

  nexus_memory_deallocate(owned);

  return error;
}

NexusError *nexus_network_socket_nonblocking_set(NexusNetworkSocket *socket, boolean nonblocking) {
  int flags;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL, "network_socket_nonblocking_set: NULL socket");
  flags = fcntl(socket->fd, F_GETFL);

  if (flags < 0 || fcntl(socket->fd, F_SETFL, nonblocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_nonblocking_set: fcntl failed");
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_bind(NexusNetworkSocket *socket, const NexusNetworkAddress *address) {
  struct sockaddr_storage native;
  socklen_t               length;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && address != NULL, "network_socket_bind: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(socket->family == address->family, "network_socket_bind: family mismatch");
  length = n_network_address_native_get(address, &native);

  if (bind(socket->fd, (const struct sockaddr *)(const void *)&native, length) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_bind: bind failed");
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_listen(NexusNetworkSocket *socket, int32 backlog) {
  NEXUS_PROGRAM_PRECONDITION(socket != NULL, "network_socket_listen: NULL socket");
  NEXUS_PROGRAM_PRECONDITION(socket->protocol == NNP_TCP && backlog > 0, "network_socket_listen: TCP and positive backlog required");

  if (listen(socket->fd, (int)backlog) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_listen: listen failed");
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_accept(NexusNetworkSocket *listener, boolean nonblocking, NexusNetworkSocket **out_socket,
                                        NexusNetworkAddress *out_peer, NexusNetworkOutcome *out_outcome) {
  struct sockaddr_storage native;
  socklen_t               length;
  int                     descriptor;
  NexusNetworkSocket     *accepted;

  NEXUS_PROGRAM_PRECONDITION(listener != NULL && out_socket != NULL && out_outcome != NULL, "network_socket_accept: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(listener->protocol == NNP_TCP, "network_socket_accept: requires TCP");
  *out_socket  = NULL;
  *out_outcome = NNO_COMPLETE;
  nexus_memory_bytes_clear(&native, NEXUS_SIZEOF(native));
  length = (socklen_t)sizeof(native);

  do {
    descriptor = accept4(listener->fd, (struct sockaddr *)(void *)&native, &length, SOCK_CLOEXEC | (nonblocking ? SOCK_NONBLOCK : 0));
  } while (descriptor < 0 && errno == EINTR);

  if (descriptor < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *out_outcome = NNO_WOULD_BLOCK;

      return NEXUS_PROGRAM_ERROR_SUCCESS();
    }

    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_accept: accept4 failed");
  }

  accepted             = (NexusNetworkSocket *)nexus_memory_allocate(NEXUS_SIZEOF(*accepted));
  accepted->fd         = descriptor;
  accepted->family     = listener->family;
  accepted->protocol   = NNP_TCP;
  accepted->connecting = FALSE;

  if (out_peer != NULL) {
    n_network_address_from_native((const struct sockaddr *)(const void *)&native, out_peer);
  }

  *out_socket = accepted;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_connect(NexusNetworkSocket *socket, const NexusNetworkAddress *address, NexusNetworkOutcome *out_outcome) {
  struct sockaddr_storage native;
  socklen_t               length;
  int                     result;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && address != NULL && out_outcome != NULL, "network_socket_connect: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(socket->family == address->family, "network_socket_connect: family mismatch");
  NEXUS_PROGRAM_PRECONDITION(!socket->connecting, "network_socket_connect: connection already pending");
  *out_outcome = NNO_COMPLETE;
  length       = n_network_address_native_get(address, &native);
  result       = connect(socket->fd, (const struct sockaddr *)(const void *)&native, length);

  if (result == 0) {
    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }

  if (errno == EINPROGRESS || errno == EALREADY || errno == EINTR) {
    socket->connecting = TRUE;
    *out_outcome       = NNO_IN_PROGRESS;

    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }

  return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_connect: connect failed");
}

NexusError *nexus_network_socket_connect_finish(NexusNetworkSocket *socket, NexusNetworkOutcome *out_outcome) {
  struct pollfd descriptor;
  socklen_t     length;
  int           result;
  int           pending_error;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && out_outcome != NULL, "network_socket_connect_finish: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(socket->connecting, "network_socket_connect_finish: no pending connect");
  *out_outcome       = NNO_COMPLETE;
  descriptor.fd      = socket->fd;
  descriptor.events  = POLLIN | POLLOUT;
  descriptor.revents = 0;

  do {
    result = poll(&descriptor, 1U, 0);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_connect_finish: poll failed");
  }

  if (result == 0) {
    *out_outcome = NNO_IN_PROGRESS;

    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }

  length = (socklen_t)sizeof(pending_error);

  if (getsockopt(socket->fd, SOL_SOCKET, SO_ERROR, &pending_error, &length) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_connect_finish: SO_ERROR failed");
  }

  socket->connecting = FALSE;

  if (pending_error != 0) {
    return _nexus_program_errors_from_errno_create(pending_error, "network_socket_connect_finish: connection failed", __FILE__, __LINE__);
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_local_address_get(NexusNetworkSocket *socket, NexusNetworkAddress *out_address) {
  struct sockaddr_storage native;
  socklen_t               length;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && out_address != NULL, "network_socket_local_address_get: NULL pointer");
  nexus_memory_bytes_clear(&native, NEXUS_SIZEOF(native));
  length = (socklen_t)sizeof(native);

  if (getsockname(socket->fd, (struct sockaddr *)(void *)&native, &length) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_local_address_get: getsockname failed");
  }

  n_network_address_from_native((const struct sockaddr *)(const void *)&native, out_address);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_peer_address_get(NexusNetworkSocket *socket, NexusNetworkAddress *out_address) {
  struct sockaddr_storage native;
  socklen_t               length;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && out_address != NULL, "network_socket_peer_address_get: NULL pointer");
  nexus_memory_bytes_clear(&native, NEXUS_SIZEOF(native));
  length = (socklen_t)sizeof(native);

  if (getpeername(socket->fd, (struct sockaddr *)(void *)&native, &length) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_peer_address_get: getpeername failed");
  }

  n_network_address_from_native((const struct sockaddr *)(const void *)&native, out_address);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_send(NexusNetworkSocket *socket, const void *bytes, uint_large size, uint_large *out_size,
                                      NexusNetworkOutcome *out_outcome) {
  ssize_t result;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && out_size != NULL && out_outcome != NULL, "network_socket_send: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL || size == 0U, "network_socket_send: NULL bytes");
  NEXUS_PROGRAM_PRECONDITION(size <= (uint_large)SSIZE_MAX, "network_socket_send: size exceeds native I/O limit");
  *out_size    = 0U;
  *out_outcome = NNO_COMPLETE;

  do {
    result = send(socket->fd, bytes, (size_t)size, MSG_NOSIGNAL);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *out_outcome = NNO_WOULD_BLOCK;

      return NEXUS_PROGRAM_ERROR_SUCCESS();
    }

    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_send: send failed");
  }

  *out_size = (uint_large)result;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_receive(NexusNetworkSocket *socket, void *bytes, uint_large capacity, uint_large *out_size,
                                         NexusNetworkOutcome *out_outcome) {
  ssize_t result;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && bytes != NULL && out_size != NULL && out_outcome != NULL, "network_socket_receive: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(capacity > 0U && capacity <= (uint_large)SSIZE_MAX, "network_socket_receive: invalid capacity");
  *out_size    = 0U;
  *out_outcome = NNO_COMPLETE;

  do {
    result = recv(socket->fd, bytes, (size_t)capacity, socket->protocol == NNP_UDP ? MSG_TRUNC : 0);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *out_outcome = NNO_WOULD_BLOCK;

      return NEXUS_PROGRAM_ERROR_SUCCESS();
    }

    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_receive: recv failed");
  }

  *out_size = (uint_large)result;

  if (result == 0 && socket->protocol == NNP_TCP) {
    *out_outcome = NNO_END_OF_STREAM;
  } else if ((uint_large)result > capacity) {
    *out_outcome = NNO_TRUNCATED;
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_send_to(NexusNetworkSocket *socket, const NexusNetworkAddress *address, const void *bytes, uint_large size,
                                         uint_large *out_size, NexusNetworkOutcome *out_outcome) {
  struct sockaddr_storage native;
  socklen_t               length;
  ssize_t                 result;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && address != NULL && out_size != NULL && out_outcome != NULL, "network_socket_send_to: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(socket->protocol == NNP_UDP && socket->family == address->family, "network_socket_send_to: protocol/family mismatch");
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL || size == 0U, "network_socket_send_to: NULL bytes");
  NEXUS_PROGRAM_PRECONDITION(size <= (uint_large)SSIZE_MAX, "network_socket_send_to: size exceeds native I/O limit");
  *out_size    = 0U;
  *out_outcome = NNO_COMPLETE;
  length       = n_network_address_native_get(address, &native);

  do {
    result = sendto(socket->fd, bytes, (size_t)size, MSG_NOSIGNAL, (const struct sockaddr *)(const void *)&native, length);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *out_outcome = NNO_WOULD_BLOCK;

      return NEXUS_PROGRAM_ERROR_SUCCESS();
    }

    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_send_to: sendto failed");
  }

  *out_size = (uint_large)result;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_receive_from(NexusNetworkSocket *socket, void *bytes, uint_large capacity, NexusNetworkAddress *out_sender,
                                              uint_large *out_size, NexusNetworkOutcome *out_outcome) {
  struct sockaddr_storage native;
  socklen_t               length;
  ssize_t                 result;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL && bytes != NULL && out_size != NULL && out_outcome != NULL, "network_socket_receive_from: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(socket->protocol == NNP_UDP, "network_socket_receive_from: requires UDP");
  NEXUS_PROGRAM_PRECONDITION(capacity > 0U && capacity <= (uint_large)SSIZE_MAX, "network_socket_receive_from: invalid capacity");
  *out_size    = 0U;
  *out_outcome = NNO_COMPLETE;
  nexus_memory_bytes_clear(&native, NEXUS_SIZEOF(native));
  length = (socklen_t)sizeof(native);

  do {
    result = recvfrom(socket->fd, bytes, (size_t)capacity, MSG_TRUNC, (struct sockaddr *)(void *)&native, &length);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *out_outcome = NNO_WOULD_BLOCK;

      return NEXUS_PROGRAM_ERROR_SUCCESS();
    }

    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_receive_from: recvfrom failed");
  }

  *out_size    = (uint_large)result;
  *out_outcome = (uint_large)result > capacity ? NNO_TRUNCATED : NNO_COMPLETE;

  if (out_sender != NULL) {
    n_network_address_from_native((const struct sockaddr *)(const void *)&native, out_sender);
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_shutdown(NexusNetworkSocket *socket, NexusNetworkShutdown direction) {
  int native_direction;

  NEXUS_PROGRAM_PRECONDITION(socket != NULL, "network_socket_shutdown: NULL socket");
  NEXUS_PROGRAM_PRECONDITION(direction == NNS_RECEIVE || direction == NNS_SEND || direction == NNS_BOTH,
                             "network_socket_shutdown: invalid direction");
  native_direction = SHUT_RDWR;

  if (direction == NNS_RECEIVE) {
    native_direction = SHUT_RD;
  } else if (direction == NNS_SEND) {
    native_direction = SHUT_WR;
  }

  if (shutdown(socket->fd, native_direction) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_shutdown: shutdown failed");
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

/*
Summary: maps option names once for both get and set, validating applicability.
*/
static void n_network_socket_option_native_get(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int *out_level, int *out_option) {
  NEXUS_PROGRAM_PRECONDITION(socket != NULL, "network_socket_option: NULL socket");
  NEXUS_PROGRAM_PRECONDITION(option >= NNSO_REUSE_ADDRESS && option <= NNSO_SEND_BUFFER_SIZE, "network_socket_option: invalid option");
  NEXUS_PROGRAM_PRECONDITION(option != NNSO_TCP_NO_DELAY || socket->protocol == NNP_TCP, "network_socket_option: TCP option on UDP");
  NEXUS_PROGRAM_PRECONDITION(option != NNSO_IPV6_ONLY || socket->family == NNF_IPV6, "network_socket_option: IPv6 option on IPv4");
  *out_level = SOL_SOCKET;

  switch (option) {
  case NNSO_REUSE_ADDRESS:
    *out_option = SO_REUSEADDR;
    break;
  case NNSO_REUSE_PORT:
    *out_option = SO_REUSEPORT;
    break;
  case NNSO_KEEP_ALIVE:
    *out_option = SO_KEEPALIVE;
    break;
  case NNSO_TCP_NO_DELAY:
    *out_level  = IPPROTO_TCP;
    *out_option = TCP_NODELAY;
    break;
  case NNSO_IPV6_ONLY:
    *out_level  = IPPROTO_IPV6;
    *out_option = IPV6_V6ONLY;
    break;
  case NNSO_BROADCAST:
    *out_option = SO_BROADCAST;
    break;
  case NNSO_RECEIVE_BUFFER_SIZE:
    *out_option = SO_RCVBUF;
    break;
  case NNSO_SEND_BUFFER_SIZE:
    *out_option = SO_SNDBUF;
    break;
  }
}

NexusError *nexus_network_socket_option_set(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int32 value) {
  int level;
  int native_option;
  int native_value;

  n_network_socket_option_native_get(socket, option, &level, &native_option);
  NEXUS_PROGRAM_PRECONDITION((option == NNSO_RECEIVE_BUFFER_SIZE || option == NNSO_SEND_BUFFER_SIZE) ? value > 0 : (value == 0 || value == 1),
                             "network_socket_option_set: invalid value");
  native_value = (int)value;

  if (setsockopt(socket->fd, level, native_option, &native_value, (socklen_t)sizeof(native_value)) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_option_set: setsockopt failed");
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_option_get(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int32 *out_value) {
  int       level;
  int       native_option;
  int       native_value;
  socklen_t length;

  n_network_socket_option_native_get(socket, option, &level, &native_option);
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "network_socket_option_get: NULL output");
  length = (socklen_t)sizeof(native_value);

  if (getsockopt(socket->fd, level, native_option, &native_value, &length) != 0) {
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_socket_option_get: getsockopt failed");
  }

  *out_value = (int32)native_value;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusNetworkWaitSet *nexus_network_wait_set_create(uint32 capacity) {
  NexusNetworkWaitSet *set;

  NEXUS_PROGRAM_PRECONDITION(capacity > 0U && (uint_large)capacity <= (uint_large)((size_t)-1) / (uint_large)sizeof(struct pollfd),
                             "network_wait_set_create: invalid capacity");
  set              = (NexusNetworkWaitSet *)nexus_memory_allocate(NEXUS_SIZEOF(*set));
  set->capacity    = capacity;
  set->descriptors = (struct pollfd *)nexus_memory_allocate((uint_large)capacity * (uint_large)sizeof(struct pollfd));

  return set;
}

void nexus_network_wait_set_destroy(NexusNetworkWaitSet **set) {
  NEXUS_PROGRAM_PRECONDITION(set != NULL && *set != NULL, "network_wait_set_destroy: NULL handle");
  nexus_memory_deallocate((*set)->descriptors);
  nexus_memory_deallocate(*set);
  *set = NULL;
}

NexusError *nexus_network_wait(NexusNetworkWaitSet *set, NexusNetworkWaitEntry *entries, uint32 count, int32 timeout_ms, uint32 *out_ready_count) {
  struct timespec now;
  int64           deadline;
  int64           remaining;
  uint32          index;
  int             result;
  int             timeout;

  NEXUS_PROGRAM_PRECONDITION(set != NULL && out_ready_count != NULL, "network_wait: NULL pointer");
  NEXUS_PROGRAM_PRECONDITION(entries != NULL || count == 0U, "network_wait: NULL entries");
  NEXUS_PROGRAM_PRECONDITION(count <= set->capacity && timeout_ms >= -1, "network_wait: invalid count/timeout");
  *out_ready_count = 0U;

  for (index = 0U; index < count; index++) {
    NEXUS_PROGRAM_PRECONDITION(entries[index].socket != NULL, "network_wait: NULL socket entry");
    NEXUS_PROGRAM_PRECONDITION((entries[index].events & ~(uint32)(NNE_READABLE | NNE_WRITABLE)) == 0U, "network_wait: invalid input events");
    entries[index].ready_events = 0U;
    set->descriptors[index].fd  = entries[index].socket->fd;
    set->descriptors[index].events =
        (short)(((entries[index].events & NNE_READABLE) != 0U ? POLLIN : 0) | ((entries[index].events & NNE_WRITABLE) != 0U ? POLLOUT : 0));
    set->descriptors[index].revents = 0;
  }

  deadline = 0;
  timeout  = (int)timeout_ms;

  if (timeout_ms > 0) {
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_wait: clock_gettime failed");
    }

    deadline = ((int64)now.tv_sec * 1000000000LL) + (int64)now.tv_nsec + ((int64)timeout_ms * 1000000LL);
  }

  for (;;) {
    result = poll(set->descriptors, (nfds_t)count, timeout);

    if (result >= 0) {
      break;
    }

    if (errno != EINTR) {
      return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_wait: poll failed");
    }

    if (timeout_ms == 0) {
      return NEXUS_PROGRAM_ERROR_SUCCESS();
    }

    if (timeout_ms > 0) {
      if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return NEXUS_PROGRAM_ERROR_FROM_ERRNO("network_wait: clock_gettime failed");
      }

      remaining = deadline - ((int64)now.tv_sec * 1000000000LL + (int64)now.tv_nsec);

      if (remaining <= 0) {
        return NEXUS_PROGRAM_ERROR_SUCCESS();
      }

      timeout = (int)((remaining + 999999LL) / 1000000LL);
    }
  }

  for (index = 0U; index < count; index++) {
    short ready;

    ready                       = set->descriptors[index].revents;
    entries[index].ready_events = ((ready & POLLIN) != 0 ? (uint32)NNE_READABLE : 0U) | ((ready & POLLOUT) != 0 ? (uint32)NNE_WRITABLE : 0U) |
                                  ((ready & POLLERR) != 0 ? (uint32)NNE_ERROR : 0U) | ((ready & POLLHUP) != 0 ? (uint32)NNE_HANGUP : 0U) |
                                  ((ready & POLLNVAL) != 0 ? (uint32)NNE_INVALID : 0U);

    if (entries[index].ready_events != 0U) {
      (*out_ready_count)++;
    }
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

#else

NexusError *nexus_network_address_parse(const char *host, uint16 port, uint32 scope_id, NexusNetworkAddress *out_address) {
  (void)host;
  (void)port;
  (void)scope_id;
  (void)out_address;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_address_parse");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_address_format(const NexusNetworkAddress *address, char *out_host, uint_large capacity) {
  (void)address;
  (void)out_host;
  (void)capacity;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_address_format");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_addresses_resolve(const char *host, uint16 port, NexusNetworkFamily family, NexusNetworkProtocol protocol, boolean passive,
                                            NexusNetworkAddress *out_addresses, uint32 capacity, uint32 *out_count) {
  (void)host;
  (void)port;
  (void)family;
  (void)protocol;
  (void)passive;
  (void)out_addresses;
  (void)capacity;
  (void)out_count;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_addresses_resolve");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_create(NexusNetworkFamily family, NexusNetworkProtocol protocol, boolean nonblocking,
                                        NexusNetworkSocket **out_socket) {
  (void)family;
  (void)protocol;
  (void)nonblocking;
  (void)out_socket;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_create");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_close(NexusNetworkSocket **socket) {
  (void)socket;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_close");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_nonblocking_set(NexusNetworkSocket *socket, boolean nonblocking) {
  (void)socket;
  (void)nonblocking;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_nonblocking_set");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_bind(NexusNetworkSocket *socket, const NexusNetworkAddress *address) {
  (void)socket;
  (void)address;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_bind");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_listen(NexusNetworkSocket *socket, int32 backlog) {
  (void)socket;
  (void)backlog;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_listen");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_accept(NexusNetworkSocket *listener, boolean nonblocking, NexusNetworkSocket **out_socket,
                                        NexusNetworkAddress *out_peer, NexusNetworkOutcome *out_outcome) {
  (void)listener;
  (void)nonblocking;
  (void)out_socket;
  (void)out_peer;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_accept");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_connect(NexusNetworkSocket *socket, const NexusNetworkAddress *address, NexusNetworkOutcome *out_outcome) {
  (void)socket;
  (void)address;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_connect");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_connect_finish(NexusNetworkSocket *socket, NexusNetworkOutcome *out_outcome) {
  (void)socket;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_connect_finish");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_local_address_get(NexusNetworkSocket *socket, NexusNetworkAddress *out_address) {
  (void)socket;
  (void)out_address;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_local_address_get");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_peer_address_get(NexusNetworkSocket *socket, NexusNetworkAddress *out_address) {
  (void)socket;
  (void)out_address;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_peer_address_get");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_send(NexusNetworkSocket *socket, const void *bytes, uint_large size, uint_large *out_size,
                                      NexusNetworkOutcome *out_outcome) {
  (void)socket;
  (void)bytes;
  (void)size;
  (void)out_size;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_send");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_receive(NexusNetworkSocket *socket, void *bytes, uint_large capacity, uint_large *out_size,
                                         NexusNetworkOutcome *out_outcome) {
  (void)socket;
  (void)bytes;
  (void)capacity;
  (void)out_size;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_receive");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_send_to(NexusNetworkSocket *socket, const NexusNetworkAddress *address, const void *bytes, uint_large size,
                                         uint_large *out_size, NexusNetworkOutcome *out_outcome) {
  (void)socket;
  (void)address;
  (void)bytes;
  (void)size;
  (void)out_size;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_send_to");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_receive_from(NexusNetworkSocket *socket, void *bytes, uint_large capacity, NexusNetworkAddress *out_sender,
                                              uint_large *out_size, NexusNetworkOutcome *out_outcome) {
  (void)socket;
  (void)bytes;
  (void)capacity;
  (void)out_sender;
  (void)out_size;
  (void)out_outcome;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_receive_from");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_shutdown(NexusNetworkSocket *socket, NexusNetworkShutdown direction) {
  (void)socket;
  (void)direction;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_shutdown");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_option_set(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int32 value) {
  (void)socket;
  (void)option;
  (void)value;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_option_set");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_network_socket_option_get(NexusNetworkSocket *socket, NexusNetworkSocketOption option, int32 *out_value) {
  (void)socket;
  (void)option;
  (void)out_value;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_socket_option_get");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusNetworkWaitSet *nexus_network_wait_set_create(uint32 capacity) {
  (void)capacity;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_wait_set_create");

  return NULL;
}

void nexus_network_wait_set_destroy(NexusNetworkWaitSet **set) {
  (void)set;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_wait_set_destroy");
}

NexusError *nexus_network_wait(NexusNetworkWaitSet *set, NexusNetworkWaitEntry *entries, uint32 count, int32 timeout_ms, uint32 *out_ready_count) {
  (void)set;
  (void)entries;
  (void)count;
  (void)timeout_ms;
  (void)out_ready_count;
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("nexus_network_wait");

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

#endif
