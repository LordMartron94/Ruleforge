#include "../nexus.h"

#if NEXUS_PLATFORM_LINUX

#  include <errno.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <stddef.h>
#  include <sys/poll.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>

/* Declared by <unistd.h> only under _GNU_SOURCE on glibc; nexus.h deliberately only defines the
   narrower _DEFAULT_SOURCE (see its GLOBAL DEFINES section), so this one symbol is declared by
   hand rather than widening every translation unit's feature-macro exposure for it alone. */
extern char **environ; /* NOLINT(readability-redundant-declaration) */

struct NexusProcess {
  pid_t                  pid;
  int                    pidfd;
  int                    stdin_fd;  /* parent-side write end, or -1 if not piped */
  int                    stdout_fd; /* parent-side read end, or -1 if not piped */
  int                    stderr_fd; /* parent-side read end, or -1 if not piped */
  boolean                has_exited;
  NexusProcessExitStatus exit_status;
};

/*
Summary: pidfd_open/pidfd_send_signal have no glibc wrapper as of this codebase's target glibc --
  issued as raw syscalls, matching the syscall-numbers-only availability confirmed for this
  platform (see adr/0004).
*/
static int n_process_pidfd_open(pid_t pid) {
  long result;
  result = syscall(SYS_pidfd_open, (long)pid, (long)0);
  return (int)result;
}

static int n_process_pidfd_send_signal(int pidfd, int signal_number) {
  long result;
  result = syscall(SYS_pidfd_send_signal, (long)pidfd, (long)signal_number, 0L, (long)0);
  return (int)result;
}

static int n_process_native_signal_number_get(NexusProcessSignal signal) {
  switch (signal) {
  case NEXUS_PROCESS_SIGNAL_INTERRUPT:
    return SIGINT;

  case NEXUS_PROCESS_SIGNAL_TERMINATE:
    return SIGTERM;

  case NEXUS_PROCESS_SIGNAL_KILL:
    return SIGKILL;

  default:
    return 0;
  }
}

/*
Summary: the argv/envp execve actually wants is char *const[], predating POSIX const-correctness;
  execve itself never writes through either array. Going through (const void *) is the standard
  way to cast away constness without -Wcast-qual flagging it as accidental.
*/
static char *const *n_process_exec_argv_get(const char *const *argv) {
  return (char *const *)(const void *)argv;
}

/*
Summary: runs entirely between fork() and either execve() succeeding or _exit(127) -- every
  operation here (dup2, close, chdir, open, write, execve, _exit) is async-signal-safe, and
  nothing from Nexus's own error/context/allocation machinery may be touched on this path: the
  parent may be mid-mutation of that shared static state at the instant of fork, and the child
  starts with a frozen, possibly-inconsistent copy of it.
Params: error_pipe_write_fd  close-on-exec; closes itself on a successful execve, otherwise this
  function writes errno to it before _exit(127)
*/
static void n_process_child_run(const NexusProcessSpawnOptions *options, int error_pipe_write_fd, int stdin_child_fd, int stdout_child_fd,
                                int stderr_child_fd) {
  int failure_errno;

  if ((stdin_child_fd >= 0 && dup2(stdin_child_fd, STDIN_FILENO) < 0) || (stdout_child_fd >= 0 && dup2(stdout_child_fd, STDOUT_FILENO) < 0) ||
      (stderr_child_fd >= 0 && dup2(stderr_child_fd, STDERR_FILENO) < 0)) {
    failure_errno = errno;
    (void)write(error_pipe_write_fd, &failure_errno, sizeof(failure_errno));
    _exit(127);
  }

  if (options->working_directory != NULL && chdir(options->working_directory) != 0) {
    failure_errno = errno;
    (void)write(error_pipe_write_fd, &failure_errno, sizeof(failure_errno));
    _exit(127);
  }

  execve(options->argv[0], n_process_exec_argv_get(options->argv), options->envp != NULL ? n_process_exec_argv_get(options->envp) : environ);

  /* execve only returns on failure. */
  failure_errno = errno;
  (void)write(error_pipe_write_fd, &failure_errno, sizeof(failure_errno));
  _exit(127);
}

