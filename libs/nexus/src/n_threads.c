#include "../nexus.h"

/* pthread functions return their error code directly rather than setting errno. Every
   fallible pthread call copies that code to errno immediately before creating a NexusError. */

#if NEXUS_PLATFORM_POSIX

#  include <errno.h>
#  include <pthread.h>
#  include <semaphore.h>
#  include <time.h>

struct NexusThread {
  pthread_t handle;
};

struct NexusMutex {
  pthread_mutex_t handle;
};

struct NexusCond {
  pthread_cond_t handle;
};

struct NexusSemaphore {
  sem_t handle;
};

struct NexusThreadsWaitGroup {
  pthread_mutex_t mutex;
  pthread_cond_t  cond;
  int32           counter;
};

void nexus_threads_sleep(NexusDuration duration) {
  struct timespec sleep_time;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_sleep");

  if (duration.nanoseconds <= 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }

  sleep_time.tv_sec  = (time_t)(duration.nanoseconds / (int64)NEXUS_NANOSECONDS_PER_SECOND);
  sleep_time.tv_nsec = (long)(duration.nanoseconds % (int64)NEXUS_NANOSECONDS_PER_SECOND);

  /* nanosleep writes the remaining time back into sleep_time on EINTR -- looping on it finishes
     out the original requested duration rather than returning early after a signal. */
  while (nanosleep(&sleep_time, &sleep_time) != 0 && errno == EINTR) {
  }

  NEXUS_PROGRAM_CONTEXT_POP();
}

void nexus_threads_spin_wait(NexusDuration duration) {
  NexusTime start;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_spin_wait");
  NEXUS_PROGRAM_ASSERT(duration.nanoseconds >= 0, "threads_spin_wait: duration must not be negative");

  if (duration.nanoseconds <= 0) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return;
  }

  start = nexus_time_get_monotonic();

  while (nexus_time_duration_between(start, nexus_time_get_monotonic()).nanoseconds < duration.nanoseconds) {
  }

  NEXUS_PROGRAM_CONTEXT_POP();
}

typedef struct NThreadStartContext {
  NexusThreadFunc entry_func;
  void           *user_data;
} NThreadStartContext;

static void *n_threads_start_trampoline(void *raw_context) {
  NThreadStartContext *context;
  NexusThreadFunc      entry_func;
  void                *user_data;

  context    = (NThreadStartContext *)raw_context;
  entry_func = context->entry_func;
  user_data  = context->user_data;

  nexus_memory_deallocate(context);

  entry_func(user_data);

  return NULL;
}

