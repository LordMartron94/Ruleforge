# 0011. Explicit file sizing for memory maps

## Status
Proposed

## Context and Problem Statement
`nexus_memory_map` accepted arbitrary byte lengths but rounded them to whole
pages and returned that rounded value as `out_mapped_size`. This mixed the
OS address-space extent with the bytes a caller could safely use. A file
ending inside its final page does not acquire additional persistable bytes
because it is mapped, and accessing whole pages beyond EOF can cause SIGBUS.

The function also accepted file-backed requests without checking the file's
length. `NFM_READ_BINARY_PLUS` preserves an existing file's size;
`NFM_WRITE_BINARY_PLUS` truncates it to zero. Neither prepares a larger file
mapping. Anonymous memory-backed files could be initially sized at creation,
but Nexus had no public operation for subsequently querying or resizing an
open file.

## Decision Drivers
- Keep usable bytes distinct from the kernel's page-granularity accounting.
- Make file mutation explicit and independent of creating an address-space view.
- Preserve the existing mapping signature and stack caller.
- Keep file-size operations behind Nexus's filesystem boundary.
- Reject an already-short file before exposing a mapping that can fault on use.

## Considered Options
- Keep the mapping signature, return the requested length, reject requests
  beyond EOF, and provide explicit file-size query and resize operations.
- Grow writable files automatically as part of mapping them.
- Replace the mapping outputs with an object carrying separate usable and
  page-rounded sizes.

## Decision Outcome
Mr. Hoorn selected explicit resizing with the existing signature.

`nexus_memory_map` maps exactly the requested byte length from file offset
zero and returns that length unchanged in `out_mapped_size`. Both output
values are cleared on returned failure. The kernel manages its page-rounded
extent internally. Bytes beyond the requested length remain outside Nexus's
usable-range contract for anonymous and file-backed mappings alike.

For a file-backed request, Nexus queries the OS-visible length and returns
`NEXUS_FILESYSTEM_ERROR_FILE_TOO_SMALL` when the request exceeds it. Mapping
does not flush or resize the file. A caller with pending buffered output
must flush it before mapping.

`nexus_filesystem_file_size_get` queries the descriptor without changing the
stream position or flushing pending output. `nexus_filesystem_file_resize`
synchronizes buffered I/O, then grows or shrinks a writable file to an exact
byte length, including zero. Growth preserves the prefix and supplies zero
bytes in the extension. Shrink discards the removed data. Unrepresentable
file lengths return `NEXUS_FILESYSTEM_ERROR_FILE_SIZE_OUT_OF_RANGE`, including
at anonymous-file creation; conversions to `off_t` must not silently wrap.

Unmap and protect require page-aligned starting addresses. Their lengths
need not be page-aligned: each operation affects every page intersecting the
requested range. Callers can pass the successful mapping's returned length
unchanged. Stack allocation continues to round its own usable region because
its guard page is an explicit page-granularity requirement.

### Pros and Cons of the Options
**Explicit resize and existing signature**
- Keeps file mutation visible and usable without creating a mapping.
- Preserves call sites and avoids allocating mapping metadata.
- Requires an explicit preparation step when a file is too short.
- Changes the meaning of `out_mapped_size` from rounded extent to usable bytes;
  external callers relying on padding must request those bytes explicitly.

**Automatic growth during mapping**
- Combines preparation and mapping in a single call.
- Implicitly mutates the backing file and cannot infer whether a caller
  intended to grow it or supplied an incorrect length.
- Leaves a changed file if growth succeeds and the later mmap fails.

**Mapping object with usable and rounded sizes**
- Represents both quantities explicitly and could carry future view metadata.
- Changes every caller and adds state that unmap/protect do not need for the
  current offset-zero, ordinary-page API.

## Consequences
The ordinary workflow is open, explicitly resize if necessary, then map.
Read-only and private mappings use the same EOF validation. Shared writes
can update file contents; neither unmapping nor resizing promises disk
durability or physical-storage reservation.

File-size validation is a snapshot. Callers must coordinate concurrent
resizing and stop using removed ranges before shrinking a mapped file.
Another process can still truncate after validation and cause SIGBUS; this
API does not introduce locking or file seals. Device mappings whose usable
extent is not represented by the descriptor's reported size are outside the
file-mapping contract.

## Related Decisions
Refines the file-backed mapping contract introduced by
[0006-anonymous-files-and-file-backed-memory-map](./0006-anonymous-files-and-file-backed-memory-map.md).

## Sources
- [Linux mmap and munmap](https://man7.org/linux/man-pages/man2/mmap.2.html)
- [Linux mprotect](https://man7.org/linux/man-pages/man2/mprotect.2.html)
- [Linux ftruncate](https://man7.org/linux/man-pages/man2/ftruncate.2.html)
- [Linux fstat](https://man7.org/linux/man-pages/man2/stat.2.html)
