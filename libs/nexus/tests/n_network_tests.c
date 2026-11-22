#include <string.h>
#include <nexus.h>

#include <errno.h>
#include <stdio.h>

#if NEXUS_PLATFORM_LINUX
#  include <signal.h>
#  include <sys/time.h>
#endif

#define N_NETWORK_TEST_CHECK(condition)                                                                                                              \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      (void)fprintf(stderr, "network test failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                   \
      return 1;                                                                                                                                      \
    }                                                                                                                                                \
  } while (0)

#define N_NETWORK_TEST_OK(call)                                                                                                                      \
  do {                                                                                                                                               \
    NexusError *test_error;                                                                                                                          \
    test_error = (call);                                                                                                                             \
    if (test_error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {                                                                                            \
      char       diagnostic[2048];                                                                                                                   \
      uint_large diagnostic_size;                                                                                                                    \
      diagnostic_size = nexus_program_errors_error_print(test_error, diagnostic, NEXUS_SIZEOF(diagnostic));                                          \
      (void)diagnostic_size;                                                                                                                         \
      (void)fprintf(stderr, "network call failed at line %d: %s\n", __LINE__, diagnostic);                                                           \
      return 1;                                                                                                                                      \
    }                                                                                                                                                \
  } while (0)

#if NEXUS_PLATFORM_LINUX

static int network_test_addresses(void) {
  NexusNetworkAddress address;
  NexusNetworkAddress saved;
  NexusNetworkAddress resolved[8];
  NexusError         *error;
  char                host[NEXUS_NETWORK_ADDRESS_TEXT_CAPACITY];
  uint32              count;
  uint32              index;

  N_NETWORK_TEST_OK(nexus_network_address_parse("127.0.0.1", 43210U, 0U, &address));
  N_NETWORK_TEST_CHECK(address.family == NNF_IPV4 && address.port == 43210U && address.scope_id == 0U);
  N_NETWORK_TEST_CHECK(address.bytes[0] == 127U && address.bytes[3] == 1U && address.bytes[15] == 0U);
  N_NETWORK_TEST_OK(nexus_network_address_format(&address, host, NEXUS_SIZEOF(host)));
  N_NETWORK_TEST_CHECK(strcmp(host, "127.0.0.1") == 0);
  saved = address;
  error = nexus_network_address_parse("127.0.0.1", 0U, 4U, &address);
  N_NETWORK_TEST_CHECK(error->code == GLOBAL_NEXUS_NETWORK_ERROR_INVALID_ADDRESS && address.family == saved.family && address.port == saved.port &&
                       address.scope_id == saved.scope_id && memcmp(address.bytes, saved.bytes, sizeof(saved.bytes)) == 0);
  error = nexus_network_address_parse("not an IP", 0U, 0U, &address);
  N_NETWORK_TEST_CHECK(error->code == GLOBAL_NEXUS_NETWORK_ERROR_INVALID_ADDRESS && address.family == saved.family && address.port == saved.port &&
                       address.scope_id == saved.scope_id && memcmp(address.bytes, saved.bytes, sizeof(saved.bytes)) == 0);
  error = nexus_network_address_format(&address, host, 2U);
  N_NETWORK_TEST_CHECK(error->code == GLOBAL_NEXUS_NETWORK_ERROR_BUFFER_TOO_SMALL && host[0] == '\0');

  N_NETWORK_TEST_OK(nexus_network_address_parse("fe80::1234", 65535U, 7U, &address));
  N_NETWORK_TEST_CHECK(address.family == NNF_IPV6 && address.port == 65535U && address.scope_id == 7U);
  N_NETWORK_TEST_OK(nexus_network_address_format(&address, host, NEXUS_SIZEOF(host)));
  N_NETWORK_TEST_CHECK(strcmp(host, "fe80::1234") == 0);
  N_NETWORK_TEST_OK(nexus_network_address_parse("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255", 0U, 0U, &address));
  N_NETWORK_TEST_OK(nexus_network_address_format(&address, host, NEXUS_SIZEOF(host)));

  error = nexus_network_addresses_resolve("127.0.0.1", 12345U, NNF_IPV4, NNP_TCP, FALSE, NULL, 0U, &count);
  N_NETWORK_TEST_CHECK(error->code == GLOBAL_NEXUS_NETWORK_ERROR_BUFFER_TOO_SMALL && count == 1U);
  N_NETWORK_TEST_OK(nexus_network_addresses_resolve("127.0.0.1", 12345U, NNF_IPV4, NNP_TCP, FALSE, resolved, 8U, &count));
  N_NETWORK_TEST_CHECK(count == 1U && resolved[0].port == 12345U && resolved[0].bytes[0] == 127U);
  N_NETWORK_TEST_OK(nexus_network_addresses_resolve("::1", 80U, NNF_IPV6, NNP_UDP, FALSE, resolved, 8U, &count));
  N_NETWORK_TEST_CHECK(count == 1U && resolved[0].family == NNF_IPV6 && resolved[0].bytes[15] == 1U);
  resolved[0] = saved;
  error       = nexus_network_addresses_resolve(NULL, 0U, NNF_ANY, NNP_TCP, TRUE, resolved, 1U, &count);
  N_NETWORK_TEST_CHECK(error->code == GLOBAL_NEXUS_NETWORK_ERROR_BUFFER_TOO_SMALL && count == 2U);
  N_NETWORK_TEST_CHECK(resolved[0].family == saved.family && resolved[0].port == saved.port && resolved[0].scope_id == saved.scope_id &&
                       memcmp(resolved[0].bytes, saved.bytes, sizeof(saved.bytes)) == 0);
  N_NETWORK_TEST_OK(nexus_network_addresses_resolve(NULL, 0U, NNF_ANY, NNP_TCP, TRUE, resolved, 8U, &count));
  N_NETWORK_TEST_CHECK(count == 2U);

  for (index = 0U; index < count; index++) {
    N_NETWORK_TEST_CHECK(resolved[index].port == 0U && resolved[index].bytes[0] == 0U && resolved[index].bytes[15] == 0U);
  }

  N_NETWORK_TEST_OK(nexus_network_addresses_resolve("localhost", 81U, NNF_IPV4, NNP_TCP, FALSE, resolved, 8U, &count));
  N_NETWORK_TEST_CHECK(count > 0U && resolved[0].port == 81U);
  error = nexus_network_addresses_resolve("", 80U, NNF_ANY, NNP_TCP, FALSE, resolved, 8U, &count);
  N_NETWORK_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS && count == 0U);

  return 0;
}

