#include "../nexus.h"

NexusError *nexus_stack_create(uint_large usable_size, NexusStack *out_stack) {
  uint_large  page_size;
  uint_large  aligned_usable_size;
  uint_large  mapped_size;
  uint_large  mapped_size_internal;
  void       *base;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_stack_create");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(out_stack != NULL, "stack_create: out_stack must not be NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(usable_size > 0, "stack_create: usable_size must be > 0");

  page_size = nexus_memory_page_size_get();
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(usable_size <= UINT_LARGE_MAX_VAL - (page_size - 1), "stack_create: usable_size overflows page alignment");
  aligned_usable_size = ((usable_size + page_size - 1) / page_size) * page_size;
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(aligned_usable_size <= UINT_LARGE_MAX_VAL - page_size,
                                    "stack_create: usable_size leaves no representable guard page");
  mapped_size = aligned_usable_size + page_size; /* one extra page for the guard. */

  error = nexus_memory_map(NULL, mapped_size, NMP_READ_WRITE, NMS_PRIVATE, &base, &mapped_size_internal);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  /* Guard page at the low-address end: every architecture this runs on grows its stack
     downward, so that's the direction an overflow actually writes into. */
  error = nexus_memory_protect(base, page_size, NMP_NONE);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    (void)nexus_memory_unmap(base, mapped_size);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  out_stack->base        = base;
  out_stack->usable      = (byte *)base + page_size;
  out_stack->usable_size = aligned_usable_size;
  out_stack->mapped_size = mapped_size;

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

void nexus_stack_destroy(NexusStack *stack) {
  if (stack == NULL || stack->base == NULL) {
    return;
  }

  /* Unmapped outright, not quarantined-and-poisoned like the heap allocator's own debug mode
     -- any later access (overflow or use-after-destroy) is an immediate SEGV either way,
     which is the point, not a gap to fill in with software checking. */
  (void)nexus_memory_unmap(stack->base, stack->mapped_size);

  stack->base        = NULL;
  stack->usable      = NULL;
  stack->usable_size = 0;
  stack->mapped_size = 0;
}
