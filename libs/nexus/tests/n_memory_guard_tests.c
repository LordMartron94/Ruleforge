#include <nexus.h>

/* UBSan regression: validating the prefix of a valid debug allocation used to
   add a wrapped unsigned offset to its user pointer, even without corruption. */
int main(void) {
  static const uint_large sizes[] = {1U, 17U, 4096U};
  const uint_large        growth  = 31U;
  byte                   *block;
  byte                    expected;
  uint_large              size_index;
  uint_large              index;

  for (size_index = 0; size_index < NEXUS_SIZEOF(sizes) / NEXUS_SIZEOF(sizes[0]); size_index++) {
    block = (byte *)nexus_memory_allocate(sizes[size_index]);

    for (index = 0; index < sizes[size_index]; index++)
      block[index] = (byte)index;

    block = (byte *)nexus_memory_reallocate(block, sizes[size_index] + growth);

    for (index = 0; index < sizes[size_index]; index++) {
      expected = (byte)index;
      NEXUS_PROGRAM_ASSERT_ALWAYS(block[index] == expected, "Memory guard regression: growth changed byte %llu", index);
    }

    block = (byte *)nexus_memory_reallocate(block, 1U);
    NEXUS_PROGRAM_ASSERT_ALWAYS(block[0] == 0U, "Memory guard regression: shrink changed the retained byte");
    nexus_memory_deallocate(block);
  }

  return 0;
}