/*
Summary: opens the pipe (if mode is NEXUS_PROCESS_STREAM_PIPE) or /dev/null (if DISCARD) a single
  stream needs, split into the fd the child dup2's onto its own std* slot and the fd the parent
  keeps (-1 if the stream is piped from the child's side / not applicable).
Params:
  out_child_fd   the fd the child should dup2 onto its std* slot, or -1 for INHERIT (no dup2 needed)
  out_parent_fd  the parent-side pipe end to keep open (PIPE only), else -1
*/
static boolean n_process_stream_open(NexusProcessStreamMode mode, boolean is_input, int *out_child_fd, int *out_parent_fd) {
  int pipe_fds[2];
  int devnull_fd;

  switch (mode) {
  case NEXUS_PROCESS_STREAM_INHERIT:
    *out_child_fd  = -1;
    *out_parent_fd = -1;
    return TRUE;

  case NEXUS_PROCESS_STREAM_PIPE:
    if (pipe(pipe_fds) != 0) {
      return FALSE;
    }
    if (fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC) != 0) {
      (void)close(pipe_fds[0]);
      (void)close(pipe_fds[1]);
      return FALSE;
    }
    /* is_input: the child reads, so its fd is the pipe's read end and the parent writes to the
       write end. Otherwise (stdout/stderr) it's the reverse. */
    *out_child_fd  = is_input ? pipe_fds[0] : pipe_fds[1];
    *out_parent_fd = is_input ? pipe_fds[1] : pipe_fds[0];
    return TRUE;

  case NEXUS_PROCESS_STREAM_DISCARD:
    devnull_fd = open("/dev/null", is_input ? O_RDONLY : O_WRONLY);
    if (devnull_fd < 0) {
      return FALSE;
    }
    *out_child_fd  = devnull_fd;
    *out_parent_fd = -1;
    return TRUE;

  default:
    return FALSE;
  }
}

