#include "../nexus.h"

#include <stb_sprintf.h>

#include <stdio.h>
#include <time.h>

#if NEXUS_PLATFORM_POSIX_API

static NexusTimePrecision _nexus_time_precision_from_nanoseconds(timestamp precision_nanoseconds) {
  if (precision_nanoseconds <= 1) {
    return NTP_NANO_SECOND;
  }

  if (precision_nanoseconds <= NEXUS_NANOSECONDS_PER_MICROSECOND) {
    return NTP_MICRO_SECOND;
  }

  if (precision_nanoseconds <= NEXUS_NANOSECONDS_PER_MILLISECOND) {
    return NTP_MILLI_SECOND;
  }

  return NTP_SECOND;
}

static NexusTime _nexus_time_from_clock(int clock_id, NexusClockOrigin clock_origin) {
  struct timespec current_clock;
  struct timespec clock_resolution;
  timestamp       precision_nanoseconds;

  timestamp current_time_ns;
  NexusTime current_time;

  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_time_from_clock");

  if (clock_getres(clock_id, &clock_resolution) != 0) {
    NEXUS_PROGRAM_PANIC("clock_getres failed");
  }

  precision_nanoseconds = ((uint64)clock_resolution.tv_sec * (uint64)NEXUS_NANOSECONDS_PER_SECOND) + (uint64)clock_resolution.tv_nsec;

  if (clock_gettime(clock_id, &current_clock) != 0) {
    NEXUS_PROGRAM_PANIC("clock_gettime failed");
  }

  current_time_ns = ((uint64)current_clock.tv_sec * (uint64)NEXUS_NANOSECONDS_PER_SECOND) + (uint64)current_clock.tv_nsec;

  current_time.time         = current_time_ns;
  current_time.precision    = _nexus_time_precision_from_nanoseconds(precision_nanoseconds);
  current_time.clock_origin = clock_origin;

  NEXUS_PROGRAM_CONTEXT_POP();
  return current_time;
}

NexusTime nexus_time_get_real(void) {
  return _nexus_time_from_clock(CLOCK_REALTIME, NCO_REAL);
}

NexusTime nexus_time_get_monotonic(void) {
  return _nexus_time_from_clock(CLOCK_MONOTONIC, NCO_MONOTONIC);
}

NexusTime nexus_time_from_timespec(struct timespec timespec_value, NexusClockOrigin clock_origin, NexusTimePrecision precision) {
  NexusTime result;

  result.time         = ((uint64)timespec_value.tv_sec * (uint64)NEXUS_NANOSECONDS_PER_SECOND) + (uint64)timespec_value.tv_nsec;
  result.precision    = precision;
  result.clock_origin = clock_origin;

  return result;
}

#else

NexusTime nexus_time_get_real(void) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_get_real");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("time_get_real");
}

NexusTime nexus_time_get_monotonic(void) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_get_monotonic");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("time_get_monotonic");
}

NexusTime nexus_time_from_timespec(struct timespec timespec_value, NexusClockOrigin clock_origin, NexusTimePrecision precision) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_from_timespec");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("time_from_timespec");
}

#endif

NexusDuration nexus_time_duration_from_nanoseconds(int64 nanoseconds) {
  NexusDuration duration;
  duration.nanoseconds = nanoseconds;
  duration.precision   = NTP_NANO_SECOND;
  return duration;
}

NexusDuration nexus_time_duration_from_microseconds(int64 microseconds) {
  NexusDuration duration;
  duration.nanoseconds = microseconds * (int64)NEXUS_NANOSECONDS_PER_MICROSECOND;
  duration.precision   = NTP_MICRO_SECOND;
  return duration;
}

NexusDuration nexus_time_duration_from_milliseconds(int64 milliseconds) {
  NexusDuration duration;
  duration.nanoseconds = milliseconds * (int64)NEXUS_NANOSECONDS_PER_MILLISECOND;
  duration.precision   = NTP_MILLI_SECOND;
  return duration;
}

