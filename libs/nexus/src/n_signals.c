#include "../nexus.h"

#if NEXUS_PLATFORM_POSIX

#  include <signal.h>

static volatile sig_atomic_t n_signals_received[NEXUS_SIGNAL_COUNT];

static void n_signals_handler(int signal_number) {
  switch (signal_number) {
  case SIGINT:
    n_signals_received[NEXUS_SIGNAL_INTERRUPT] = 1;
    break;

  case SIGTERM:
    n_signals_received[NEXUS_SIGNAL_TERMINATE] = 1;
    break;

  default:
    break;
  }
}

static int n_signals_native_number_get(NexusSignal signal) {
  switch (signal) {
  case NEXUS_SIGNAL_INTERRUPT:
    return SIGINT;

  case NEXUS_SIGNAL_TERMINATE:
    return SIGTERM;

  default:
    return 0;
  }
}

NexusError *nexus_signals_capture(NexusSignal signal) {
  struct sigaction action;
  int              signal_number;
  NexusError      *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_capture");
  NEXUS_PROGRAM_PRECONDITION(signal < NEXUS_SIGNAL_COUNT, "invalid NexusSignal: %d", (int)signal);

  signal_number              = n_signals_native_number_get(signal);
  n_signals_received[signal] = 0;

  action.sa_handler = n_signals_handler;
  action.sa_flags   = 0;

  if (sigemptyset(&action.sa_mask) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize the signal mask for signal %d", signal_number);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (sigaction(signal_number, &action, NULL) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to install the handler for signal %d", signal_number);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_signals_release(NexusSignal signal) {
  struct sigaction action;
  int              signal_number;
  NexusError      *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_release");
  NEXUS_PROGRAM_PRECONDITION(signal < NEXUS_SIGNAL_COUNT, "invalid NexusSignal: %d", (int)signal);

  signal_number = n_signals_native_number_get(signal);

  action.sa_handler = SIG_DFL;
  action.sa_flags   = 0;

  if (sigemptyset(&action.sa_mask) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize the signal mask for signal %d", signal_number);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  if (sigaction(signal_number, &action, NULL) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to restore the default handler for signal %d", signal_number);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  n_signals_received[signal] = 0;

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

boolean nexus_signals_received_get(NexusSignal signal) {
  boolean received;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_received_get");
  NEXUS_PROGRAM_PRECONDITION(signal < NEXUS_SIGNAL_COUNT, "invalid NexusSignal: %d", (int)signal);
  received = n_signals_received[signal] != 0 ? TRUE : FALSE;
  NEXUS_PROGRAM_CONTEXT_POP();
  return received;
}

boolean nexus_signals_received_exchange(NexusSignal signal) {
  boolean received;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_received_exchange");
  NEXUS_PROGRAM_PRECONDITION(signal < NEXUS_SIGNAL_COUNT, "invalid NexusSignal: %d", (int)signal);

  received                   = n_signals_received[signal] != 0 ? TRUE : FALSE;
  n_signals_received[signal] = 0;

  NEXUS_PROGRAM_CONTEXT_POP();
  return received;
}

void nexus_signals_received_clear(NexusSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_received_clear");
  NEXUS_PROGRAM_PRECONDITION(signal < NEXUS_SIGNAL_COUNT, "invalid NexusSignal: %d", (int)signal);
  n_signals_received[signal] = 0;
  NEXUS_PROGRAM_CONTEXT_POP();
}

#else

NexusError *nexus_signals_capture(NexusSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_capture");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("signals_capture");
}
NexusError *nexus_signals_release(NexusSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_release");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("signals_release");
}
boolean nexus_signals_received_get(NexusSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_received_get");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("signals_received_get");
}
boolean nexus_signals_received_exchange(NexusSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_received_exchange");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("signals_received_exchange");
}
void nexus_signals_received_clear(NexusSignal signal) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_signals_received_clear");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("signals_received_clear");
}

#endif