NexusError *nexus_threads_create(NexusThread **out_thread, NexusThreadFunc entry_func, void *user_data) {
  NThreadStartContext *context;
  NexusThread         *thread;
  NexusError          *error;
  int                  result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_create");
  NEXUS_PROGRAM_PRECONDITION(out_thread != NULL, "threads_create: out_thread must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(entry_func != NULL, "threads_create: entry_func must not be NULL");

  thread = (NexusThread *)nexus_memory_allocate(NEXUS_SIZEOF(NexusThread));

  context             = (NThreadStartContext *)nexus_memory_allocate(NEXUS_SIZEOF(NThreadStartContext));
  context->entry_func = entry_func;
  context->user_data  = user_data;

  result = pthread_create(&thread->handle, NULL, n_threads_start_trampoline, context);
  if (result != 0) {
    nexus_memory_deallocate(context);
    nexus_memory_deallocate(thread);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to create thread");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_thread = thread;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_join(NexusThread *thread) {
  NexusError *error;
  int         result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_join");
  NEXUS_PROGRAM_PRECONDITION(thread != NULL, "threads_join: thread must not be NULL");

  result = pthread_join(thread->handle, NULL);
  nexus_memory_deallocate(thread);

  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to join thread");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_mutex_create(NexusMutex **out_mutex) {
  NexusMutex         *mutex;
  NexusError         *error;
  pthread_mutexattr_t attributes;
  int                 result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_create");
  NEXUS_PROGRAM_PRECONDITION(out_mutex != NULL, "threads_mutex_create: out_mutex must not be NULL");

  mutex = (NexusMutex *)nexus_memory_allocate(NEXUS_SIZEOF(NexusMutex));

  result = pthread_mutexattr_init(&attributes);
  if (result != 0) {
    nexus_memory_deallocate(mutex);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize mutex attributes");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  (void)pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);

  result = pthread_mutex_init(&mutex->handle, &attributes);
  (void)pthread_mutexattr_destroy(&attributes);

  if (result != 0) {
    nexus_memory_deallocate(mutex);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize mutex");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_mutex = mutex;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_mutex_lock(NexusMutex *mutex) {
  NexusError *error;
  int         result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_lock");
  NEXUS_PROGRAM_PRECONDITION(mutex != NULL, "threads_mutex_lock: mutex must not be NULL");

  result = pthread_mutex_lock(&mutex->handle);
  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to lock mutex");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

boolean nexus_threads_mutex_try_lock(NexusMutex *mutex) {
  boolean locked;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_try_lock");
  NEXUS_PROGRAM_PRECONDITION(mutex != NULL, "threads_mutex_try_lock: mutex must not be NULL");

  locked = (pthread_mutex_trylock(&mutex->handle) == 0) ? TRUE : FALSE;

  NEXUS_PROGRAM_CONTEXT_POP();
  return locked;
}

NexusError *nexus_threads_mutex_unlock(NexusMutex *mutex) {
  NexusError *error;
  int         result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_unlock");
  NEXUS_PROGRAM_PRECONDITION(mutex != NULL, "threads_mutex_unlock: mutex must not be NULL");

  result = pthread_mutex_unlock(&mutex->handle);
  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to unlock mutex");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

void nexus_threads_mutex_destroy(NexusMutex *mutex) {
  if (mutex == NULL) {
    return;
  }

  (void)pthread_mutex_destroy(&mutex->handle);
  nexus_memory_deallocate(mutex);
}

NexusError *nexus_threads_cond_create(NexusCond **out_cond) {
  NexusCond         *cond;
  NexusError        *error;
  pthread_condattr_t attributes;
  int                result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_create");
  NEXUS_PROGRAM_PRECONDITION(out_cond != NULL, "threads_cond_create: out_cond must not be NULL");

  cond = (NexusCond *)nexus_memory_allocate(NEXUS_SIZEOF(NexusCond));

  result = pthread_condattr_init(&attributes);
  if (result != 0) {
    nexus_memory_deallocate(cond);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize condition variable attributes");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  /* CLOCK_MONOTONIC rather than the default CLOCK_REALTIME -- a timed wait's deadline must not
     be vulnerable to the wall clock being stepped (NTP sync, manual adjustment, DST) mid-wait. */
  (void)pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);

  result = pthread_cond_init(&cond->handle, &attributes);
  (void)pthread_condattr_destroy(&attributes);

  if (result != 0) {
    nexus_memory_deallocate(cond);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize condition variable");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_cond = cond;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_cond_wait(NexusCond *cond, NexusMutex *mutex) {
  NexusError *error;
  int         result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_wait");
  NEXUS_PROGRAM_PRECONDITION(cond != NULL, "threads_cond_wait: cond must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(mutex != NULL, "threads_cond_wait: mutex must not be NULL");

  result = pthread_cond_wait(&cond->handle, &mutex->handle);
  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to wait on condition variable");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

boolean nexus_threads_cond_wait_timeout(NexusCond *cond, NexusMutex *mutex, NexusDuration duration) {
  struct timespec deadline;
  NexusTime       now;
  timestamp       deadline_ns;
  boolean         signaled;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_wait_timeout");
  NEXUS_PROGRAM_PRECONDITION(cond != NULL, "threads_cond_wait_timeout: cond must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(mutex != NULL, "threads_cond_wait_timeout: mutex must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(duration.nanoseconds > 0, "threads_cond_wait_timeout: duration must be strictly positive");

  /* Matches CLOCK_MONOTONIC set on this cond's attributes at creation -- same clock, same
     epoch, so adding a relative duration to "now" produces a valid absolute deadline. */
  now         = nexus_time_get_monotonic();
  deadline_ns = now.time + (uint64)duration.nanoseconds;

  deadline.tv_sec  = (time_t)(deadline_ns / NEXUS_NANOSECONDS_PER_SECOND);
  deadline.tv_nsec = (long)(deadline_ns % NEXUS_NANOSECONDS_PER_SECOND);

  signaled = (pthread_cond_timedwait(&cond->handle, &mutex->handle, &deadline) == 0) ? TRUE : FALSE;

  NEXUS_PROGRAM_CONTEXT_POP();
  return signaled;
}

NexusError *nexus_threads_cond_signal(NexusCond *cond) {
  NexusError *error;
  int         result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_signal");
  NEXUS_PROGRAM_PRECONDITION(cond != NULL, "threads_cond_signal: cond must not be NULL");

  result = pthread_cond_signal(&cond->handle);
  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to signal condition variable");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_cond_broadcast(NexusCond *cond) {
  NexusError *error;
  int         result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_broadcast");
  NEXUS_PROGRAM_PRECONDITION(cond != NULL, "threads_cond_broadcast: cond must not be NULL");

  result = pthread_cond_broadcast(&cond->handle);
  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to broadcast condition variable");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

void nexus_threads_cond_destroy(NexusCond *cond) {
  if (cond == NULL) {
    return;
  }

  (void)pthread_cond_destroy(&cond->handle);
  nexus_memory_deallocate(cond);
}

NexusError *nexus_threads_semaphore_create(NexusSemaphore **out_semaphore, uint32 initial_count) {
  NexusSemaphore *semaphore;
  NexusError     *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_create");
  NEXUS_PROGRAM_PRECONDITION(out_semaphore != NULL, "threads_semaphore_create: out_semaphore must not be NULL");

  semaphore = (NexusSemaphore *)nexus_memory_allocate(NEXUS_SIZEOF(NexusSemaphore));

  if (sem_init(&semaphore->handle, 0, initial_count) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize semaphore");
    nexus_memory_deallocate(semaphore);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_semaphore = semaphore;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_semaphore_wait(NexusSemaphore *sem) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_wait");
  NEXUS_PROGRAM_PRECONDITION(sem != NULL, "threads_semaphore_wait: sem must not be NULL");

  while (sem_wait(&sem->handle) != 0) {
    if (errno != EINTR) {
      error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to wait on semaphore");
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_semaphore_post(NexusSemaphore *sem) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_post");
  NEXUS_PROGRAM_PRECONDITION(sem != NULL, "threads_semaphore_post: sem must not be NULL");

  if (sem_post(&sem->handle) != 0) {
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to post to semaphore");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

void nexus_threads_semaphore_destroy(NexusSemaphore *sem) {
  if (sem == NULL) {
    return;
  }

  (void)sem_destroy(&sem->handle);
  nexus_memory_deallocate(sem);
}

NexusError *nexus_threads_waitgroup_create(NexusThreadsWaitGroup **out_waitgroup) {
  NexusThreadsWaitGroup *waitgroup;
  NexusError            *error;
  int                    result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_create");
  NEXUS_PROGRAM_PRECONDITION(out_waitgroup != NULL, "threads_waitgroup_create: out_waitgroup must not be NULL");

  waitgroup          = (NexusThreadsWaitGroup *)nexus_memory_allocate(NEXUS_SIZEOF(NexusThreadsWaitGroup));
  waitgroup->counter = 0;

  result = pthread_mutex_init(&waitgroup->mutex, NULL);
  if (result != 0) {
    nexus_memory_deallocate(waitgroup);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize waitgroup mutex");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  result = pthread_cond_init(&waitgroup->cond, NULL);
  if (result != 0) {
    (void)pthread_mutex_destroy(&waitgroup->mutex);
    nexus_memory_deallocate(waitgroup);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize waitgroup condition variable");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_waitgroup = waitgroup;
  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_waitgroup_add(NexusThreadsWaitGroup *waitgroup, int32 delta) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_add");
  NEXUS_PROGRAM_PRECONDITION(waitgroup != NULL, "threads_waitgroup_add: waitgroup must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(delta != 0, "threads_waitgroup_add: delta must not be 0");

  (void)pthread_mutex_lock(&waitgroup->mutex);

  waitgroup->counter += delta;

  NEXUS_PROGRAM_ASSERT(waitgroup->counter >= 0, "threads_waitgroup_add: counter dropped below zero");

  if (waitgroup->counter <= 0) {
    (void)pthread_cond_broadcast(&waitgroup->cond);
  }

  (void)pthread_mutex_unlock(&waitgroup->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_waitgroup_done(NexusThreadsWaitGroup *waitgroup) {
  return nexus_threads_waitgroup_add(waitgroup, -1);
}

NexusError *nexus_threads_waitgroup_wait(NexusThreadsWaitGroup *waitgroup) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_wait");
  NEXUS_PROGRAM_PRECONDITION(waitgroup != NULL, "threads_waitgroup_wait: waitgroup must not be NULL");

  (void)pthread_mutex_lock(&waitgroup->mutex);

  while (waitgroup->counter > 0) {
    (void)pthread_cond_wait(&waitgroup->cond, &waitgroup->mutex);
  }

  (void)pthread_mutex_unlock(&waitgroup->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

boolean nexus_threads_waitgroup_try_wait(NexusThreadsWaitGroup *waitgroup) {
  boolean is_zero;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_try_wait");
  NEXUS_PROGRAM_PRECONDITION(waitgroup != NULL, "threads_waitgroup_try_wait: waitgroup must not be NULL");

  (void)pthread_mutex_lock(&waitgroup->mutex);
  is_zero = (waitgroup->counter <= 0) ? TRUE : FALSE;
  (void)pthread_mutex_unlock(&waitgroup->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return is_zero;
}

void nexus_threads_waitgroup_destroy(NexusThreadsWaitGroup *waitgroup) {
  if (waitgroup == NULL) {
    return;
  }

  (void)pthread_cond_destroy(&waitgroup->cond);
  (void)pthread_mutex_destroy(&waitgroup->mutex);
  nexus_memory_deallocate(waitgroup);
}

struct NexusChannel {
  pthread_mutex_t mutex;
  pthread_cond_t  not_empty;
  pthread_cond_t  not_full;
  byte           *buffer;
  uint_large      element_size;
  uint_large      capacity;
  uint_large      count;
  uint_large      head;
  uint_large      tail;
  boolean         closed;
};

static NexusError *_n_threads_channel_create(NexusChannel **out_channel, uint_large element_size, uint_large capacity,
                                             const pthread_mutexattr_t *mutex_attr) {
  NexusChannel *channel;
  int           result;

  NEXUS_PROGRAM_PRECONDITION(out_channel != NULL, "threads_channel_create: out_channel must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(element_size > 0, "threads_channel_create: element_size must be > 0");
  NEXUS_PROGRAM_PRECONDITION(capacity > 0, "threads_channel_create: capacity must be > 0 (no unbuffered/rendezvous channels yet)");

  channel = (NexusChannel *)nexus_memory_allocate(NEXUS_SIZEOF(NexusChannel));

  channel->buffer       = (byte *)nexus_memory_allocate(element_size * capacity);
  channel->element_size = element_size;
  channel->capacity     = capacity;
  channel->count        = 0;
  channel->head         = 0;
  channel->tail         = 0;
  channel->closed       = FALSE;

  result = pthread_mutex_init(&channel->mutex, mutex_attr);
  if (result != 0) {
    nexus_memory_deallocate(channel->buffer);
    nexus_memory_deallocate(channel);
    errno = result;
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize channel mutex");
  }

  result = pthread_cond_init(&channel->not_empty, NULL);
  if (result != 0) {
    (void)pthread_mutex_destroy(&channel->mutex);
    nexus_memory_deallocate(channel->buffer);
    nexus_memory_deallocate(channel);
    errno = result;
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize channel condition variable");
  }

  result = pthread_cond_init(&channel->not_full, NULL);
  if (result != 0) {
    (void)pthread_cond_destroy(&channel->not_empty);
    (void)pthread_mutex_destroy(&channel->mutex);
    nexus_memory_deallocate(channel->buffer);
    nexus_memory_deallocate(channel);
    errno = result;
    return NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize channel condition variable");
  }

  *out_channel = channel;
  return &nexus_program_errors_success;
}

NexusError *nexus_threads_channel_create(NexusChannel **out_channel, uint_large element_size, uint_large capacity) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_create");
  error = _n_threads_channel_create(out_channel, element_size, capacity, NULL);
  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

NexusError *nexus_threads_channel_create_recursive(NexusChannel **out_channel, uint_large element_size, uint_large capacity) {
  pthread_mutexattr_t attr;
  NexusError         *error;
  int                 result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_create_recursive");

  result = pthread_mutexattr_init(&attr);
  if (result != 0) {
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to initialize recursive channel mutex attributes");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  if (result != 0) {
    (void)pthread_mutexattr_destroy(&attr);
    errno = result;
    error = NEXUS_PROGRAM_ERROR_FROM_ERRNO("failed to set recursive channel mutex type");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  error = _n_threads_channel_create(out_channel, element_size, capacity, &attr);
  (void)pthread_mutexattr_destroy(&attr);

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

static void _n_threads_channel_write_slot(NexusChannel *channel, const void *element) {
  byte *slot = channel->buffer + (channel->tail * channel->element_size);

  /* Untraced: this moves an already-accounted-for element into ring-buffer storage the channel
     itself owns, not a new application-visible allocation event -- see
     nexus_memory_bytes_copy_untraced. */
  nexus_memory_bytes_copy_untraced(element, slot, channel->element_size);
  channel->tail = (channel->tail + 1) % channel->capacity;
  channel->count++;
}

static void _n_threads_channel_read_slot(NexusChannel *channel, void *out_element) {
  byte *slot = channel->buffer + (channel->head * channel->element_size);

  /* Untraced -- see _n_threads_channel_write_slot. */
  nexus_memory_bytes_copy_untraced(slot, out_element, channel->element_size);
  channel->head = (channel->head + 1) % channel->capacity;
  channel->count--;
}

NexusError *nexus_threads_channel_send(NexusChannel *channel, const void *element) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_send");
  NEXUS_PROGRAM_PRECONDITION(channel != NULL, "threads_channel_send: channel must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "threads_channel_send: element must not be NULL");

  (void)pthread_mutex_lock(&channel->mutex);

  while (channel->count == channel->capacity && !channel->closed) {
    (void)pthread_cond_wait(&channel->not_full, &channel->mutex);
  }

  if (channel->closed) {
    (void)pthread_mutex_unlock(&channel->mutex);
    error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_THREADS_ERROR_CHANNEL_CLOSED, "cannot send on a closed channel");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  _n_threads_channel_write_slot(channel, element);

  (void)pthread_cond_signal(&channel->not_empty);
  (void)pthread_mutex_unlock(&channel->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

boolean nexus_threads_channel_try_send(NexusChannel *channel, const void *element) {
  boolean sent;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_try_send");
  NEXUS_PROGRAM_PRECONDITION(channel != NULL, "threads_channel_try_send: channel must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "threads_channel_try_send: element must not be NULL");

  (void)pthread_mutex_lock(&channel->mutex);

  if (channel->closed || channel->count == channel->capacity) {
    sent = FALSE;
  } else {
    _n_threads_channel_write_slot(channel, element);
    (void)pthread_cond_signal(&channel->not_empty);
    sent = TRUE;
  }

  (void)pthread_mutex_unlock(&channel->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return sent;
}

NexusError *nexus_threads_channel_receive(NexusChannel *channel, void *out_element, boolean *out_closed) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_receive");
  NEXUS_PROGRAM_PRECONDITION(channel != NULL, "threads_channel_receive: channel must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_element != NULL, "threads_channel_receive: out_element must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_closed != NULL, "threads_channel_receive: out_closed must not be NULL");

  (void)pthread_mutex_lock(&channel->mutex);

  while (channel->count == 0 && !channel->closed) {
    (void)pthread_cond_wait(&channel->not_empty, &channel->mutex);
  }

  if (channel->count == 0) {
    /* Closed and fully drained -- report it rather than blocking forever. */
    *out_closed = TRUE;
    (void)pthread_mutex_unlock(&channel->mutex);
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  _n_threads_channel_read_slot(channel, out_element);
  *out_closed = FALSE;

  (void)pthread_cond_signal(&channel->not_full);
  (void)pthread_mutex_unlock(&channel->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

boolean nexus_threads_channel_try_receive(NexusChannel *channel, void *out_element) {
  boolean received;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_try_receive");
  NEXUS_PROGRAM_PRECONDITION(channel != NULL, "threads_channel_try_receive: channel must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(out_element != NULL, "threads_channel_try_receive: out_element must not be NULL");

  (void)pthread_mutex_lock(&channel->mutex);

  if (channel->count == 0) {
    received = FALSE;
  } else {
    _n_threads_channel_read_slot(channel, out_element);
    (void)pthread_cond_signal(&channel->not_full);
    received = TRUE;
  }

  (void)pthread_mutex_unlock(&channel->mutex);

  NEXUS_PROGRAM_CONTEXT_POP();
  return received;
}

void nexus_threads_channel_close(NexusChannel *channel) {
  if (channel == NULL) {
    return;
  }

  (void)pthread_mutex_lock(&channel->mutex);

  channel->closed = TRUE;

  /* Every blocked sender must wake to see closed and fail; every blocked receiver must wake to
     re-check count==0 && closed and finally report closed once drained. */
  (void)pthread_cond_broadcast(&channel->not_full);
  (void)pthread_cond_broadcast(&channel->not_empty);

  (void)pthread_mutex_unlock(&channel->mutex);
}

void nexus_threads_channel_destroy(NexusChannel *channel) {
  if (channel == NULL) {
    return;
  }

  (void)pthread_cond_destroy(&channel->not_full);
  (void)pthread_cond_destroy(&channel->not_empty);
  (void)pthread_mutex_destroy(&channel->mutex);
  nexus_memory_deallocate(channel->buffer);
  nexus_memory_deallocate(channel);
}

#else

void nexus_threads_sleep(NexusDuration duration) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_sleep");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_sleep");
}
void nexus_threads_spin_wait(NexusDuration duration) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_spin_wait");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_spin_wait");
}
NexusError *nexus_threads_create(NexusThread **out_thread, NexusThreadFunc entry_func, void *user_data) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_create");
}
NexusError *nexus_threads_join(NexusThread *thread) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_join");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_join");
}
NexusError *nexus_threads_mutex_create(NexusMutex **out_mutex) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_mutex_create");
}
NexusError *nexus_threads_mutex_lock(NexusMutex *mutex) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_lock");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_mutex_lock");
}
boolean nexus_threads_mutex_try_lock(NexusMutex *mutex) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_try_lock");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_mutex_try_lock");
}
NexusError *nexus_threads_mutex_unlock(NexusMutex *mutex) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_unlock");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_mutex_unlock");
}
void nexus_threads_mutex_destroy(NexusMutex *mutex) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_mutex_destroy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_mutex_destroy");
}
NexusError *nexus_threads_cond_create(NexusCond **out_cond) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_cond_create");
}
NexusError *nexus_threads_cond_wait(NexusCond *cond, NexusMutex *mutex) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_wait");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_cond_wait");
}
boolean nexus_threads_cond_wait_timeout(NexusCond *cond, NexusMutex *mutex, NexusDuration duration) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_wait_timeout");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_cond_wait_timeout");
}
NexusError *nexus_threads_cond_signal(NexusCond *cond) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_signal");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_cond_signal");
}
NexusError *nexus_threads_cond_broadcast(NexusCond *cond) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_broadcast");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_cond_broadcast");
}
void nexus_threads_cond_destroy(NexusCond *cond) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_cond_destroy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_cond_destroy");
}
NexusError *nexus_threads_semaphore_create(NexusSemaphore **out_semaphore, uint32 initial_count) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_semaphore_create");
}
NexusError *nexus_threads_semaphore_wait(NexusSemaphore *sem) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_wait");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_semaphore_wait");
}
NexusError *nexus_threads_semaphore_post(NexusSemaphore *sem) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_post");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_semaphore_post");
}
void nexus_threads_semaphore_destroy(NexusSemaphore *sem) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_semaphore_destroy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_semaphore_destroy");
}
NexusError *nexus_threads_waitgroup_create(NexusThreadsWaitGroup **out_waitgroup) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_waitgroup_create");
}
NexusError *nexus_threads_waitgroup_add(NexusThreadsWaitGroup *waitgroup, int32 delta) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_add");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_waitgroup_add");
}
NexusError *nexus_threads_waitgroup_done(NexusThreadsWaitGroup *waitgroup) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_done");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_waitgroup_done");
}
NexusError *nexus_threads_waitgroup_wait(NexusThreadsWaitGroup *waitgroup) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_wait");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_waitgroup_wait");
}
boolean nexus_threads_waitgroup_try_wait(NexusThreadsWaitGroup *waitgroup) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_try_wait");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_waitgroup_try_wait");
}
void nexus_threads_waitgroup_destroy(NexusThreadsWaitGroup *waitgroup) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_waitgroup_destroy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_waitgroup_destroy");
}

NexusError *nexus_threads_channel_create(NexusChannel **out_channel, uint_large element_size, uint_large capacity) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_create");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_create");
}
NexusError *nexus_threads_channel_send(NexusChannel *channel, const void *element) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_send");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_send");
}
boolean nexus_threads_channel_try_send(NexusChannel *channel, const void *element) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_try_send");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_try_send");
}
NexusError *nexus_threads_channel_receive(NexusChannel *channel, void *out_element, boolean *out_closed) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_receive");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_receive");
}
boolean nexus_threads_channel_try_receive(NexusChannel *channel, void *out_element) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_try_receive");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_try_receive");
}
void nexus_threads_channel_close(NexusChannel *channel) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_close");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_close");
}
void nexus_threads_channel_destroy(NexusChannel *channel) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_threads_channel_destroy");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("threads_channel_destroy");
}

#endif