NexusDuration nexus_time_duration_from_seconds(int64 seconds) {
  NexusDuration duration;
  duration.nanoseconds = seconds * (int64)NEXUS_NANOSECONDS_PER_SECOND;
  duration.precision   = NTP_SECOND;
  return duration;
}

NexusDuration nexus_time_duration_between(NexusTime start, NexusTime end) {
  NexusDuration result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_duration_between");
  NEXUS_PROGRAM_ASSERT(start.clock_origin == end.clock_origin, "NexusTime clock origins must match for duration_between");

  /* Coarser (larger ordinal) of the two -- a combined value is only as trustworthy as its
     least precise input. */
  result.precision = start.precision > end.precision ? start.precision : end.precision;

  if (end.time >= start.time) {
    result.nanoseconds = (int64)(end.time - start.time);
  } else {
    result.nanoseconds = -((int64)(start.time - end.time));
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}

NexusTime nexus_time_add_duration(NexusTime time, NexusDuration duration) {
  NexusTime result;
  timestamp base_time;
  timestamp offset;
  timestamp new_time;

  result.clock_origin = time.clock_origin;
  result.precision    = (time.precision > duration.precision) ? time.precision : duration.precision;

  base_time = time.time;
  offset    = (timestamp)duration.nanoseconds;
  new_time  = base_time + offset;

  if (duration.nanoseconds > 0 && new_time < base_time) {
    result.time = TIMESTAMP_MAX_VAL;
  } else if (duration.nanoseconds < 0 && new_time > base_time) {
    result.time = 0;
  } else {
    result.time = new_time;
  }

  return result;
}

NexusTime nexus_time_sub_duration(NexusTime time, NexusDuration duration) {
  NexusDuration negated;
  negated.nanoseconds = -duration.nanoseconds;
  negated.precision   = duration.precision;
  return nexus_time_add_duration(time, negated);
}

NexusDuration nexus_time_duration_add(NexusDuration duration1, NexusDuration duration2) {
  NexusDuration result;
  result.nanoseconds = duration1.nanoseconds + duration2.nanoseconds;
  result.precision   = duration1.precision > duration2.precision ? duration1.precision : duration2.precision;
  return result;
}

NexusDuration nexus_time_duration_sub(NexusDuration duration1, NexusDuration duration2) {
  NexusDuration result;
  result.nanoseconds = duration1.nanoseconds - duration2.nanoseconds;
  result.precision   = duration1.precision > duration2.precision ? duration1.precision : duration2.precision;
  return result;
}

NexusDuration nexus_time_duration_mul(NexusDuration duration, int64 scalar) {
  NexusDuration result;
  result.nanoseconds = duration.nanoseconds * scalar;
  result.precision   = duration.precision;
  return result;
}

NexusDuration nexus_time_duration_mul_f(NexusDuration duration, f_real scalar) {
  NexusDuration result;

  /* Truncates precision for durations exceeding roughly 104 days -- f_real's mantissa can't
     represent a nanosecond count that large exactly. */
  result.nanoseconds = (int64)((f_real)duration.nanoseconds * scalar);
  result.precision   = duration.precision;

  return result;
}

NexusDuration nexus_time_duration_div(NexusDuration duration, int64 scalar) {
  NexusDuration result;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_duration_div");
  NEXUS_PROGRAM_ASSERT(scalar != 0, "duration_div: scalar must not be zero");

  result.nanoseconds = duration.nanoseconds / scalar;
  result.precision   = duration.precision;

  NEXUS_PROGRAM_CONTEXT_POP();
  return result;
}

#if NEXUS_PLATFORM_POSIX_API

NexusDateTime nexus_time_to_local_datetime(NexusTime utc_time) {
  NexusDateTime date_time;
  timestamp     seconds;
  uint32        nanoseconds;
  time_t        timer_seconds;
  struct tm     tm_info;
  int           conversion_ok;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_to_local_datetime");
  NEXUS_PROGRAM_ASSERT(utc_time.clock_origin == NCO_REAL, "nexus_time_to_local_datetime requires real clock time");

  date_time.year       = 0;
  date_time.month      = 0;
  date_time.day        = 0;
  date_time.hour       = 0;
  date_time.minute     = 0;
  date_time.second     = 0;
  date_time.nanosecond = 0;
  date_time.precision  = utc_time.precision;
  date_time.is_dst     = -1;

  seconds              = utc_time.time / NEXUS_NANOSECONDS_PER_SECOND;
  nanoseconds          = (uint32)(utc_time.time % NEXUS_NANOSECONDS_PER_SECOND);
  date_time.nanosecond = nanoseconds;

  if (seconds > (timestamp)0xFFFFFFFFFFFFULL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return date_time;
  }

  timer_seconds = (time_t)seconds;
  #if defined(__MINGW32__)
  /* The Windows C runtime's localtime_s takes the arguments the other way round and returns an error code. */
  conversion_ok = (localtime_s(&tm_info, &timer_seconds) == 0);
#else
  conversion_ok = (localtime_r(&timer_seconds, &tm_info) != NULL);
#endif

  if (!conversion_ok) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return date_time;
  }

  date_time.year   = (int32)(tm_info.tm_year + 1900);
  date_time.month  = (uint8)(tm_info.tm_mon + 1);
  date_time.day    = (uint8)tm_info.tm_mday;
  date_time.hour   = (uint8)tm_info.tm_hour;
  date_time.minute = (uint8)tm_info.tm_min;
  date_time.second = (uint8)tm_info.tm_sec;
  date_time.is_dst = (int8)tm_info.tm_isdst;

  NEXUS_PROGRAM_CONTEXT_POP();
  return date_time;
}

