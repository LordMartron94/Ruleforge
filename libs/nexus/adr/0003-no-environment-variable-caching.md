# 0003. Don't cache environment variable reads

## Status
Accepted

## Decision
In the context of `nexus_environment_variable_get`, facing the fact that
caching a read value would be strictly faster for a caller who reads the same
variable repeatedly, we decided against any caching in Nexus itself, to
achieve correctness under the environment changing between calls — the
process's own environment can be modified at any time (by
`nexus_environment_variable_set`/`unset`, or by anything else in or around
the process), so a value cached at the wrong moment would silently go stale
with no way for Nexus to know — accepting that a caller who repeatedly reads
the same variable and wants to avoid the repeated lookup must build and own
that cache themselves, with whatever invalidation policy actually fits their
use of it. Consistent with Nexus's role as a foundational library rather than
a framework (see the filesystem subsystem's equivalent stance on
cross-filesystem move fallbacks): it exposes the OS's actual, current state
on every call rather than silently trading correctness for speed on the
caller's behalf.
