# 0004. Process spawn/wait/communicate: fork+error-pipe, pidfd-based reaping, Linux-only, destroy-never-kills

## Status
Accepted

## Context and Problem Statement
Nexus had no process-spawning capability at all: `n_signals.c` only captures signals sent *to*
this process (SIGINT/SIGTERM), and the prior process/signals/pty implementation from before the
2026-09-12 baseline reset was deleted along with its ADRs (0031-0036) and never re-implemented.
A new consumer (a test-runner tool, SHIELD, currently being scoped separately) needs to spawn its
own test binary as a child, capture its stdout/stderr, and specifically distinguish "the test
failed an assertion" from "the test crashed" (segfault, abort, or an external kill) -- both must
surface as an ordinary result, not an unhandled failure of the spawning call itself.

This record covers `nexus_process_spawn`/`_poll`/`_wait`/`_communicate`/`_signal`/`_destroy`,
added to `libs/nexus/src/n_process.c` and declared in `nexus.h`'s new PROCESS section.

## Decision Drivers
- A crash in the spawned child (signal-terminated) must be distinguishable from a normal exit,
  reported as data, not lost as an unreportable child-process failure.
- Reaping must not race against the child's pid being reused by an unrelated process once
  several children may be in flight (this codebase spawns per-test-case, potentially many).
- Resource release (`destroy`) must never be a hidden lifecycle action -- STYLE_GUIDE's general
  posture against surprising side effects applies directly: freeing a handle must not be able to
  terminate something still running that the caller may have wanted to keep running (e.g. a
  detached long-lived worker).
- `nexus_process_communicate`'s stdin/stdout/stderr handling must not deadlock the classic way
  (writing all of a large stdin before ever reading the child's output, while the child blocks
  writing to a full stdout pipe waiting for a reader).
- Matches the codebase's established error convention (see `NEXUS_ERROR_FILESYSTEM_FACILITY`'s
  own doc comment): a custom per-module error facility exists only for conditions that are not a
  real OS errno; every failure mode this module has (pipe/fork/dup2/chdir/execve/poll/wait/read/
  write/signal) is a genuine errno, so none of it should invent synthetic error codes.

## Considered Options
- **Spawn technique**: A -- `posix_spawn`/`posix_spawn_file_actions`. B -- plain `fork` +
  `execve`, exec failure silently exits the child with no way to distinguish "exec failed" from
  "the program legitimately exits 127". C -- `fork` + `execve` + a close-on-exec error pipe
  (chosen).
- **Reaping**: A -- `waitpid(pid, ...)` polled via a stored pid number. B -- `pidfd_open` +
  `waitid(P_PIDFD, ...)` (chosen).
- **Platform scope**: A -- `NEXUS_PLATFORM_POSIX`, matching `n_signals.c`. B --
  `NEXUS_PLATFORM_LINUX` specifically (chosen).
- **Error reporting**: A -- a dedicated `NexusProcessError` facility with synthetic codes for
  spawn/wait/communicate/signal failures. B -- route every real syscall failure through the
  existing `NEXUS_PROGRAM_ERROR_FROM_ERRNO`/`NEXUS_ERROR_OS_FACILITY` passthrough, including the
  exec-pipe-relayed errno (chosen).