NexusTime nexus_time_from_local_datetime(NexusDateTime date_time) {
  NexusTime utc_time;
  struct tm tm_info;
  time_t    timer_seconds;

  utc_time.time         = 0;
  utc_time.clock_origin = NCO_REAL;
  utc_time.precision    = date_time.precision;

  tm_info.tm_sec   = (int)date_time.second;
  tm_info.tm_min   = (int)date_time.minute;
  tm_info.tm_hour  = (int)date_time.hour;
  tm_info.tm_mday  = (int)date_time.day;
  tm_info.tm_mon   = (int)date_time.month - 1;
  tm_info.tm_year  = (int)date_time.year - 1900;
  tm_info.tm_wday  = 0;
  tm_info.tm_yday  = 0;
  tm_info.tm_isdst = -1;

  timer_seconds = mktime(&tm_info);
  if (timer_seconds == (time_t)-1) {
    return utc_time;
  }

  utc_time.time = ((uint64)timer_seconds * NEXUS_NANOSECONDS_PER_SECOND) + (uint64)date_time.nanosecond;
  return utc_time;
}

#else

NexusDateTime nexus_time_to_local_datetime(NexusTime utc_time) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_to_local_datetime");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("time_to_local_datetime");
}

NexusTime nexus_time_from_local_datetime(NexusDateTime date_time) {
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_from_local_datetime");
  NEXUS_PLATFORM_UNSUPPORTED_RAISE("time_from_local_datetime");
}

#endif