NexusError *nexus_process_spawn(const NexusProcessSpawnOptions *options, NexusProcess **out_process) {
  int           error_pipe_fds[2];
  int           stdin_child_fd;
  int           stdin_parent_fd;
  int           stdout_child_fd;
  int           stdout_parent_fd;
  int           stderr_child_fd;
  int           stderr_parent_fd;
  pid_t         child_pid;
  int           pidfd;
  ssize_t       bytes_read;
  int           child_errno;
  NexusError   *error;
  NexusProcess *process;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_spawn");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(options != NULL, "process_spawn: options must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_process != NULL, "process_spawn: out_process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(options->argv != NULL && options->argv[0] != NULL,
                                    "process_spawn: options->argv must be non-NULL with a non-NULL argv[0]");

  if (!n_process_stream_open(options->stdin_mode, TRUE, &stdin_child_fd, &stdin_parent_fd)) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_spawn: failed to set up the child's stdin");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  if (!n_process_stream_open(options->stdout_mode, FALSE, &stdout_child_fd, &stdout_parent_fd)) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_spawn: failed to set up the child's stdout");
    if (stdin_child_fd >= 0) {
      (void)close(stdin_child_fd);
    }
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  if (!n_process_stream_open(options->stderr_mode, FALSE, &stderr_child_fd, &stderr_parent_fd)) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_spawn: failed to set up the child's stderr");
    if (stdin_child_fd >= 0) {
      (void)close(stdin_child_fd);
    }
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    if (stdout_child_fd >= 0) {
      (void)close(stdout_child_fd);
    }
    if (stdout_parent_fd >= 0) {
      (void)close(stdout_parent_fd);
    }
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (pipe(error_pipe_fds) != 0 || fcntl(error_pipe_fds[0], F_SETFD, FD_CLOEXEC) != 0 || fcntl(error_pipe_fds[1], F_SETFD, FD_CLOEXEC) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_spawn: failed to create the exec-status pipe");
    if (stdin_child_fd >= 0) {
      (void)close(stdin_child_fd);
    }
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    if (stdout_child_fd >= 0) {
      (void)close(stdout_child_fd);
    }
    if (stdout_parent_fd >= 0) {
      (void)close(stdout_parent_fd);
    }
    if (stderr_child_fd >= 0) {
      (void)close(stderr_child_fd);
    }
    if (stderr_parent_fd >= 0) {
      (void)close(stderr_parent_fd);
    }
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  child_pid = fork();
  if (child_pid < 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_spawn: fork failed");
    (void)close(error_pipe_fds[0]);
    (void)close(error_pipe_fds[1]);
    if (stdin_child_fd >= 0) {
      (void)close(stdin_child_fd);
    }
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    if (stdout_child_fd >= 0) {
      (void)close(stdout_child_fd);
    }
    if (stdout_parent_fd >= 0) {
      (void)close(stdout_parent_fd);
    }
    if (stderr_child_fd >= 0) {
      (void)close(stderr_child_fd);
    }
    if (stderr_parent_fd >= 0) {
      (void)close(stderr_parent_fd);
    }
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (child_pid == 0) {
    (void)close(error_pipe_fds[0]);
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    if (stdout_parent_fd >= 0) {
      (void)close(stdout_parent_fd);
    }
    if (stderr_parent_fd >= 0) {
      (void)close(stderr_parent_fd);
    }
    n_process_child_run(options, error_pipe_fds[1], stdin_child_fd, stdout_child_fd, stderr_child_fd);
    /* n_process_child_run never returns -- it always execve's or _exit(127)'s. */
  }

  /* Parent, from here on. */
  (void)close(error_pipe_fds[1]);
  if (stdin_child_fd >= 0) {
    (void)close(stdin_child_fd);
  }
  if (stdout_child_fd >= 0) {
    (void)close(stdout_child_fd);
  }
  if (stderr_child_fd >= 0) {
    (void)close(stderr_child_fd);
  }

  /* Blocks until the child either exec's (pipe closes via O_CLOEXEC, read returns 0) or reports
     a setup/exec errno -- both happen within microseconds of fork, never an open-ended wait. */
  bytes_read = read(error_pipe_fds[0], &child_errno, sizeof(child_errno));
  (void)close(error_pipe_fds[0]);

  if (bytes_read > 0) {
    /* The child reported a setup or exec failure and has already _exit(127)'d; reap it now so it
       doesn't linger as a zombie the caller was never given a handle to reap. */
    (void)waitpid(child_pid, NULL, 0);
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    if (stdout_parent_fd >= 0) {
      (void)close(stdout_parent_fd);
    }
    if (stderr_parent_fd >= 0) {
      (void)close(stderr_parent_fd);
    }
    error =
        _nexus_program_errors_from_errno_create(child_errno, "process_spawn: the child failed to start '%s'", __FILE__, __LINE__, options->argv[0]);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  pidfd = n_process_pidfd_open(child_pid);
  if (pidfd < 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_spawn: pidfd_open failed for pid %ld", (long)child_pid);
    /* The child is already running and has exec'd successfully; this process just can't track it
       race-free going forward. Leaving it running (not killing it) is the same destroy-never-kills
       guarantee nexus_process_destroy makes -- a spawn that can't hand back a usable handle still
       must not silently kill what it started. */
    if (stdin_parent_fd >= 0) {
      (void)close(stdin_parent_fd);
    }
    if (stdout_parent_fd >= 0) {
      (void)close(stdout_parent_fd);
    }
    if (stderr_parent_fd >= 0) {
      (void)close(stderr_parent_fd);
    }
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  process                     = (NexusProcess *)nexus_memory_allocate(NEXUS_SIZEOF(*process));
  process->pid                = child_pid;
  process->pidfd              = pidfd;
  process->stdin_fd           = stdin_parent_fd;
  process->stdout_fd          = stdout_parent_fd;
  process->stderr_fd          = stderr_parent_fd;
  process->has_exited         = FALSE;
  process->exit_status.kind   = NEXUS_PROCESS_EXIT_NORMAL;
  process->exit_status.detail = 0;

  *out_process = process;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

int32 nexus_process_id_get(const NexusProcess *process) {
  int32 pid;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_id_get");
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process_id_get: process must not be NULL");
  pid = (int32)process->pid;
  NEXUS_PROGRAM_CONTEXT_POP();
  return pid;
}

int32 nexus_process_stdin_fd_get(const NexusProcess *process) {
  int32 descriptor;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_stdin_fd_get");
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process_stdin_fd_get: process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(process->stdin_fd >= 0, "process_stdin_fd_get: process's stdin was not spawned with NEXUS_PROCESS_STREAM_PIPE");
  descriptor = (int32)process->stdin_fd;
  NEXUS_PROGRAM_CONTEXT_POP();
  return descriptor;
}

int32 nexus_process_stdout_fd_get(const NexusProcess *process) {
  int32 descriptor;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_stdout_fd_get");
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process_stdout_fd_get: process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(process->stdout_fd >= 0, "process_stdout_fd_get: process's stdout was not spawned with NEXUS_PROCESS_STREAM_PIPE");
  descriptor = (int32)process->stdout_fd;
  NEXUS_PROGRAM_CONTEXT_POP();
  return descriptor;
}

/*
Summary: reaps process via waitid(P_PIDFD, ...) and fills its cached exit status. Only called
  once pidfd has actually indicated the child is exited/exiting -- waitid here never blocks.
*/
static NexusError *n_process_reap(NexusProcess *process) {
  siginfo_t   info;
  NexusError *error;

  info.si_pid = 0;
  if (waitid(P_PIDFD, (id_t)process->pidfd, &info, WEXITED) != 0 || info.si_pid == 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_reap: waitid failed for pid %ld", (long)process->pid);
    return error;
  }

  if (info.si_code == CLD_EXITED) {
    process->exit_status.kind   = NEXUS_PROCESS_EXIT_NORMAL;
    process->exit_status.detail = info.si_status;
  } else {
    process->exit_status.kind   = NEXUS_PROCESS_EXIT_SIGNALED;
    process->exit_status.detail = info.si_status;
  }
  process->has_exited = TRUE;
  return &nexus_program_errors_success;
}

NexusError *nexus_process_poll(NexusProcess *process, NexusProcessExitStatus *out_status, boolean *out_has_exited) {
  struct pollfd poll_target;
  int           poll_result;
  NexusError   *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_poll");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(process != NULL, "process_poll: process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_status != NULL, "process_poll: out_status must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_has_exited != NULL, "process_poll: out_has_exited must not be NULL");

  if (process->has_exited) {
    *out_status     = process->exit_status;
    *out_has_exited = TRUE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  poll_target.fd     = process->pidfd;
  poll_target.events = POLLIN;
  poll_result        = poll(&poll_target, 1, 0);
  if (poll_result < 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_poll: poll failed for pid %ld", (long)process->pid);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  if (poll_result == 0) {
    *out_has_exited = FALSE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = n_process_reap(process);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  *out_status     = process->exit_status;
  *out_has_exited = TRUE;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_process_wait(NexusProcess *process, int32 timeout_milliseconds, NexusProcessExitStatus *out_status, boolean *out_has_exited) {
  struct pollfd poll_target;
  int           poll_result;
  NexusError   *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_wait");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(process != NULL, "process_wait: process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_status != NULL, "process_wait: out_status must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_has_exited != NULL, "process_wait: out_has_exited must not be NULL");

  if (process->has_exited) {
    *out_status     = process->exit_status;
    *out_has_exited = TRUE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  poll_target.fd     = process->pidfd;
  poll_target.events = POLLIN;
  poll_result        = poll(&poll_target, 1, timeout_milliseconds < 0 ? -1 : (int)timeout_milliseconds);
  if (poll_result < 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_wait: poll failed for pid %ld", (long)process->pid);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  if (poll_result == 0) {
    *out_has_exited = FALSE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  error = n_process_reap(process);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  *out_status     = process->exit_status;
  *out_has_exited = TRUE;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

/*
Summary: appends [data, data + size) to list (element_size 1) -- list_append_uninitialized bumps
  the length by one element at a time (no bulk-append exists on NexusDataList today), so this
  reserves once up front to guarantee no reallocation happens mid-loop, then the per-byte calls
  are pure bump-and-copy. Not a hot path: bounded by pipe throughput, not called per-frame.
*/
static void n_process_buffer_append(NexusDataList *list, const byte *data, uint_large size) {
  uint_large i;
  void      *slot;

  if (size == 0) {
    return;
  }
  nexus_data_list_reserve(list, nexus_data_list_length_get(list) + size);
  for (i = 0; i < size; i++) {
    slot          = nexus_data_list_append_uninitialized(list);
    *(byte *)slot = data[i];
  }
}

NexusError *nexus_process_communicate(NexusProcess *process, const void *stdin_data, uint_large stdin_size, NexusDataList *out_stdout,
                                      NexusDataList *out_stderr, int32 timeout_milliseconds, NexusProcessExitStatus *out_status,
                                      boolean *out_has_exited) {
  struct pollfd poll_targets[4];
  nfds_t        poll_count;
  int           stdin_index;
  int           stdout_index;
  int           stderr_index;
  int           pidfd_index;
  uint_large    stdin_written;
  byte          read_buffer[4096];
  ssize_t       io_result;
  int           poll_result;
  NexusError   *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_communicate");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(process != NULL, "process_communicate: process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_status != NULL, "process_communicate: out_status must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_has_exited != NULL, "process_communicate: out_has_exited must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS((out_stdout != NULL) == (process->stdout_fd >= 0),
                                    "process_communicate: out_stdout must be given if and only if stdout_mode was NEXUS_PROCESS_STREAM_PIPE");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS((out_stderr != NULL) == (process->stderr_fd >= 0),
                                    "process_communicate: out_stderr must be given if and only if stderr_mode was NEXUS_PROCESS_STREAM_PIPE");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(stdin_size == 0 || process->stdin_fd >= 0,
                                    "process_communicate: stdin_data/stdin_size given but stdin_mode was not NEXUS_PROCESS_STREAM_PIPE");

  if (process->has_exited) {
    *out_status     = process->exit_status;
    *out_has_exited = TRUE;
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  stdin_written = 0;

  for (;;) {
    poll_count   = 0;
    stdin_index  = -1;
    stdout_index = -1;
    stderr_index = -1;

    if (process->stdin_fd >= 0 && stdin_written < stdin_size) {
      stdin_index                     = (int)poll_count;
      poll_targets[poll_count].fd     = process->stdin_fd;
      poll_targets[poll_count].events = POLLOUT;
      poll_count++;
    }
    if (process->stdout_fd >= 0) {
      stdout_index                    = (int)poll_count;
      poll_targets[poll_count].fd     = process->stdout_fd;
      poll_targets[poll_count].events = POLLIN;
      poll_count++;
    }
    if (process->stderr_fd >= 0) {
      stderr_index                    = (int)poll_count;
      poll_targets[poll_count].fd     = process->stderr_fd;
      poll_targets[poll_count].events = POLLIN;
      poll_count++;
    }
    pidfd_index                     = (int)poll_count;
    poll_targets[poll_count].fd     = process->pidfd;
    poll_targets[poll_count].events = POLLIN;
    poll_count++;

    poll_result = poll(poll_targets, poll_count, timeout_milliseconds < 0 ? -1 : (int)timeout_milliseconds);
    if (poll_result < 0) {
      error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_communicate: poll failed for pid %ld", (long)process->pid);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
    if (poll_result == 0) {
      *out_has_exited = FALSE;
      NEXUS_PROGRAM_CONTEXT_POP();
      return &nexus_program_errors_success;
    }

    if (stdin_index >= 0 && (poll_targets[stdin_index].revents & (POLLOUT | POLLERR)) != 0) {
      io_result = write(process->stdin_fd, (const byte *)stdin_data + stdin_written, stdin_size - stdin_written);
      if (io_result < 0) {
        if (errno == EPIPE) {
          /* The child stopped reading its stdin -- not a failure of this call; see Description. */
          (void)close(process->stdin_fd);
          process->stdin_fd = -1;
        } else {
          error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_communicate: writing stdin failed for pid %ld", (long)process->pid);
          NEXUS_PROGRAM_CONTEXT_POP();
          return error;
        }
      } else {
        stdin_written += (uint_large)io_result;
        if (stdin_written >= stdin_size) {
          (void)close(process->stdin_fd);
          process->stdin_fd = -1;
        }
      }
    }

    if (stdout_index >= 0 && (poll_targets[stdout_index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      io_result = read(process->stdout_fd, read_buffer, sizeof(read_buffer));
      if (io_result < 0) {
        error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_communicate: reading stdout failed for pid %ld", (long)process->pid);
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }
      if (io_result == 0) {
        (void)close(process->stdout_fd);
        process->stdout_fd = -1;
      } else {
        n_process_buffer_append(out_stdout, read_buffer, (uint_large)io_result);
      }
    }

    if (stderr_index >= 0 && (poll_targets[stderr_index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      io_result = read(process->stderr_fd, read_buffer, sizeof(read_buffer));
      if (io_result < 0) {
        error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_communicate: reading stderr failed for pid %ld", (long)process->pid);
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }
      if (io_result == 0) {
        (void)close(process->stderr_fd);
        process->stderr_fd = -1;
      } else {
        n_process_buffer_append(out_stderr, read_buffer, (uint_large)io_result);
      }
    }

    if ((poll_targets[pidfd_index].revents & POLLIN) != 0 && process->stdout_fd < 0 && process->stderr_fd < 0) {
      /* The child has exited and every piped stream it had is fully drained. */
      error = n_process_reap(process);
      if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
        NEXUS_PROGRAM_CONTEXT_POP();
        return error;
      }
      *out_status     = process->exit_status;
      *out_has_exited = TRUE;
      NEXUS_PROGRAM_CONTEXT_POP();
      return &nexus_program_errors_success;
    }
  }
}

NexusError *nexus_process_signal(NexusProcess *process, NexusProcessSignal signal) {
  int         native_signal;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_signal");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(process != NULL, "process_signal: process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(signal < NEXUS_PROCESS_SIGNAL_COUNT, "process_signal: invalid NexusProcessSignal: %d", (int)signal);

  native_signal = n_process_native_signal_number_get(signal);
  if (n_process_pidfd_send_signal(process->pidfd, native_signal) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("process_signal: pidfd_send_signal failed for pid %ld", (long)process->pid);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

void nexus_process_destroy(NexusProcess *process) {
  if (process == NULL) {
    return;
  }

  (void)close(process->pidfd);
  if (process->stdin_fd >= 0) {
    (void)close(process->stdin_fd);
  }
  if (process->stdout_fd >= 0) {
    (void)close(process->stdout_fd);
  }
  if (process->stderr_fd >= 0) {
    (void)close(process->stderr_fd);
  }
  nexus_memory_deallocate(process);
}

#else

NexusError *nexus_process_spawn(const NexusProcessSpawnOptions *options, NexusProcess **out_process) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_spawn");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_spawn");
}
int32 nexus_process_id_get(const NexusProcess *process) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_id_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_id_get");
}
int32 nexus_process_stdin_fd_get(const NexusProcess *process) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_stdin_fd_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_stdin_fd_get");
}
int32 nexus_process_stdout_fd_get(const NexusProcess *process) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_stdout_fd_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_stdout_fd_get");
}
NexusError *nexus_process_poll(NexusProcess *process, NexusProcessExitStatus *out_status, boolean *out_has_exited) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_poll");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_poll");
}
NexusError *nexus_process_wait(NexusProcess *process, int32 timeout_milliseconds, NexusProcessExitStatus *out_status, boolean *out_has_exited) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_wait");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_wait");
}
NexusError *nexus_process_communicate(NexusProcess *process, const void *stdin_data, uint_large stdin_size, NexusDataList *out_stdout,
                                      NexusDataList *out_stderr, int32 timeout_milliseconds, NexusProcessExitStatus *out_status,
                                      boolean *out_has_exited) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_communicate");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_communicate");
}
NexusError *nexus_process_signal(NexusProcess *process, NexusProcessSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_signal");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_signal");
}
void nexus_process_destroy(NexusProcess *process) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_process_destroy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("process_destroy");
}

#endif