static int network_test_tcp(NexusNetworkFamily family, boolean nonblocking) {
  NexusNetworkSocket   *listener;
  NexusNetworkSocket   *client;
  NexusNetworkSocket   *server;
  NexusNetworkWaitSet  *set;
  NexusNetworkWaitEntry entries[2];
  NexusNetworkAddress   local;
  NexusNetworkAddress   peer;
  NexusNetworkAddress   client_local;
  NexusNetworkOutcome   outcome;
  NexusError           *error;
  uint32                ready_count;
  uint_large            size;
  uint_large            total;
  uint_large            received;
  int32                 option;
  char                  buffer[65536];
  uint32                iteration;

  N_NETWORK_TEST_OK(nexus_network_address_parse(family == NNF_IPV4 ? "127.0.0.1" : "::1", 0U, 0U, &local));
  N_NETWORK_TEST_OK(nexus_network_socket_create(family, NNP_TCP, TRUE, &listener));
  N_NETWORK_TEST_OK(nexus_network_socket_option_set(listener, NNSO_REUSE_ADDRESS, 1));
  N_NETWORK_TEST_OK(nexus_network_socket_option_get(listener, NNSO_REUSE_ADDRESS, &option));
  N_NETWORK_TEST_CHECK(option == 1);

  if (family == NNF_IPV6) {
    N_NETWORK_TEST_OK(nexus_network_socket_option_get(listener, NNSO_IPV6_ONLY, &option));
    N_NETWORK_TEST_CHECK(option == 1);
  }

  N_NETWORK_TEST_OK(nexus_network_socket_bind(listener, &local));
  N_NETWORK_TEST_OK(nexus_network_socket_local_address_get(listener, &local));
  N_NETWORK_TEST_CHECK(local.port != 0U && local.family == family);
  N_NETWORK_TEST_OK(nexus_network_socket_listen(listener, 8));
  N_NETWORK_TEST_OK(nexus_network_socket_accept(listener, TRUE, &server, &peer, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_WOULD_BLOCK && server == NULL);
  N_NETWORK_TEST_OK(nexus_network_socket_create(family, NNP_TCP, nonblocking, &client));
  N_NETWORK_TEST_OK(nexus_network_socket_option_set(client, NNSO_TCP_NO_DELAY, 1));
  N_NETWORK_TEST_OK(nexus_network_socket_option_set(client, NNSO_KEEP_ALIVE, 1));
  N_NETWORK_TEST_OK(nexus_network_socket_option_set(client, NNSO_SEND_BUFFER_SIZE, 4096));
  N_NETWORK_TEST_OK(nexus_network_socket_option_get(client, NNSO_SEND_BUFFER_SIZE, &option));
  N_NETWORK_TEST_CHECK(option >= 4096);
  N_NETWORK_TEST_OK(nexus_network_socket_connect(client, &local, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE || outcome == NNO_IN_PROGRESS);
  set               = nexus_network_wait_set_create(2U);
  entries[0].socket = client;
  entries[0].events = NNE_WRITABLE;
  entries[1].socket = listener;
  entries[1].events = NNE_READABLE;

  if (outcome == NNO_IN_PROGRESS) {
    N_NETWORK_TEST_OK(nexus_network_wait(set, entries, 1U, 1000, &ready_count));
    N_NETWORK_TEST_CHECK(ready_count == 1U);
    N_NETWORK_TEST_OK(nexus_network_socket_connect_finish(client, &outcome));
    N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE);
  }

  N_NETWORK_TEST_OK(nexus_network_wait(set, &entries[1], 1U, 1000, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 1U && (entries[1].ready_events & NNE_READABLE) != 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_accept(listener, TRUE, &server, &peer, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE && server != NULL && peer.family == family);
  N_NETWORK_TEST_OK(nexus_network_socket_local_address_get(client, &client_local));
  N_NETWORK_TEST_CHECK(peer.port == client_local.port);
  N_NETWORK_TEST_OK(nexus_network_socket_peer_address_get(client, &peer));
  N_NETWORK_TEST_CHECK(peer.port == local.port && peer.family == family);
  N_NETWORK_TEST_OK(nexus_network_socket_receive(server, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_WOULD_BLOCK && size == 0U);
  entries[1].socket = server;
  N_NETWORK_TEST_OK(nexus_network_wait(set, &entries[1], 1U, 5, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 0U && entries[1].ready_events == 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_send(client, "hello", 5U, &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE && size == 5U);
  N_NETWORK_TEST_OK(nexus_network_wait(set, entries, 2U, 1000, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 2U);
  N_NETWORK_TEST_OK(nexus_network_socket_receive(server, buffer, 2U, &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 2U && outcome == NNO_COMPLETE && memcmp(buffer, "he", 2U) == 0);
  N_NETWORK_TEST_OK(nexus_network_socket_receive(server, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 3U && outcome == NNO_COMPLETE && memcmp(buffer, "llo", 3U) == 0);

  /* A bounded fill proves short writes and would-block under backpressure. */
  N_NETWORK_TEST_OK(nexus_network_socket_nonblocking_set(client, TRUE));
  nexus_memory_bytes_set(buffer, NEXUS_SIZEOF(buffer), 0x5aU);
  total = 0U;

  for (iteration = 0U; iteration < 1000U; iteration++) {
    N_NETWORK_TEST_OK(nexus_network_socket_send(client, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));

    if (outcome == NNO_WOULD_BLOCK) {
      N_NETWORK_TEST_CHECK(size == 0U);
      break;
    }

    N_NETWORK_TEST_CHECK(size > 0U && size <= NEXUS_SIZEOF(buffer));
    total += size;
  }

  N_NETWORK_TEST_CHECK(outcome == NNO_WOULD_BLOCK && total > 0U);
  received = 0U;

  while (received < total) {
    N_NETWORK_TEST_OK(nexus_network_wait(set, &entries[1], 1U, 1000, &ready_count));
    N_NETWORK_TEST_CHECK(ready_count == 1U);
    N_NETWORK_TEST_OK(nexus_network_socket_receive(server, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
    N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE && size > 0U);
    received += size;
  }

  N_NETWORK_TEST_CHECK(received == total);
  N_NETWORK_TEST_OK(nexus_network_socket_shutdown(client, NNS_SEND));
  N_NETWORK_TEST_OK(nexus_network_wait(set, &entries[1], 1U, 1000, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 1U);
  N_NETWORK_TEST_OK(nexus_network_socket_receive(server, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_END_OF_STREAM && size == 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_send(server, "reply", 5U, &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE && size == 5U);
  entries[0].events = NNE_READABLE;
  N_NETWORK_TEST_OK(nexus_network_wait(set, entries, 1U, 1000, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 1U);
  N_NETWORK_TEST_OK(nexus_network_socket_receive(client, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE && size == 5U && memcmp(buffer, "reply", 5U) == 0);

  /* SIGPIPE retains its default disposition; a local write shutdown must
     return EPIPE rather than terminating this test process. */
  error = nexus_network_socket_send(client, "x", 1U, &size, &outcome);
  N_NETWORK_TEST_CHECK(error->code ==
                       NEXUS_PROGRAM_ERROR_CODE_MAKE(NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_OS_FACILITY, NEXUS_ERROR_LOCAL(NPOEC_ERRNO, EPIPE)));
  N_NETWORK_TEST_CHECK(size == 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_close(&server));
  N_NETWORK_TEST_OK(nexus_network_socket_close(&client));
  N_NETWORK_TEST_OK(nexus_network_socket_close(&listener));
  N_NETWORK_TEST_CHECK(server == NULL && client == NULL && listener == NULL);
  nexus_network_wait_set_destroy(&set);
  N_NETWORK_TEST_CHECK(set == NULL);

  return 0;
}

static int network_test_udp(NexusNetworkFamily family) {
  NexusNetworkSocket   *sender;
  NexusNetworkSocket   *receiver;
  NexusNetworkWaitSet  *set;
  NexusNetworkWaitEntry entry;
  NexusNetworkAddress   destination;
  NexusNetworkAddress   origin;
  NexusNetworkAddress   local;
  NexusNetworkOutcome   outcome;
  uint_large            size;
  uint32                ready_count;
  char                  buffer[8];
  int32                 option;

  N_NETWORK_TEST_OK(nexus_network_address_parse(family == NNF_IPV4 ? "127.0.0.1" : "::1", 0U, 0U, &destination));
  N_NETWORK_TEST_OK(nexus_network_socket_create(family, NNP_UDP, TRUE, &receiver));
  N_NETWORK_TEST_OK(nexus_network_socket_create(family, NNP_UDP, FALSE, &sender));
  N_NETWORK_TEST_OK(nexus_network_socket_bind(receiver, &destination));
  N_NETWORK_TEST_OK(nexus_network_socket_local_address_get(receiver, &destination));
  N_NETWORK_TEST_OK(nexus_network_socket_option_set(sender, NNSO_BROADCAST, 1));
  N_NETWORK_TEST_OK(nexus_network_socket_option_get(sender, NNSO_BROADCAST, &option));
  N_NETWORK_TEST_CHECK(option == 1);
  set          = nexus_network_wait_set_create(1U);
  entry.socket = receiver;
  entry.events = NNE_READABLE;
  N_NETWORK_TEST_OK(nexus_network_socket_receive_from(receiver, buffer, NEXUS_SIZEOF(buffer), &origin, &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 0U && outcome == NNO_WOULD_BLOCK);
  N_NETWORK_TEST_OK(nexus_network_socket_send_to(sender, &destination, "a datagram!", 11U, &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 11U && outcome == NNO_COMPLETE);
  N_NETWORK_TEST_OK(nexus_network_wait(set, &entry, 1U, 1000, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 1U);
  N_NETWORK_TEST_OK(nexus_network_socket_receive_from(receiver, buffer, NEXUS_SIZEOF(buffer), &origin, &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 11U && outcome == NNO_TRUNCATED && memcmp(buffer, "a datagr", 8U) == 0);
  N_NETWORK_TEST_OK(nexus_network_socket_local_address_get(sender, &local));
  N_NETWORK_TEST_CHECK(origin.family == family && origin.port == local.port);
  N_NETWORK_TEST_OK(nexus_network_socket_send_to(sender, &destination, NULL, 0U, &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 0U && outcome == NNO_COMPLETE);
  N_NETWORK_TEST_OK(nexus_network_wait(set, &entry, 1U, 1000, &ready_count));
  N_NETWORK_TEST_OK(nexus_network_socket_receive_from(receiver, buffer, NEXUS_SIZEOF(buffer), NULL, &size, &outcome));
  N_NETWORK_TEST_CHECK(size == 0U && outcome == NNO_COMPLETE);
  N_NETWORK_TEST_OK(nexus_network_socket_connect(sender, &destination, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE);
  N_NETWORK_TEST_OK(nexus_network_socket_send(sender, "connected UDP", 13U, &size, &outcome));
  N_NETWORK_TEST_OK(nexus_network_wait(set, &entry, 1U, 1000, &ready_count));
  N_NETWORK_TEST_OK(nexus_network_socket_receive(receiver, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_TRUNCATED && size == 13U && memcmp(buffer, "connecte", 8U) == 0);
  N_NETWORK_TEST_OK(nexus_network_socket_send(sender, NULL, 0U, &size, &outcome));
  N_NETWORK_TEST_OK(nexus_network_wait(set, &entry, 1U, 1000, &ready_count));
  N_NETWORK_TEST_OK(nexus_network_socket_receive(receiver, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_COMPLETE && size == 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_receive(receiver, buffer, NEXUS_SIZEOF(buffer), &size, &outcome));
  N_NETWORK_TEST_CHECK(outcome == NNO_WOULD_BLOCK && size == 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_close(&sender));
  N_NETWORK_TEST_OK(nexus_network_socket_close(&receiver));
  nexus_network_wait_set_destroy(&set);

  return 0;
}

static int network_test_refused(void) {
  NexusNetworkSocket   *bound;
  NexusNetworkSocket   *client;
  NexusNetworkAddress   address;
  NexusNetworkOutcome   outcome;
  NexusNetworkWaitSet  *set;
  NexusNetworkWaitEntry entry;
  NexusError           *error;
  uint32                ready_count;
  uint_large            size;
  char                  buffer[1];

  N_NETWORK_TEST_OK(nexus_network_address_parse("127.0.0.1", 0U, 0U, &address));
  N_NETWORK_TEST_OK(nexus_network_socket_create(NNF_IPV4, NNP_TCP, TRUE, &bound));
  N_NETWORK_TEST_OK(nexus_network_socket_bind(bound, &address));
  N_NETWORK_TEST_OK(nexus_network_socket_local_address_get(bound, &address));
  N_NETWORK_TEST_OK(nexus_network_socket_create(NNF_IPV4, NNP_TCP, TRUE, &client));
  error = nexus_network_socket_connect(client, &address, &outcome);

  if (error->code == GLOBAL_NEXUS_ERROR_SUCCESS) {
    N_NETWORK_TEST_CHECK(outcome == NNO_IN_PROGRESS);
    set          = nexus_network_wait_set_create(1U);
    entry.socket = client;
    entry.events = NNE_WRITABLE;
    N_NETWORK_TEST_OK(nexus_network_wait(set, &entry, 1U, 1000, &ready_count));
    N_NETWORK_TEST_CHECK(ready_count == 1U);
    error = nexus_network_socket_connect_finish(client, &outcome);
    nexus_network_wait_set_destroy(&set);
  }

  N_NETWORK_TEST_CHECK(error->code == NEXUS_PROGRAM_ERROR_CODE_MAKE(NEXUS_ERROR_LIBRARY_IDENTIFIER, NEXUS_ERROR_OS_FACILITY,
                                                                    NEXUS_ERROR_LOCAL(NPOEC_ERRNO, ECONNREFUSED)));
  N_NETWORK_TEST_OK(nexus_network_socket_close(&client));
  /* A failed peer query leaves its caller-owned output unchanged. */
  address.port = 123U;
  error        = nexus_network_socket_peer_address_get(bound, &address);
  N_NETWORK_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS && address.port == 123U);
  error = nexus_network_socket_receive(bound, buffer, 1U, &size, &outcome);
  N_NETWORK_TEST_CHECK(error->code != GLOBAL_NEXUS_ERROR_SUCCESS && size == 0U);
  N_NETWORK_TEST_OK(nexus_network_socket_close(&bound));

  return 0;
}

static volatile sig_atomic_t network_test_signal_count;

static void network_test_alarm(int signal_number) {
  (void)signal_number;
  network_test_signal_count++;
}

static int network_test_interrupted_wait(void) {
  struct sigaction     action;
  struct sigaction     previous;
  struct itimerval     timer;
  struct itimerval     disabled;
  struct itimerval     previous_timer;
  NexusNetworkWaitSet *set;
  NexusTime            start;
  NexusDuration        elapsed;
  uint32               ready_count;

  nexus_memory_bytes_clear(&action, NEXUS_SIZEOF(action));
  action.sa_handler = network_test_alarm;
  (void)sigemptyset(&action.sa_mask);
  N_NETWORK_TEST_CHECK(sigaction(SIGALRM, &action, &previous) == 0);
  nexus_memory_bytes_clear(&timer, NEXUS_SIZEOF(timer));
  nexus_memory_bytes_clear(&disabled, NEXUS_SIZEOF(disabled));
  timer.it_value.tv_usec    = 1000;
  timer.it_interval.tv_usec = 1000;
  network_test_signal_count = 0;
  set                       = nexus_network_wait_set_create(1U);
  N_NETWORK_TEST_CHECK(setitimer(ITIMER_REAL, &timer, &previous_timer) == 0);
  start = nexus_time_get_monotonic();
  N_NETWORK_TEST_OK(nexus_network_wait(set, NULL, 0U, 20, &ready_count));
  elapsed = nexus_time_duration_between(start, nexus_time_get_monotonic());
  N_NETWORK_TEST_CHECK(setitimer(ITIMER_REAL, &disabled, NULL) == 0);
  N_NETWORK_TEST_CHECK(sigaction(SIGALRM, &previous, NULL) == 0);
  N_NETWORK_TEST_CHECK(setitimer(ITIMER_REAL, &previous_timer, NULL) == 0);
  N_NETWORK_TEST_CHECK(ready_count == 0U && network_test_signal_count > 0);
  N_NETWORK_TEST_CHECK(elapsed.nanoseconds >= 19000000LL && elapsed.nanoseconds < 1000000000LL);
  N_NETWORK_TEST_OK(nexus_network_wait(set, NULL, 0U, 0, &ready_count));
  N_NETWORK_TEST_CHECK(ready_count == 0U);
  nexus_network_wait_set_destroy(&set);

  return 0;
}

#endif

int main(void) {
#if NEXUS_PLATFORM_LINUX
  N_NETWORK_TEST_CHECK(network_test_addresses() == 0);
  N_NETWORK_TEST_CHECK(network_test_tcp(NNF_IPV4, TRUE) == 0);
  N_NETWORK_TEST_CHECK(network_test_tcp(NNF_IPV4, FALSE) == 0);
  N_NETWORK_TEST_CHECK(network_test_tcp(NNF_IPV6, TRUE) == 0);
  N_NETWORK_TEST_CHECK(network_test_udp(NNF_IPV4) == 0);
  N_NETWORK_TEST_CHECK(network_test_udp(NNF_IPV6) == 0);
  N_NETWORK_TEST_CHECK(network_test_refused() == 0);
  N_NETWORK_TEST_CHECK(network_test_interrupted_wait() == 0);
  (void)puts("Nexus networking tests passed (IPv4/IPv6 TCP/UDP, backpressure, half-close, EINTR).");
#else
  (void)puts("Nexus networking runtime tests require Linux.");
  return 77;
#endif

  return 0;
}
