/*
Summary: Logos's own benchmarks, kept out of logos.h so the public API stays free of them.
Layer: above logos.h; for the testbed and other callers that exercise Logos.
See: logos.h
*/
#pragma once

#include "logos.h"

#define LOGOS_BENCHMARKS_LEXER         ((uint32)1)
#define LOGOS_BENCHMARKS_PARSER_PRATT  ((uint32)2)
#define LOGOS_BENCHMARKS_PARSER_EARLEY ((uint32)4)
#define LOGOS_BENCHMARKS_COMPILE       ((uint32)8)
#define LOGOS_BENCHMARKS_ALL           ((uint32)15)

/*
Summary: measures lexing, parsing and lexer compilation and logs one throughput line per workload.
Description: generates deterministic statements (8 MiB for the lexer, 1 MiB for the parser, which stays
  below its token limit), then reports the best of
  repeated runs for the lexer (whole buffer with lex and lex_many, 4 KiB and 64 byte
  chunks), the parser (Pratt expressions inside Earley rules, and Earley rules alone)
  and lexer compilation with 0, 100 and 1000 extra keyword rules. Each line gives
  time, MiB/s, million tokens per second and nanoseconds per token. Build optimized.
Params:
  runtime  the Logos runtime whose Echo context receives the log; borrowed
  selection  a union of the LOGOS_BENCHMARKS_* workloads to run
Errors: NULL runtime asserts. An input the parser does not accept is logged.
Thread-safety: synchronize against runtime destruction.
*/
extern void logos_benchmarks_run(LogosRuntime *runtime, uint32 selection);