boolean nexus_time_datetime_parse(const char *string, NexusDateTime *out_date_time) {
  int          parsed_fields;
  int          year;
  unsigned int month;
  unsigned int day;
  unsigned int hour;
  unsigned int minute;
  unsigned int second;
  unsigned int nanosecond;

  if (string == NULL || out_date_time == NULL) {
    return FALSE;
  }

  nanosecond = 0;

  /* parsed_fields < 6 rejects any input that did not match all required fields, which is the
     failure mode cert-err34-c warns about here. */
  parsed_fields = sscanf(string, "%d-%u-%u %u:%u:%u.%u", &year, &month, &day, &hour, &minute, &second, &nanosecond); /* NOLINT(cert-err34-c) */
  if (parsed_fields < 6) {
    return FALSE;
  }

  out_date_time->year       = (int32)year;
  out_date_time->month      = (uint8)month;
  out_date_time->day        = (uint8)day;
  out_date_time->hour       = (uint8)hour;
  out_date_time->minute     = (uint8)minute;
  out_date_time->second     = (uint8)second;
  out_date_time->nanosecond = nanosecond;
  out_date_time->precision  = NTP_NANO_SECOND;
  out_date_time->is_dst     = -1;
  return TRUE;
}

boolean nexus_time_from_local_datetime_string(const char *string, NexusTime *out_time) {
  NexusDateTime date_time;

  if (string == NULL || out_time == NULL) {
    return FALSE;
  }

  if (nexus_time_datetime_parse(string, &date_time) == FALSE) {
    return FALSE;
  }

  *out_time = nexus_time_from_local_datetime(date_time);
  return TRUE;
}

static uint_large _nexus_time_duration_format_f_real_nanoseconds(char *string, uint_large max_string_length, f_real nanoseconds) {
  f_real     absolute_nanoseconds;
  f_real     scaled_value;
  uint_large written;

  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_time_duration_format_f_real_nanoseconds");
  NEXUS_PROGRAM_ASSERT(string != NULL, "duration format: string must not be NULL");
  NEXUS_PROGRAM_ASSERT(max_string_length > 0, "duration format: max_string_length must be > 0");

  absolute_nanoseconds = nanoseconds >= (f_real)0 ? nanoseconds : -nanoseconds;

  if (absolute_nanoseconds < (f_real)NEXUS_NANOSECONDS_PER_MICROSECOND) {
    written = (uint_large)stbsp_snprintf(string, (int)max_string_length, "%.3f ns", nanoseconds);
  } else if (absolute_nanoseconds < (f_real)NEXUS_NANOSECONDS_PER_MILLISECOND) {
    scaled_value = nanoseconds / (f_real)NEXUS_NANOSECONDS_PER_MICROSECOND;
    written      = (uint_large)stbsp_snprintf(string, (int)max_string_length, "%.3f us", scaled_value);
  } else if (absolute_nanoseconds < (f_real)NEXUS_NANOSECONDS_PER_SECOND) {
    scaled_value = nanoseconds / (f_real)NEXUS_NANOSECONDS_PER_MILLISECOND;
    written      = (uint_large)stbsp_snprintf(string, (int)max_string_length, "%.3f ms", scaled_value);
  } else {
    scaled_value = nanoseconds / (f_real)NEXUS_NANOSECONDS_PER_SECOND;
    written      = (uint_large)stbsp_snprintf(string, (int)max_string_length, "%.3f s", scaled_value);
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return written;
}

uint_large nexus_time_duration_format(char *string, uint_large max_string_length, NexusDuration duration) {
  return _nexus_time_duration_format_f_real_nanoseconds(string, max_string_length, (f_real)duration.nanoseconds);
}

uint_large nexus_time_datetime_format(char *string, uint_large max_string_length, NexusDateTime date_time) {
  uint_large written;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_datetime_format");
  NEXUS_PROGRAM_ASSERT(string != NULL, "datetime format: string must not be NULL");
  NEXUS_PROGRAM_ASSERT(max_string_length > 0, "datetime format: max_string_length must be > 0");

  written = (uint_large)stbsp_snprintf(string, (int)max_string_length, "%04d-%02d-%02d %02d:%02d:%02d.%09u", (int)date_time.year,
                                       (unsigned int)date_time.month, (unsigned int)date_time.day, (unsigned int)date_time.hour,
                                       (unsigned int)date_time.minute, (unsigned int)date_time.second, (unsigned int)date_time.nanosecond);

  NEXUS_PROGRAM_CONTEXT_POP();
  return written;
}