- **PTY support**: in scope now, or deferred. Deferred (chosen) -- out of scope for this record.
- **`destroy` semantics**: A -- releases resources only, never signals or blocks (chosen). B --
  sends SIGKILL to a still-running child before releasing resources ("destroy always cleans up
  fully").

## Decision Outcome
**Spawn (C):** `fork()`, then in the child only async-signal-safe calls (`dup2`/`close`/`chdir`/
`execve`/`_exit`) run before either `execve` succeeds or the child writes its `errno` to a
close-on-exec pipe and calls `_exit(127)`. The parent blocks briefly on that pipe: data read back
means a real setup/exec failure (reported as an ordinary `NexusError`, via
`_nexus_program_errors_from_errno_create` with the relayed errno rather than the live global
`errno`); the pipe closing with zero bytes (because `O_CLOEXEC` closed the write end on a
successful `execve`) means the child is running. This turns "did exec actually succeed" from a
silent, unrecoverable child-side failure into a parent-observable one, at the cost of one extra
pipe and one extra blocking read per spawn (microseconds, not a hot path).

**Reaping (B):** `pidfd_open(child_pid, 0)` gives a pollable fd used everywhere a caller would
otherwise need the raw pid -- `poll()` alongside the stdio pipe fds in `nexus_process_communicate`,
and `waitid(P_PIDFD, pidfd, &info, WEXITED)` to reap. This is race-free regardless of how many
other children exist or how quickly a pid gets reused after some unrelated process exits, which a
plain stored-pid `waitpid` is not.

**Platform scope (B):** gated on `NEXUS_PLATFORM_LINUX` (matching `n_program.c`'s own
errno-translation gate, `#if NEXUS_PLATFORM_LINUX` at `n_program.c:332`), narrower than
`n_signals.c`'s `NEXUS_PLATFORM_POSIX`. `pidfd_open`/`pidfd_send_signal` are Linux-only kernel
facilities (no glibc wrapper exists at this codebase's target glibc either -- both are issued as
raw `syscall()`s); building a portable fallback (plain `waitpid`+stored-pid on non-Linux POSIX)
was not attempted for a first version, since it would mean two different reaping strategies
behind one API with no current second-platform consumer to validate the fallback against.

**Error reporting (B):** no `NexusProcessError` facility was added. Every failure this module can
report -- `pipe`/`fork`/`dup2`/`chdir`/`execve` (relayed), `pidfd_open`, `poll`, `waitid`,
`read`/`write` on the stdio pipes, `pidfd_send_signal` -- is a genuine OS errno, so all of it goes
through the existing `NEXUS_PROGRAM_ERROR_FROM_ERRNO` path. This was a correction made mid-
implementation: the original plan for this module assumed a custom facility (mirroring
`NexusFilesystemError`/`NexusThreadsError`), until `NEXUS_ERROR_FILESYSTEM_FACILITY`'s own doc
comment made explicit that a custom facility exists specifically for conditions that *aren't* a
real errno (e.g. filesystem's regex-compile failure) -- a condition this module never has.

**PTY (deferred):** not implemented here. The motivating consumer's actual need (re-exec a test
binary in a discovery-vs-run-one mode, detect a crash by signal) does not require a pty; adding
one now would be speculative surface with no caller. A pty-spawning variant is a natural, clearly
separable follow-up if a future consumer needs one (e.g. something that needs a child to believe
it has an interactive terminal).

**`destroy` (A):** `nexus_process_destroy` closes the pidfd and any open pipe fds and frees the
handle -- it never sends a signal and never blocks, regardless of whether the child has exited.
A still-running child keeps running, untouched. The accepted consequence: a child destroyed while
running, that is never separately reaped (`poll`/`wait`/`communicate` never called), becomes a
zombie once it does exit, until this process itself exits -- documented directly in the function's
doc comment rather than hidden. A caller that actually wants a running child stopped calls
`nexus_process_signal` first and reaps the result before destroying.

### Pros and Cons of the Options
**Spawn A -- `posix_spawn`**
- Good: a single well-tested libc call, less code in this module.
- Bad: `posix_spawn_file_actions` covers the fd-redirection cases used here, but propagating an
  exec-time failure (e.g. ENOENT) back as a normal return value/errno the same way a direct
  `execve` would is implementation-defined in exactly how much detail survives; the file-actions
  API is also its own indirection layer to learn and wrap for comparatively little benefit over
  the well-understood fork+pipe idiom every mainstream scripting-language runtime already uses.

**Spawn B -- plain fork+execve, no error pipe**
- Bad only: a failed `execve` (bad path, bad permissions) is indistinguishable from the child
  program itself legitimately exiting with status 127 -- rejected outright, not a tradeoff.

**Reaping A -- waitpid(pid, ...)**
- Good: no extra fd, no syscall the wrapper doesn't already need.
- Bad: pid reuse race once other children exist; also would need its own separate blocking
  mechanism to unify with `poll()`-driven stdio draining in `communicate`, rather than folding
  into the same `poll()` call pidfd allows.

**Platform scope A -- POSIX**
- Good: matches `n_signals.c`'s existing precedent, no narrower carve-out to explain.
- Bad: would need a second, different reaping strategy for the non-Linux POSIX branch with no
  current consumer to validate it against -- deferred rather than built speculatively.

**Error reporting A -- custom NexusProcessError facility**
- Good: gives each specific failure mode (spawn vs. wait vs. communicate vs. signal) its own
  distinguishable code a caller could `switch` on.
- Bad: directly contradicts the established convention (a custom facility is for non-errno
  synthetic conditions only) for no real benefit, since every failure here already carries a real,
  specific errno a caller can already inspect (`strerror`, `EPIPE`, `ENOENT`, etc.) -- rejected
  once that convention was actually read rather than assumed.

**destroy B -- SIGKILL a still-running child before releasing resources**
- Good: guarantees no zombie can ever result from a destroy call, simpler mental model of
  "destroyed means gone."
- Bad: makes resource release an implicit lifecycle action with a real external side effect (an
  unrelated program is killed) purely as a consequence of a local handle going out of scope --
  exactly the kind of surprising, hard-to-reason-about behavior a `destroy` function should not
  have. Rejected in favor of making the tradeoff explicit and giving the caller (`nexus_process_
  signal` + reap) the tool to get the SIGKILL-then-cleanup behavior deliberately, when that really
  is what's wanted.

## Consequences
Easier: a crash in a spawned child (segfault, abort, external kill) is reported through the same
`NexusProcessExitStatus` as any other outcome, with no dependency on how the child's own program
distinguished an assert from a panic -- the consumer this module exists for gets exactly the
signal it needs (literally). `communicate()`'s single `poll()` loop over stdin/stdout/stderr/pidfd
avoids the classic write-then-read pipe deadlock without the caller having to reason about it.

Harder: this module now only supports Linux; a consumer that needs the same API on macOS/BSD
needs a second reaping strategy added later, not a trivial recompile. A destroyed-while-running,
never-reaped child can zombie until this process exits -- callers that spawn many short-lived
children and always intend to let them finish (the SHIELD use case) will naturally reap via
`wait`/`communicate` and never hit this; a caller that fires-and-forgets many children without
ever reaping any of them accumulates zombies, which is a real, disclosed cost of the
destroy-never-kills guarantee rather than a bug.

## Framework Hook (optional)
Directly an OODA "Decide": the error-facility choice in particular was actually revised mid-
implementation once `NEXUS_ERROR_FILESYSTEM_FACILITY`'s own doc comment was read rather than
assumed by analogy to `NexusThreadsError` -- Observe/Orient catching a wrong initial Decide before
it shipped, exactly the loop this record exists to make legible afterward. `destroy`-never-kills
is a direct application of STYLE_GUIDE's Black-Box Modules posture: an opaque handle's lifecycle
operations should do only what their name says, so a caller reasoning about "what does releasing
this handle do" never has to also reason about "and does it externally affect something else."

## Related Decisions (optional)
Supersedes, in spirit only (no ID carries forward — the prior corpus was deleted, not amended):
the pre-2026-09-12-reset process/signals ADRs 0031-0036, which covered similar ground (fork+exec
via an error pipe, pidfd, destroy-never-kills, plus a pty spawn variant this record deliberately
excludes). Those IDs are gone and not cited here per the numbering convention's "no cross-
reference to a non-existent ID" rule; this is a fresh decision that happens to reach some of the
same conclusions independently, not a restoration.

## Amendment: interactive stdin/stdout access (added, same record)
`nexus_process_communicate` is a one-shot primitive: write stdin once, drain stdout/stderr until
exit, return. A consumer that reuses one spawned process across several requests (`tools/shield`'s
worker-reuse redesign, `adr/0001` in that repo) needs multiple write/read round-trips against a
*still-running* child, which `communicate` cannot do by construction.

Added `nexus_process_stdin_fd_get`/`nexus_process_stdout_fd_get`: plain accessors for the same
parent-side pipe fds `communicate` already reads/writes internally, for a caller spawned with the
corresponding stream in `NEXUS_PROCESS_STREAM_PIPE` mode. `NexusProcess` keeps owning the fds'
lifecycle (`nexus_process_destroy` still closes them) -- this only grants direct access for as long
as the process is alive; a caller that wants raw access does its own `poll()`/read/write loop
against them (interleaved with ordinary `nexus_process_poll` calls to detect the child exiting),
same as `communicate` does internally. No higher-level line-buffered abstraction was added to
Nexus for this: the narrow need was fd access, not a second protocol layer, and the existing
`poll()`-driven-loop idiom `communicate` already established was left for each caller to apply to
its own framing needs rather than generalized speculatively.

This is judged a clean additive facet of the same "how does a caller communicate with a spawned
process" decision this record already covers -- Status stays Accepted rather than reverting to
Proposed, since nothing above changes: existing `nexus_process_spawn`/`_poll`/`_wait`/
`_communicate`/`_signal`/`_destroy` behavior, signatures, and guarantees are all unchanged.
