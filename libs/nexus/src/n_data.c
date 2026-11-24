#include "../nexus.h"

/* ---------------------------------------------------------------------------- */
/* COMMON STORAGE                                                               */
/* ---------------------------------------------------------------------------- */

typedef union NDataMaximumAlignment {
  long double real;
  void       *pointer;
  uint64      integer;
} NDataMaximumAlignment;

typedef struct NDataBuffer {
  byte      *data;
  uint_large element_size;
  uint_large stride;
  uint_large length;
  uint_large capacity;
} NDataBuffer;

struct NexusDataList {
  NDataBuffer buffer;
};

struct NexusDataStack {
  NDataBuffer buffer;
};

struct NexusDataQueue {
  NDataBuffer buffer;
  uint_large  head;
};

struct NexusDataRingBuffer {
  NDataBuffer buffer;
  uint_large  head;
};

struct NexusDataPriorityQueue {
  NDataBuffer               buffer;
  NexusDataCompareFunction *compare;
  void                     *user_data;
};

static uint_large n_data_align_up(uint_large value, uint_large alignment) {
  uint_large remainder;

  remainder = value % alignment;
  if (remainder == 0) {
    return value;
  }

  NEXUS_PROGRAM_PRECONDITION_ALWAYS(value <= UINT_LARGE_MAX_VAL - (alignment - remainder), "data size alignment overflow");
  return value + (alignment - remainder);
}

/* Elements are tightly packed (stride == element_size) so storage can be handed to APIs that expect a plain C array. A C type's size is
   always a multiple of its alignment, so with a maximally aligned base every typed element stays correctly aligned without padding. */
static uint_large n_data_element_stride_get(uint_large element_size) {
  NEXUS_PROGRAM_PRECONDITION(element_size > 0, "data element_size must be greater than zero");
  return element_size;
}

static uint_large n_data_allocation_size_get(uint_large stride, uint_large capacity) {
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(capacity == 0 || stride <= UINT_LARGE_MAX_VAL / capacity, "data allocation size overflow");
  return stride * capacity;
}

static void n_data_bytes_move(void *destination, const void *source, uint_large size) {
  byte       *destination_bytes;
  const byte *source_bytes;
  uint_large  index;

  destination_bytes = (byte *)destination;
  source_bytes      = (const byte *)source;

  if (destination_bytes == source_bytes || size == 0) {
    return;
  }

  if (destination_bytes < source_bytes || destination_bytes >= source_bytes + size) {
    for (index = 0; index < size; index++) {
      destination_bytes[index] = source_bytes[index];
    }
  } else {
    index = size;
    while (index > 0) {
      index--;
      destination_bytes[index] = source_bytes[index];
    }
  }
}

static void n_data_elements_swap(byte *left, byte *right, uint_large element_size) {
  uint_large index;

  if (left == right) {
    return;
  }

  for (index = 0; index < element_size; index++) {
    byte temporary;

    temporary    = left[index];
    left[index]  = right[index];
    right[index] = temporary;
  }
}

static void n_data_buffer_initialize(NDataBuffer *buffer, uint_large element_size, uint_large initial_capacity) {
  buffer->data         = NULL;
  buffer->element_size = element_size;
  buffer->stride       = n_data_element_stride_get(element_size);
  buffer->length       = 0;
  buffer->capacity     = 0;

  if (initial_capacity > 0) {
    buffer->data     = (byte *)nexus_memory_allocate(n_data_allocation_size_get(buffer->stride, initial_capacity));
    buffer->capacity = initial_capacity;
  }
}

static void n_data_buffer_destroy(NDataBuffer *buffer) {
  if (buffer->data != NULL) {
    nexus_memory_deallocate(buffer->data);
  }

  buffer->data     = NULL;
  buffer->length   = 0;
  buffer->capacity = 0;
}

static void n_data_buffer_reserve(NDataBuffer *buffer, uint_large minimum_capacity) {
  uint_large new_capacity;
  uint_large allocation_size;

  if (minimum_capacity <= buffer->capacity) {
    return;
  }

  new_capacity = buffer->capacity == 0 ? 4U : buffer->capacity;
  while (new_capacity < minimum_capacity) {
    if (new_capacity > UINT_LARGE_MAX_VAL / 2U) {
      new_capacity = minimum_capacity;
      break;
    }
    new_capacity *= 2U;
  }

  allocation_size = n_data_allocation_size_get(buffer->stride, new_capacity);
  if (buffer->data == NULL) {
    buffer->data = (byte *)nexus_memory_allocate(allocation_size);
  } else {
    buffer->data = (byte *)nexus_memory_reallocate(buffer->data, allocation_size);
  }
  buffer->capacity = new_capacity;
}

static byte *n_data_buffer_at(NDataBuffer *buffer, uint_large index) {
  return buffer->data + (index * buffer->stride);
}

static const byte *n_data_buffer_at_const(const NDataBuffer *buffer, uint_large index) {
  return buffer->data + (index * buffer->stride);
}

/* reverses the slots in [begin, end) */
static void n_data_buffer_reverse_range(NDataBuffer *buffer, uint_large begin, uint_large end) {
  while (begin + 1U < end) {
    end--;
    n_data_elements_swap(n_data_buffer_at(buffer, begin), n_data_buffer_at(buffer, end), buffer->element_size);
    begin++;
  }
}

/* rotates a circular buffer in place so the slot at *head moves to slot 0, then resets *head; three reversals, no scratch storage */
static byte *n_data_buffer_linearize(NDataBuffer *buffer, uint_large *head) {
  if (buffer->data == NULL) {
    return NULL;
  }

  if (*head != 0) {
    n_data_buffer_reverse_range(buffer, 0, *head);
    n_data_buffer_reverse_range(buffer, *head, buffer->capacity);
    n_data_buffer_reverse_range(buffer, 0, buffer->capacity);
    *head = 0;
  }

  return buffer->data;
}

/* ---------------------------------------------------------------------------- */
/* ARRAY                                                                        */
/* ---------------------------------------------------------------------------- */

NexusDataArray nexus_data_array_view_create(void *data, uint_large element_size, uint_large capacity) {
  NexusDataArray array;

  NEXUS_PROGRAM_PRECONDITION(element_size > 0, "data_array_view_create: element_size must be greater than zero");
  NEXUS_PROGRAM_PRECONDITION(data != NULL || capacity == 0, "data_array_view_create: data is NULL for nonzero capacity");

  array.data         = (byte *)data;
  array.element_size = element_size;
  array.capacity     = capacity;
  return array;
}

NexusDataArray nexus_data_array_subview(NexusDataArray array, uint_large offset, uint_large count) {
  NEXUS_PROGRAM_PRECONDITION(offset <= array.capacity, "data_array_subview: offset is outside capacity");
  NEXUS_PROGRAM_PRECONDITION(count <= array.capacity - offset, "data_array_subview: count is outside capacity");

  return nexus_data_array_view_create(array.data == NULL ? NULL : array.data + (offset * array.element_size), array.element_size, count);
}

void *nexus_data_array_at(NexusDataArray *array, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_at: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->data != NULL, "data_array_at: array data is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < array->capacity, "data_array_at: index is outside capacity");

  return array->data + (index * array->element_size);
}

const void *nexus_data_array_at_const(const NexusDataArray *array, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_at_const: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->data != NULL, "data_array_at_const: array data is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < array->capacity, "data_array_at_const: index is outside capacity");

  return array->data + (index * array->element_size);
}

void *nexus_data_array_data_get(NexusDataArray *array) {
  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_data_get: array is NULL");
  return array->data;
}

const void *nexus_data_array_data_get_const(const NexusDataArray *array) {
  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_data_get_const: array is NULL");
  return array->data;
}

void nexus_data_array_set(NexusDataArray *array, uint_large index, const void *element) {
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_array_set: element is NULL");
  nexus_memory_bytes_copy(element, nexus_data_array_at(array, index), array->element_size);
}

void nexus_data_array_get(const NexusDataArray *array, uint_large index, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(out_element != NULL, "data_array_get: out_element is NULL");
  nexus_memory_bytes_copy(nexus_data_array_at_const(array, index), out_element, array->element_size);
}

void nexus_data_array_set_pointer(NexusDataArray *array, uint_large index, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_set_pointer: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->element_size == NEXUS_SIZEOF(void *), "data_array_set_pointer: element_size is not pointer-sized");
  nexus_data_array_set(array, index, (const void *)&pointer);
}

void *nexus_data_array_pointer_at(const NexusDataArray *array, uint_large index) {
  void *pointer;

  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_pointer_at: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->element_size == NEXUS_SIZEOF(void *), "data_array_pointer_at: element_size is not pointer-sized");
  nexus_data_array_get(array, index, (void *)&pointer);
  return pointer;
}

void nexus_data_array_reverse(NexusDataArray *array) {
  uint_large left;
  uint_large right;

  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_reverse: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->data != NULL || array->capacity == 0, "data_array_reverse: array data is NULL");

  left  = 0;
  right = array->capacity;
  while (left < right) {
    right--;
    if (left >= right) {
      break;
    }

    n_data_elements_swap(array->data + (left * array->element_size), array->data + (right * array->element_size), array->element_size);
    left++;
  }
}

typedef struct NDataSortFrame {
  uint_large first;
  uint_large count;
  uint32     depth;
} NDataSortFrame;

static void n_data_array_insertion_sort(NexusDataArray *array, uint_large first, uint_large count, NexusDataCompareFunction *compare,
                                        void *user_data) {
  uint_large index;

  for (index = first + 1U; index < first + count; index++) {
    uint_large current;

    current = index;
    while (current > first &&
           compare(array->data + (current * array->element_size), array->data + ((current - 1U) * array->element_size), user_data) < 0) {
      n_data_elements_swap(array->data + (current * array->element_size), array->data + ((current - 1U) * array->element_size), array->element_size);
      current--;
    }
  }
}

static void n_data_array_heap_sift_down(NexusDataArray *array, uint_large first, uint_large root, uint_large count, NexusDataCompareFunction *compare,
                                        void *user_data) {
  if (count < 2U) {
    return;
  }

  while (root <= (count - 2U) / 2U) {
    uint_large child;

    child = (root * 2U) + 1U;
    if (child + 1U < count &&
        compare(array->data + ((first + child) * array->element_size), array->data + ((first + child + 1U) * array->element_size), user_data) < 0) {
      child++;
    }

    if (compare(array->data + ((first + root) * array->element_size), array->data + ((first + child) * array->element_size), user_data) >= 0) {
      return;
    }

    n_data_elements_swap(array->data + ((first + root) * array->element_size), array->data + ((first + child) * array->element_size),
                         array->element_size);
    root = child;
  }
}

static void n_data_array_heap_sort(NexusDataArray *array, uint_large first, uint_large count, NexusDataCompareFunction *compare, void *user_data) {
  uint_large index;

  if (count < 2U) {
    return;
  }

  index = count / 2U;
  while (index > 0) {
    index--;
    n_data_array_heap_sift_down(array, first, index, count, compare, user_data);
  }

  index = count;
  while (index > 1U) {
    index--;
    n_data_elements_swap(array->data + (first * array->element_size), array->data + ((first + index) * array->element_size), array->element_size);
    n_data_array_heap_sift_down(array, first, 0, index, compare, user_data);
  }
}

static uint_large n_data_array_partition(NexusDataArray *array, uint_large first, uint_large count, NexusDataCompareFunction *compare,
                                         void *user_data) {
  uint_large middle;
  uint_large last;
  uint_large store;
  uint_large scan;

  middle = first + (count / 2U);
  last   = first + count - 1U;

  if (compare(array->data + (middle * array->element_size), array->data + (first * array->element_size), user_data) < 0) {
    n_data_elements_swap(array->data + (middle * array->element_size), array->data + (first * array->element_size), array->element_size);
  }
  if (compare(array->data + (last * array->element_size), array->data + (first * array->element_size), user_data) < 0) {
    n_data_elements_swap(array->data + (last * array->element_size), array->data + (first * array->element_size), array->element_size);
  }
  if (compare(array->data + (last * array->element_size), array->data + (middle * array->element_size), user_data) < 0) {
    n_data_elements_swap(array->data + (last * array->element_size), array->data + (middle * array->element_size), array->element_size);
  }

  n_data_elements_swap(array->data + (middle * array->element_size), array->data + (last * array->element_size), array->element_size);
  store = first;

  for (scan = first; scan < last; scan++) {
    if (compare(array->data + (scan * array->element_size), array->data + (last * array->element_size), user_data) < 0) {
      n_data_elements_swap(array->data + (scan * array->element_size), array->data + (store * array->element_size), array->element_size);
      store++;
    }
  }

  n_data_elements_swap(array->data + (store * array->element_size), array->data + (last * array->element_size), array->element_size);
  return store;
}

void nexus_data_array_sort(NexusDataArray *array, NexusDataCompareFunction *compare, void *user_data) {
  NDataSortFrame frames[128];
  uint32         frame_count;
  uint32         depth;
  uint_large     size;

  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_sort: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(compare != NULL, "data_array_sort: compare is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->data != NULL || array->capacity == 0, "data_array_sort: array data is NULL");

  if (array->capacity < 2U) {
    return;
  }

  depth = 0;
  size  = array->capacity;
  while (size > 1U) {
    depth++;
    size >>= 1U;
  }
  depth *= 2U;

  frames[0].first = 0;
  frames[0].count = array->capacity;
  frames[0].depth = depth;
  frame_count     = 1U;

  while (frame_count > 0U) {
    NDataSortFrame frame;

    frame_count--;
    frame = frames[frame_count];

    while (frame.count > 16U) {
      uint_large pivot;
      uint_large left_count;
      uint_large right_first;
      uint_large right_count;

      if (frame.depth == 0U) {
        n_data_array_heap_sort(array, frame.first, frame.count, compare, user_data);
        frame.count = 0;
        break;
      }
      frame.depth--;

      pivot       = n_data_array_partition(array, frame.first, frame.count, compare, user_data);
      left_count  = pivot - frame.first;
      right_first = pivot + 1U;
      right_count = (frame.first + frame.count) - right_first;

      NEXUS_PROGRAM_INVARIANT(frame_count < NEXUS_ARRAY_SIZE_ELEMENTS(frames), "data_array_sort: partition stack overflow");

      if (left_count < right_count) {
        if (right_count > 1U) {
          frames[frame_count].first = right_first;
          frames[frame_count].count = right_count;
          frames[frame_count].depth = frame.depth;
          frame_count++;
        }
        frame.count = left_count;
      } else {
        if (left_count > 1U) {
          frames[frame_count].first = frame.first;
          frames[frame_count].count = left_count;
          frames[frame_count].depth = frame.depth;
          frame_count++;
        }
        frame.first = right_first;
        frame.count = right_count;
      }
    }

    if (frame.count > 1U) {
      n_data_array_insertion_sort(array, frame.first, frame.count, compare, user_data);
    }
  }
}

boolean nexus_data_array_binary_search(const NexusDataArray *array, const void *key, NexusDataCompareFunction *compare, void *user_data,
                                       uint_large *out_index) {
  uint_large first;
  uint_large count;

  NEXUS_PROGRAM_PRECONDITION(array != NULL, "data_array_binary_search: array is NULL");
  NEXUS_PROGRAM_PRECONDITION(key != NULL, "data_array_binary_search: key is NULL");
  NEXUS_PROGRAM_PRECONDITION(compare != NULL, "data_array_binary_search: compare is NULL");
  NEXUS_PROGRAM_PRECONDITION(array->data != NULL || array->capacity == 0, "data_array_binary_search: array data is NULL");

  first = 0;
  count = array->capacity;
  while (count > 0U) {
    uint_large step;
    uint_large index;
    int32      order;

    step  = count / 2U;
    index = first + step;
    order = compare(array->data + (index * array->element_size), key, user_data);

    if (order < 0) {
      first = index + 1U;
      count -= step + 1U;
    } else if (order > 0) {
      count = step;
    } else {
      if (out_index != NULL) {
        *out_index = index;
      }
      return TRUE;
    }
  }

  if (out_index != NULL) {
    *out_index = first;
  }
  return FALSE;
}

/* ---------------------------------------------------------------------------- */
/* BITSET                                                                       */
/* ---------------------------------------------------------------------------- */

/* Mask of the bits in word word_index that belong to the set. Every word but the last is fully live; the last may be partial, and its
   bits past bit_count are ignored so a caller that left them dirty (or a union from a wider source) cannot change a query result. */
static uint32 n_data_bitset_word_mask(const NexusDataBitset *bitset, uint_large word_index) {
  uint_large live_bits_in_last_word;

  if (word_index + 1U < NEXUS_DATA_BITSET_WORD_COUNT(bitset->bit_count)) {
    return 0xFFFFFFFFU;
  }

  live_bits_in_last_word = bitset->bit_count % NEXUS_DATA_BITSET_WORD_BITS;
  if (live_bits_in_last_word == 0U) {
    return 0xFFFFFFFFU;
  }

  return ((uint32)1U << live_bits_in_last_word) - 1U;
}

NexusDataBitset nexus_data_bitset_view_create(uint32 *words, uint_large bit_count) {
  NexusDataBitset bitset;

  NEXUS_PROGRAM_PRECONDITION(words != NULL || bit_count == 0, "data_bitset_view_create: words is NULL for nonzero bit_count");

  bitset.words     = words;
  bitset.bit_count = bit_count;
  return bitset;
}

void nexus_data_bitset_clear_all(NexusDataBitset *bitset) {
  uint_large i;
  uint_large word_count;

  NEXUS_PROGRAM_PRECONDITION(bitset != NULL, "data_bitset_clear_all: bitset is NULL");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(bitset->bit_count);
  for (i = 0; i < word_count; i++) {
    bitset->words[i] = 0U;
  }
}

uint_large nexus_data_bitset_count(const NexusDataBitset *bitset) {
  uint_large i;
  uint_large word_count;
  uint_large count;

  NEXUS_PROGRAM_PRECONDITION(bitset != NULL, "data_bitset_count: bitset is NULL");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(bitset->bit_count);
  count      = 0;
  for (i = 0; i < word_count; i++) {
    count += nexus_bits_uint32_popcount(bitset->words[i] & n_data_bitset_word_mask(bitset, i));
  }

  return count;
}

boolean nexus_data_bitset_is_subset(const NexusDataBitset *subset, const NexusDataBitset *superset) {
  uint_large i;
  uint_large word_count;

  NEXUS_PROGRAM_PRECONDITION(subset != NULL, "data_bitset_is_subset: subset is NULL");
  NEXUS_PROGRAM_PRECONDITION(superset != NULL, "data_bitset_is_subset: superset is NULL");
  NEXUS_PROGRAM_PRECONDITION(subset->bit_count == superset->bit_count, "data_bitset_is_subset: bit_count differs");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(subset->bit_count);
  for (i = 0; i < word_count; i++) {
    if ((subset->words[i] & ~superset->words[i] & n_data_bitset_word_mask(subset, i)) != 0U) {
      return FALSE;
    }
  }

  return TRUE;
}

boolean nexus_data_bitset_equals(const NexusDataBitset *left, const NexusDataBitset *right) {
  uint_large i;
  uint_large word_count;

  NEXUS_PROGRAM_PRECONDITION(left != NULL, "data_bitset_equals: left is NULL");
  NEXUS_PROGRAM_PRECONDITION(right != NULL, "data_bitset_equals: right is NULL");
  NEXUS_PROGRAM_PRECONDITION(left->bit_count == right->bit_count, "data_bitset_equals: bit_count differs");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(left->bit_count);
  for (i = 0; i < word_count; i++) {
    if (((left->words[i] ^ right->words[i]) & n_data_bitset_word_mask(left, i)) != 0U) {
      return FALSE;
    }
  }

  return TRUE;
}

void nexus_data_bitset_union_into(NexusDataBitset *destination, const NexusDataBitset *source) {
  uint_large i;
  uint_large word_count;

  NEXUS_PROGRAM_PRECONDITION(destination != NULL, "data_bitset_union_into: destination is NULL");
  NEXUS_PROGRAM_PRECONDITION(source != NULL, "data_bitset_union_into: source is NULL");
  NEXUS_PROGRAM_PRECONDITION(destination->bit_count == source->bit_count, "data_bitset_union_into: bit_count differs");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(destination->bit_count);
  for (i = 0; i < word_count; i++) {
    destination->words[i] |= source->words[i] & n_data_bitset_word_mask(source, i);
  }
}

void nexus_data_bitset_intersect_into(NexusDataBitset *destination, const NexusDataBitset *source) {
  uint_large i;
  uint_large word_count;

  NEXUS_PROGRAM_PRECONDITION(destination != NULL, "data_bitset_intersect_into: destination is NULL");
  NEXUS_PROGRAM_PRECONDITION(source != NULL, "data_bitset_intersect_into: source is NULL");
  NEXUS_PROGRAM_PRECONDITION(destination->bit_count == source->bit_count, "data_bitset_intersect_into: bit_count differs");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(destination->bit_count);
  for (i = 0; i < word_count; i++) {
    destination->words[i] &= source->words[i];
  }
}

void nexus_data_bitset_difference_into(NexusDataBitset *destination, const NexusDataBitset *source) {
  uint_large i;
  uint_large word_count;

  NEXUS_PROGRAM_PRECONDITION(destination != NULL, "data_bitset_difference_into: destination is NULL");
  NEXUS_PROGRAM_PRECONDITION(source != NULL, "data_bitset_difference_into: source is NULL");
  NEXUS_PROGRAM_PRECONDITION(destination->bit_count == source->bit_count, "data_bitset_difference_into: bit_count differs");

  word_count = NEXUS_DATA_BITSET_WORD_COUNT(destination->bit_count);
  for (i = 0; i < word_count; i++) {
    destination->words[i] &= ~source->words[i];
  }
}

/* ---------------------------------------------------------------------------- */
/* LIST                                                                         */
/* ---------------------------------------------------------------------------- */

NexusDataList *nexus_data_list_create(uint_large element_size, uint_large initial_capacity) {
  NexusDataList *list;

  list = (NexusDataList *)nexus_memory_allocate(NEXUS_SIZEOF(*list));
  n_data_buffer_initialize(&list->buffer, element_size, initial_capacity);
  return list;
}

void nexus_data_list_destroy(NexusDataList *list) {
  if (list == NULL) {
    return;
  }
  n_data_buffer_destroy(&list->buffer);
  nexus_memory_deallocate(list);
}

uint_large nexus_data_list_length_get(const NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_length_get: list is NULL");
  return list->buffer.length;
}

uint_large nexus_data_list_element_size_get(const NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_element_size_get: list is NULL");
  return list->buffer.element_size;
}

uint_large nexus_data_list_capacity_get(const NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_capacity_get: list is NULL");
  return list->buffer.capacity;
}

void nexus_data_list_reserve(NexusDataList *list, uint_large minimum_capacity) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_reserve: list is NULL");
  n_data_buffer_reserve(&list->buffer, minimum_capacity);
}

void *nexus_data_list_at(NexusDataList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_at: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->buffer.length, "data_list_at: index is outside length");
  return n_data_buffer_at(&list->buffer, index);
}

const void *nexus_data_list_at_const(const NexusDataList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_at_const: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->buffer.length, "data_list_at_const: index is outside length");
  return n_data_buffer_at_const(&list->buffer, index);
}

void *nexus_data_list_append_pointer(NexusDataList *list, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_append_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->buffer.element_size == NEXUS_EFFECTIVE_POINTER_SIZE_BYTES,
                             "data_list_append_pointer: element_size is not pointer-sized");

  return nexus_data_list_append(list, (void *)&pointer);
}

void *nexus_data_list_pointer_at(const NexusDataList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_pointer_at: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->buffer.element_size == NEXUS_EFFECTIVE_POINTER_SIZE_BYTES,
                             "data_list_pointer_at: element_size is not pointer-sized");
  NEXUS_PROGRAM_PRECONDITION(index < list->buffer.length, "data_list_pointer_at: index is outside length");

  return *(void *const *)n_data_buffer_at_const(&list->buffer, index);
}

void *nexus_data_list_append_uninitialized(NexusDataList *list) {
  byte *slot;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_append_uninitialized: list is NULL");

  n_data_buffer_reserve(&list->buffer, list->buffer.length + 1U);
  slot = n_data_buffer_at(&list->buffer, list->buffer.length);
  list->buffer.length++;
  return slot;
}

void *nexus_data_list_append(NexusDataList *list, const void *element) {
  void *slot;

  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_list_append: element is NULL");
  slot = nexus_data_list_append_uninitialized(list);
  nexus_memory_bytes_copy(element, slot, list->buffer.element_size);
  return slot;
}

void *nexus_data_list_data_get(NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_data_get: list is NULL");
  return list->buffer.data;
}

const void *nexus_data_list_data_get_const(const NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_data_get_const: list is NULL");
  return list->buffer.data;
}

void nexus_data_list_insert(NexusDataList *list, uint_large index, const void *element) {
  byte *slot;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_insert: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_list_insert: element is NULL");
  NEXUS_PROGRAM_PRECONDITION(index <= list->buffer.length, "data_list_insert: index is outside insertion range");

  n_data_buffer_reserve(&list->buffer, list->buffer.length + 1U);
  slot = n_data_buffer_at(&list->buffer, index);
  n_data_bytes_move(slot + list->buffer.stride, slot, (list->buffer.length - index) * list->buffer.stride);
  nexus_memory_bytes_copy(element, slot, list->buffer.element_size);
  list->buffer.length++;
}

void nexus_data_list_remove(NexusDataList *list, uint_large index, void *out_element) {
  byte *slot;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_remove: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->buffer.length, "data_list_remove: index is outside length");

  slot = n_data_buffer_at(&list->buffer, index);
  if (out_element != NULL) {
    nexus_memory_bytes_copy(slot, out_element, list->buffer.element_size);
  }

  n_data_bytes_move(slot, slot + list->buffer.stride, (list->buffer.length - index - 1U) * list->buffer.stride);
  list->buffer.length--;
}

boolean nexus_data_list_pop(NexusDataList *list, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_pop: list is NULL");

  if (list->buffer.length == 0) {
    return FALSE;
  }

  nexus_data_list_remove(list, list->buffer.length - 1U, out_element);
  return TRUE;
}

void nexus_data_list_insert_pointer(NexusDataList *list, uint_large index, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_insert_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->buffer.element_size == NEXUS_SIZEOF(void *), "data_list_insert_pointer: element_size is not pointer-sized");
  nexus_data_list_insert(list, index, (const void *)&pointer);
}

void nexus_data_list_remove_pointer(NexusDataList *list, uint_large index, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_remove_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->buffer.element_size == NEXUS_SIZEOF(void *), "data_list_remove_pointer: element_size is not pointer-sized");
  nexus_data_list_remove(list, index, (void *)out_pointer);
}

boolean nexus_data_list_pop_pointer(NexusDataList *list, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_pop_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->buffer.element_size == NEXUS_SIZEOF(void *), "data_list_pop_pointer: element_size is not pointer-sized");
  return nexus_data_list_pop(list, (void *)out_pointer);
}

void nexus_data_list_reverse(NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_reverse: list is NULL");
  n_data_buffer_reverse_range(&list->buffer, 0, list->buffer.length);
}

void nexus_data_list_clear(NexusDataList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_clear: list is NULL");
  list->buffer.length = 0;
}

void nexus_data_list_sort(NexusDataList *list, NexusDataCompareFunction *compare, void *user_data) {
  NexusDataArray array;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_list_sort: list is NULL");
  array = nexus_data_array_view_create(list->buffer.data, list->buffer.stride, list->buffer.length);
  nexus_data_array_sort(&array, compare, user_data);
}

/* ---------------------------------------------------------------------------- */
/* KEY SORT                                                                     */
/* ---------------------------------------------------------------------------- */

#define N_DATA_RADIX_BYTES   8U
#define N_DATA_RADIX_BUCKETS 256U

void nexus_data_keys_radix_sort(uint64 *keys, uint32 *values, uint_large count, uint64 *scratch_keys, uint32 *scratch_values) {
  uint32     histograms[N_DATA_RADIX_BYTES][N_DATA_RADIX_BUCKETS];
  uint64    *source_keys;
  uint32    *source_values;
  uint64    *target_keys;
  uint32    *target_values;
  uint64    *swap_keys;
  uint32    *swap_values;
  uint_large index;
  uint32     byte_index;
  uint32     bucket;
  uint32     running;
  uint32     bucket_count;
  uint32     shift;
  uint32     digit;

  NEXUS_PROGRAM_PRECONDITION(count == 0U || (keys != NULL && values != NULL && scratch_keys != NULL && scratch_values != NULL),
                             "data_keys_radix_sort: arrays are NULL");
  NEXUS_PROGRAM_PRECONDITION(count <= (uint_large)0xFFFFFFFFUL, "data_keys_radix_sort: count does not fit 32 bits");

  if (count < 2U) {
    return;
  }

  nexus_memory_bytes_set(histograms, NEXUS_SIZEOF(histograms), 0);
  for (index = 0; index < count; index++) {
    uint64 key;

    key = keys[index];
    for (byte_index = 0; byte_index < N_DATA_RADIX_BYTES; byte_index++) {
      histograms[byte_index][(uint32)((key >> (byte_index * 8U)) & 0xFFU)]++;
    }
  }

  source_keys   = keys;
  source_values = values;
  target_keys   = scratch_keys;
  target_values = scratch_values;

  for (byte_index = 0; byte_index < N_DATA_RADIX_BYTES; byte_index++) {
    shift = byte_index * 8U;

    /* A byte on which every key agrees would only copy the arrays, so it is skipped. */
    bucket_count = histograms[byte_index][(uint32)((source_keys[0] >> shift) & 0xFFU)];
    if (bucket_count == (uint32)count) {
      continue;
    }

    running = 0;
    for (bucket = 0; bucket < N_DATA_RADIX_BUCKETS; bucket++) {
      bucket_count                   = histograms[byte_index][bucket];
      histograms[byte_index][bucket] = running;
      running += bucket_count;
    }

    for (index = 0; index < count; index++) {
      digit                                        = (uint32)((source_keys[index] >> shift) & 0xFFU);
      target_keys[histograms[byte_index][digit]]   = source_keys[index];
      target_values[histograms[byte_index][digit]] = source_values[index];
      histograms[byte_index][digit]++;
    }

    swap_keys     = source_keys;
    swap_values   = source_values;
    source_keys   = target_keys;
    source_values = target_values;
    target_keys   = swap_keys;
    target_values = swap_values;
  }

  /* An odd number of executed passes leaves the result in the scratch arrays. */
  if (source_keys != keys) {
    nexus_memory_bytes_copy(source_keys, keys, count * NEXUS_SIZEOF(uint64));
    nexus_memory_bytes_copy(source_values, values, count * NEXUS_SIZEOF(uint32));
  }
}

/* ---------------------------------------------------------------------------- */
/* STACK                                                                        */
/* ---------------------------------------------------------------------------- */

NexusDataStack *nexus_data_stack_create(uint_large element_size, uint_large initial_capacity) {
  NexusDataStack *stack;

  stack = (NexusDataStack *)nexus_memory_allocate(NEXUS_SIZEOF(*stack));
  n_data_buffer_initialize(&stack->buffer, element_size, initial_capacity);
  return stack;
}

void nexus_data_stack_destroy(NexusDataStack *stack) {
  if (stack == NULL) {
    return;
  }
  n_data_buffer_destroy(&stack->buffer);
  nexus_memory_deallocate(stack);
}

uint_large nexus_data_stack_length_get(const NexusDataStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_length_get: stack is NULL");
  return stack->buffer.length;
}

void *nexus_data_stack_data_get(NexusDataStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_data_get: stack is NULL");
  return stack->buffer.data;
}

const void *nexus_data_stack_data_get_const(const NexusDataStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_data_get_const: stack is NULL");
  return stack->buffer.data;
}

void *nexus_data_stack_push(NexusDataStack *stack, const void *element) {
  byte *slot;

  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_push: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_stack_push: element is NULL");

  n_data_buffer_reserve(&stack->buffer, stack->buffer.length + 1U);
  slot = n_data_buffer_at(&stack->buffer, stack->buffer.length);
  nexus_memory_bytes_copy(element, slot, stack->buffer.element_size);
  stack->buffer.length++;
  return slot;
}

boolean nexus_data_stack_pop(NexusDataStack *stack, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_pop: stack is NULL");

  if (stack->buffer.length == 0) {
    return FALSE;
  }

  stack->buffer.length--;
  if (out_element != NULL) {
    nexus_memory_bytes_copy(n_data_buffer_at(&stack->buffer, stack->buffer.length), out_element, stack->buffer.element_size);
  }
  return TRUE;
}

void *nexus_data_stack_peek(NexusDataStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_peek: stack is NULL");
  if (stack->buffer.length == 0) {
    return NULL;
  }
  return n_data_buffer_at(&stack->buffer, stack->buffer.length - 1U);
}

void *nexus_data_stack_push_pointer(NexusDataStack *stack, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_push_pointer: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(stack->buffer.element_size == NEXUS_SIZEOF(void *), "data_stack_push_pointer: element_size is not pointer-sized");
  return nexus_data_stack_push(stack, (const void *)&pointer);
}

boolean nexus_data_stack_pop_pointer(NexusDataStack *stack, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_pop_pointer: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(stack->buffer.element_size == NEXUS_SIZEOF(void *), "data_stack_pop_pointer: element_size is not pointer-sized");
  return nexus_data_stack_pop(stack, (void *)out_pointer);
}

void *nexus_data_stack_peek_pointer(NexusDataStack *stack) {
  void *slot;
  void *pointer;

  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_peek_pointer: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(stack->buffer.element_size == NEXUS_SIZEOF(void *), "data_stack_peek_pointer: element_size is not pointer-sized");
  slot = nexus_data_stack_peek(stack);
  if (slot == NULL) {
    return NULL;
  }
  nexus_memory_bytes_copy(slot, (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

void nexus_data_stack_reverse(NexusDataStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_reverse: stack is NULL");
  n_data_buffer_reverse_range(&stack->buffer, 0, stack->buffer.length);
}

void nexus_data_stack_clear(NexusDataStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_stack_clear: stack is NULL");
  stack->buffer.length = 0;
}

/* ---------------------------------------------------------------------------- */
/* QUEUE                                                                        */
/* ---------------------------------------------------------------------------- */

static void n_data_queue_reserve(NexusDataQueue *queue, uint_large minimum_capacity) {
  NDataBuffer new_buffer;
  uint_large  index;

  if (minimum_capacity <= queue->buffer.capacity) {
    return;
  }

  n_data_buffer_initialize(&new_buffer, queue->buffer.element_size, minimum_capacity);
  for (index = 0; index < queue->buffer.length; index++) {
    uint_large source_index;

    source_index = (queue->head + index) % queue->buffer.capacity;
    nexus_memory_bytes_copy(n_data_buffer_at(&queue->buffer, source_index), n_data_buffer_at(&new_buffer, index), queue->buffer.element_size);
  }
  new_buffer.length = queue->buffer.length;

  n_data_buffer_destroy(&queue->buffer);
  queue->buffer = new_buffer;
  queue->head   = 0;
}

NexusDataQueue *nexus_data_queue_create(uint_large element_size, uint_large initial_capacity) {
  NexusDataQueue *queue;

  queue = (NexusDataQueue *)nexus_memory_allocate(NEXUS_SIZEOF(*queue));
  n_data_buffer_initialize(&queue->buffer, element_size, initial_capacity);
  queue->head = 0;
  return queue;
}

void nexus_data_queue_destroy(NexusDataQueue *queue) {
  if (queue == NULL) {
    return;
  }
  n_data_buffer_destroy(&queue->buffer);
  nexus_memory_deallocate(queue);
}

uint_large nexus_data_queue_length_get(const NexusDataQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_length_get: queue is NULL");
  return queue->buffer.length;
}

void *nexus_data_queue_linearize(NexusDataQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_linearize: queue is NULL");
  return n_data_buffer_linearize(&queue->buffer, &queue->head);
}

void *nexus_data_queue_push(NexusDataQueue *queue, const void *element) {
  uint_large index;
  byte      *slot;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_push: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_queue_push: element is NULL");

  if (queue->buffer.length == queue->buffer.capacity) {
    uint_large minimum_capacity;

    minimum_capacity = queue->buffer.capacity == 0 ? 4U : queue->buffer.capacity + 1U;
    n_data_queue_reserve(queue, minimum_capacity);
  }

  index = (queue->head + queue->buffer.length) % queue->buffer.capacity;
  slot  = n_data_buffer_at(&queue->buffer, index);
  nexus_memory_bytes_copy(element, slot, queue->buffer.element_size);
  queue->buffer.length++;
  return slot;
}

boolean nexus_data_queue_pop(NexusDataQueue *queue, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_pop: queue is NULL");

  if (queue->buffer.length == 0) {
    return FALSE;
  }

  if (out_element != NULL) {
    nexus_memory_bytes_copy(n_data_buffer_at(&queue->buffer, queue->head), out_element, queue->buffer.element_size);
  }

  queue->head = (queue->head + 1U) % queue->buffer.capacity;
  queue->buffer.length--;
  if (queue->buffer.length == 0) {
    queue->head = 0;
  }
  return TRUE;
}

void *nexus_data_queue_peek(NexusDataQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_peek: queue is NULL");
  if (queue->buffer.length == 0) {
    return NULL;
  }
  return n_data_buffer_at(&queue->buffer, queue->head);
}

void *nexus_data_queue_push_pointer(NexusDataQueue *queue, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_push_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->buffer.element_size == NEXUS_SIZEOF(void *), "data_queue_push_pointer: element_size is not pointer-sized");
  return nexus_data_queue_push(queue, (const void *)&pointer);
}

boolean nexus_data_queue_pop_pointer(NexusDataQueue *queue, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_pop_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->buffer.element_size == NEXUS_SIZEOF(void *), "data_queue_pop_pointer: element_size is not pointer-sized");
  return nexus_data_queue_pop(queue, (void *)out_pointer);
}

void *nexus_data_queue_peek_pointer(NexusDataQueue *queue) {
  void *slot;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_peek_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->buffer.element_size == NEXUS_SIZEOF(void *), "data_queue_peek_pointer: element_size is not pointer-sized");
  slot = nexus_data_queue_peek(queue);
  return slot == NULL ? NULL : *(void *const *)slot;
}

void nexus_data_queue_reverse(NexusDataQueue *queue) {
  uint_large left;
  uint_large right;
  uint_large remaining;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_reverse: queue is NULL");

  if (queue->buffer.length < 2U) {
    return;
  }

  left  = queue->head;
  right = queue->buffer.length - 1U;
  /* Map the last live index without overflowing head + index. */
  right = right >= queue->buffer.capacity - left ? right - (queue->buffer.capacity - left) : left + right;
  for (remaining = queue->buffer.length / 2U; remaining > 0; remaining--) {
    n_data_elements_swap(n_data_buffer_at(&queue->buffer, left), n_data_buffer_at(&queue->buffer, right), queue->buffer.element_size);
    left++;
    if (left == queue->buffer.capacity) {
      left = 0;
    }
    right = right == 0 ? queue->buffer.capacity - 1U : right - 1U;
  }
}

void nexus_data_queue_clear(NexusDataQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_queue_clear: queue is NULL");
  queue->buffer.length = 0;
  queue->head          = 0;
}

/* ---------------------------------------------------------------------------- */
/* RINGBUFFER                                                                   */
/* ---------------------------------------------------------------------------- */

NexusDataRingBuffer *nexus_data_ringbuffer_create(uint_large element_size, uint_large capacity) {
  NexusDataRingBuffer *ringbuffer;

  NEXUS_PROGRAM_PRECONDITION(capacity > 0, "data_ringbuffer_create: capacity must be greater than zero");

  ringbuffer = (NexusDataRingBuffer *)nexus_memory_allocate(NEXUS_SIZEOF(*ringbuffer));
  n_data_buffer_initialize(&ringbuffer->buffer, element_size, capacity);
  ringbuffer->head = 0;
  return ringbuffer;
}

void nexus_data_ringbuffer_destroy(NexusDataRingBuffer *ringbuffer) {
  if (ringbuffer == NULL) {
    return;
  }
  n_data_buffer_destroy(&ringbuffer->buffer);
  nexus_memory_deallocate(ringbuffer);
}

uint_large nexus_data_ringbuffer_length_get(const NexusDataRingBuffer *ringbuffer) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_length_get: ringbuffer is NULL");
  return ringbuffer->buffer.length;
}

uint_large nexus_data_ringbuffer_capacity_get(const NexusDataRingBuffer *ringbuffer) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_capacity_get: ringbuffer is NULL");
  return ringbuffer->buffer.capacity;
}

boolean nexus_data_ringbuffer_push(NexusDataRingBuffer *ringbuffer, const void *element) {
  boolean    overwritten;
  uint_large index;

  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_push: ringbuffer is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_ringbuffer_push: element is NULL");

  overwritten = ringbuffer->buffer.length == ringbuffer->buffer.capacity;
  if (overwritten) {
    index            = ringbuffer->head;
    ringbuffer->head = (ringbuffer->head + 1U) % ringbuffer->buffer.capacity;
  } else {
    index = (ringbuffer->head + ringbuffer->buffer.length) % ringbuffer->buffer.capacity;
    ringbuffer->buffer.length++;
  }

  nexus_memory_bytes_copy(element, n_data_buffer_at(&ringbuffer->buffer, index), ringbuffer->buffer.element_size);
  return overwritten;
}

boolean nexus_data_ringbuffer_pop(NexusDataRingBuffer *ringbuffer, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_pop: ringbuffer is NULL");

  if (ringbuffer->buffer.length == 0) {
    return FALSE;
  }

  if (out_element != NULL) {
    nexus_memory_bytes_copy(n_data_buffer_at(&ringbuffer->buffer, ringbuffer->head), out_element, ringbuffer->buffer.element_size);
  }

  ringbuffer->head = (ringbuffer->head + 1U) % ringbuffer->buffer.capacity;
  ringbuffer->buffer.length--;
  if (ringbuffer->buffer.length == 0) {
    ringbuffer->head = 0;
  }
  return TRUE;
}

void *nexus_data_ringbuffer_at(NexusDataRingBuffer *ringbuffer, uint_large logical_index) {
  uint_large physical_index;

  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_at: ringbuffer is NULL");
  NEXUS_PROGRAM_PRECONDITION(logical_index < ringbuffer->buffer.length, "data_ringbuffer_at: index is outside length");

  physical_index = (ringbuffer->head + logical_index) % ringbuffer->buffer.capacity;
  return n_data_buffer_at(&ringbuffer->buffer, physical_index);
}

void *nexus_data_ringbuffer_linearize(NexusDataRingBuffer *ringbuffer) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_linearize: ringbuffer is NULL");
  return n_data_buffer_linearize(&ringbuffer->buffer, &ringbuffer->head);
}

boolean nexus_data_ringbuffer_push_pointer(NexusDataRingBuffer *ringbuffer, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_push_pointer: ringbuffer is NULL");
  NEXUS_PROGRAM_PRECONDITION(ringbuffer->buffer.element_size == NEXUS_SIZEOF(void *),
                             "data_ringbuffer_push_pointer: element_size is not pointer-sized");
  return nexus_data_ringbuffer_push(ringbuffer, (const void *)&pointer);
}

boolean nexus_data_ringbuffer_pop_pointer(NexusDataRingBuffer *ringbuffer, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_pop_pointer: ringbuffer is NULL");
  NEXUS_PROGRAM_PRECONDITION(ringbuffer->buffer.element_size == NEXUS_SIZEOF(void *),
                             "data_ringbuffer_pop_pointer: element_size is not pointer-sized");
  return nexus_data_ringbuffer_pop(ringbuffer, (void *)out_pointer);
}

void *nexus_data_ringbuffer_pointer_at(NexusDataRingBuffer *ringbuffer, uint_large logical_index) {
  void *pointer;

  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_pointer_at: ringbuffer is NULL");
  NEXUS_PROGRAM_PRECONDITION(ringbuffer->buffer.element_size == NEXUS_SIZEOF(void *),
                             "data_ringbuffer_pointer_at: element_size is not pointer-sized");
  nexus_memory_bytes_copy(nexus_data_ringbuffer_at(ringbuffer, logical_index), (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

void nexus_data_ringbuffer_reverse(NexusDataRingBuffer *ringbuffer) {
  uint_large left;
  uint_large right;
  uint_large remaining;

  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_reverse: ringbuffer is NULL");

  if (ringbuffer->buffer.length < 2U) {
    return;
  }

  left  = ringbuffer->head;
  right = ringbuffer->buffer.length - 1U;
  /* Map the last live index without overflowing head + index. */
  right = right >= ringbuffer->buffer.capacity - left ? right - (ringbuffer->buffer.capacity - left) : left + right;
  for (remaining = ringbuffer->buffer.length / 2U; remaining > 0; remaining--) {
    n_data_elements_swap(n_data_buffer_at(&ringbuffer->buffer, left), n_data_buffer_at(&ringbuffer->buffer, right), ringbuffer->buffer.element_size);
    left++;
    if (left == ringbuffer->buffer.capacity) {
      left = 0;
    }
    right = right == 0 ? ringbuffer->buffer.capacity - 1U : right - 1U;
  }
}

void nexus_data_ringbuffer_clear(NexusDataRingBuffer *ringbuffer) {
  NEXUS_PROGRAM_PRECONDITION(ringbuffer != NULL, "data_ringbuffer_clear: ringbuffer is NULL");
  ringbuffer->buffer.length = 0;
  ringbuffer->head          = 0;
}

/* ---------------------------------------------------------------------------- */
/* PRIORITY QUEUE                                                               */
/* ---------------------------------------------------------------------------- */

NexusDataPriorityQueue *nexus_data_priority_queue_create(uint_large element_size, uint_large initial_capacity, NexusDataCompareFunction *compare,
                                                         void *user_data) {
  NexusDataPriorityQueue *queue;

  NEXUS_PROGRAM_PRECONDITION(compare != NULL, "data_priority_queue_create: compare is NULL");

  queue = (NexusDataPriorityQueue *)nexus_memory_allocate(NEXUS_SIZEOF(*queue));
  n_data_buffer_initialize(&queue->buffer, element_size, initial_capacity);
  queue->compare   = compare;
  queue->user_data = user_data;
  return queue;
}

void nexus_data_priority_queue_destroy(NexusDataPriorityQueue *queue) {
  if (queue == NULL) {
    return;
  }
  n_data_buffer_destroy(&queue->buffer);
  nexus_memory_deallocate(queue);
}

uint_large nexus_data_priority_queue_length_get(const NexusDataPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_length_get: queue is NULL");
  return queue->buffer.length;
}

void *nexus_data_priority_queue_push(NexusDataPriorityQueue *queue, const void *element) {
  uint_large index;
  byte      *slot;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_push: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_priority_queue_push: element is NULL");

  n_data_buffer_reserve(&queue->buffer, queue->buffer.length + 1U);
  index = queue->buffer.length;
  slot  = n_data_buffer_at(&queue->buffer, index);
  nexus_memory_bytes_copy(element, slot, queue->buffer.element_size);
  queue->buffer.length++;

  while (index > 0U) {
    uint_large parent;

    parent = (index - 1U) / 2U;
    if (queue->compare(n_data_buffer_at(&queue->buffer, parent), n_data_buffer_at(&queue->buffer, index), queue->user_data) <= 0) {
      break;
    }

    n_data_elements_swap(n_data_buffer_at(&queue->buffer, parent), n_data_buffer_at(&queue->buffer, index), queue->buffer.element_size);
    index = parent;
  }

  return n_data_buffer_at(&queue->buffer, index);
}

boolean nexus_data_priority_queue_pop(NexusDataPriorityQueue *queue, void *out_element) {
  uint_large root;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_pop: queue is NULL");

  if (queue->buffer.length == 0) {
    return FALSE;
  }

  if (out_element != NULL) {
    nexus_memory_bytes_copy(n_data_buffer_at(&queue->buffer, 0), out_element, queue->buffer.element_size);
  }

  queue->buffer.length--;
  if (queue->buffer.length == 0) {
    return TRUE;
  }

  nexus_memory_bytes_copy(n_data_buffer_at(&queue->buffer, queue->buffer.length), n_data_buffer_at(&queue->buffer, 0), queue->buffer.element_size);

  root = 0;
  while ((root * 2U) + 1U < queue->buffer.length) {
    uint_large child;

    child = (root * 2U) + 1U;
    if (child + 1U < queue->buffer.length &&
        queue->compare(n_data_buffer_at(&queue->buffer, child + 1U), n_data_buffer_at(&queue->buffer, child), queue->user_data) < 0) {
      child++;
    }

    if (queue->compare(n_data_buffer_at(&queue->buffer, root), n_data_buffer_at(&queue->buffer, child), queue->user_data) <= 0) {
      break;
    }

    n_data_elements_swap(n_data_buffer_at(&queue->buffer, root), n_data_buffer_at(&queue->buffer, child), queue->buffer.element_size);
    root = child;
  }
  return TRUE;
}

void *nexus_data_priority_queue_peek(NexusDataPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_peek: queue is NULL");
  if (queue->buffer.length == 0) {
    return NULL;
  }
  return n_data_buffer_at(&queue->buffer, 0);
}

void *nexus_data_priority_queue_push_pointer(NexusDataPriorityQueue *queue, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_push_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->buffer.element_size == NEXUS_SIZEOF(void *),
                             "data_priority_queue_push_pointer: element_size is not pointer-sized");
  return nexus_data_priority_queue_push(queue, (const void *)&pointer);
}

boolean nexus_data_priority_queue_pop_pointer(NexusDataPriorityQueue *queue, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_pop_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->buffer.element_size == NEXUS_SIZEOF(void *),
                             "data_priority_queue_pop_pointer: element_size is not pointer-sized");
  return nexus_data_priority_queue_pop(queue, (void *)out_pointer);
}

void *nexus_data_priority_queue_peek_pointer(NexusDataPriorityQueue *queue) {
  void *slot;
  void *pointer;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_peek_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->buffer.element_size == NEXUS_SIZEOF(void *),
                             "data_priority_queue_peek_pointer: element_size is not pointer-sized");
  slot = nexus_data_priority_queue_peek(queue);
  if (slot == NULL) {
    return NULL;
  }
  nexus_memory_bytes_copy(slot, (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

void nexus_data_priority_queue_clear(NexusDataPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_priority_queue_clear: queue is NULL");
  queue->buffer.length = 0;
}

/* ---------------------------------------------------------------------------- */
/* INTRUSIVE LINKED LIST                                                        */
/* ---------------------------------------------------------------------------- */

void nexus_data_linked_list_initialize(NexusDataLinkedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_initialize: list is NULL");
  list->first  = NULL;
  list->last   = NULL;
  list->length = 0;
}

void nexus_data_linked_list_node_initialize(NexusDataLinkedListNode *node) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "data_linked_list_node_initialize: node is NULL");
  node->previous = NULL;
  node->next     = NULL;
  node->owner    = NULL;
}

void nexus_data_linked_list_push_front(NexusDataLinkedList *list, NexusDataLinkedListNode *node) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_push_front: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "data_linked_list_push_front: node is NULL");
  NEXUS_PROGRAM_PRECONDITION(node->owner == NULL, "data_linked_list_push_front: node is already linked");

  node->owner    = list;
  node->previous = NULL;
  node->next     = list->first;
  if (list->first != NULL) {
    list->first->previous = node;
  } else {
    list->last = node;
  }
  list->first = node;
  list->length++;
}

void nexus_data_linked_list_push_back(NexusDataLinkedList *list, NexusDataLinkedListNode *node) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_push_back: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "data_linked_list_push_back: node is NULL");
  NEXUS_PROGRAM_PRECONDITION(node->owner == NULL, "data_linked_list_push_back: node is already linked");

  node->owner    = list;
  node->previous = list->last;
  node->next     = NULL;
  if (list->last != NULL) {
    list->last->next = node;
  } else {
    list->first = node;
  }
  list->last = node;
  list->length++;
}

void nexus_data_linked_list_insert_before(NexusDataLinkedList *list, NexusDataLinkedListNode *position, NexusDataLinkedListNode *node) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_insert_before: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(position != NULL, "data_linked_list_insert_before: position is NULL");
  NEXUS_PROGRAM_PRECONDITION(position->owner == list, "data_linked_list_insert_before: position belongs to another list");
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "data_linked_list_insert_before: node is NULL");
  NEXUS_PROGRAM_PRECONDITION(node->owner == NULL, "data_linked_list_insert_before: node is already linked");

  if (position == list->first) {
    nexus_data_linked_list_push_front(list, node);
    return;
  }

  node->owner              = list;
  node->previous           = position->previous;
  node->next               = position;
  position->previous->next = node;
  position->previous       = node;
  list->length++;
}

void nexus_data_linked_list_insert_after(NexusDataLinkedList *list, NexusDataLinkedListNode *position, NexusDataLinkedListNode *node) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_insert_after: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(position != NULL, "data_linked_list_insert_after: position is NULL");
  NEXUS_PROGRAM_PRECONDITION(position->owner == list, "data_linked_list_insert_after: position belongs to another list");
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "data_linked_list_insert_after: node is NULL");
  NEXUS_PROGRAM_PRECONDITION(node->owner == NULL, "data_linked_list_insert_after: node is already linked");

  if (position == list->last) {
    nexus_data_linked_list_push_back(list, node);
    return;
  }

  node->owner              = list;
  node->previous           = position;
  node->next               = position->next;
  position->next->previous = node;
  position->next           = node;
  list->length++;
}

void nexus_data_linked_list_remove(NexusDataLinkedList *list, NexusDataLinkedListNode *node) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_remove: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "data_linked_list_remove: node is NULL");
  NEXUS_PROGRAM_PRECONDITION(node->owner == list, "data_linked_list_remove: node belongs to another list");

  if (node->previous != NULL) {
    node->previous->next = node->next;
  } else {
    list->first = node->next;
  }

  if (node->next != NULL) {
    node->next->previous = node->previous;
  } else {
    list->last = node->previous;
  }

  node->previous = NULL;
  node->next     = NULL;
  node->owner    = NULL;
  list->length--;
}

NexusDataLinkedListNode *nexus_data_linked_list_pop_front(NexusDataLinkedList *list) {
  NexusDataLinkedListNode *node;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_pop_front: list is NULL");
  node = list->first;
  if (node != NULL) {
    nexus_data_linked_list_remove(list, node);
  }
  return node;
}

NexusDataLinkedListNode *nexus_data_linked_list_pop_back(NexusDataLinkedList *list) {
  NexusDataLinkedListNode *node;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_pop_back: list is NULL");
  node = list->last;
  if (node != NULL) {
    nexus_data_linked_list_remove(list, node);
  }
  return node;
}

void nexus_data_linked_list_reverse(NexusDataLinkedList *list) {
  NexusDataLinkedListNode *node;
  NexusDataLinkedListNode *next;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_reverse: list is NULL");

  node = list->first;
  while (node != NULL) {
    next           = node->next;
    node->next     = node->previous;
    node->previous = next;
    node           = next;
  }
  node        = list->first;
  list->first = list->last;
  list->last  = node;
}

void nexus_data_linked_list_clear(NexusDataLinkedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_linked_list_clear: list is NULL");

  while (list->first != NULL) {
    nexus_data_linked_list_remove(list, list->first);
  }
}

/* ---------------------------------------------------------------------------- */
/* SLABBED STORAGE                                                              */
/* ---------------------------------------------------------------------------- */

typedef struct NDataSlab {
  struct NDataSlab     *next;
  NDataMaximumAlignment alignment;
} NDataSlab;

typedef struct NDataSlabPool {
  NDataSlab *first;
  void      *free_slots;
  uint_large element_size;
  uint_large stride;
  uint_large capacity;
  uint_large slab_capacity;
} NDataSlabPool;

struct NexusDataSlabbedList {
  NDataSlabPool pool;
  NDataBuffer   order;
};

struct NexusDataSlabbedStack {
  NexusDataSlabbedList list;
};

struct NexusDataSlabbedQueue {
  NDataSlabPool pool;
  NDataBuffer   order;
  uint_large    head;
};

struct NexusDataSlabbedPriorityQueue {
  NexusDataSlabbedList      list;
  NexusDataCompareFunction *compare;
  void                     *user_data;
};

static void n_data_slab_pool_initialize(NDataSlabPool *pool, uint_large element_size, uint_large slab_capacity) {
  NEXUS_PROGRAM_PRECONDITION(element_size > 0, "slab element_size must be greater than zero");
  pool->first        = NULL;
  pool->free_slots   = NULL;
  pool->element_size = element_size;
  pool->stride =
      n_data_align_up(element_size < NEXUS_SIZEOF(void *) ? NEXUS_SIZEOF(void *) : element_size, (uint_large)NEXUS_ALIGNOF(NDataMaximumAlignment));
  pool->capacity      = 0;
  pool->slab_capacity = slab_capacity == 0 ? NEXUS_DATA_SLAB_DEFAULT_CAPACITY : slab_capacity;
}

static void n_data_slab_pool_reserve(NDataSlabPool *pool, uint_large minimum_capacity) {
  uint_large payload_size;
  uint_large required_slabs;
  uint_large rounded_capacity;
  if (minimum_capacity <= pool->capacity) {
    return;
  }

  required_slabs   = ((minimum_capacity - 1U) / pool->slab_capacity) + 1U;
  rounded_capacity = n_data_allocation_size_get(pool->slab_capacity, required_slabs);
  (void)n_data_allocation_size_get(pool->stride, rounded_capacity);
  payload_size = n_data_allocation_size_get(pool->stride, pool->slab_capacity);
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(payload_size <= UINT_LARGE_MAX_VAL - NEXUS_SIZEOF(NDataSlab), "slab allocation size overflow");
  while (pool->capacity < minimum_capacity) {
    NDataSlab *slab;
    uint_large index;
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(pool->capacity <= UINT_LARGE_MAX_VAL - pool->slab_capacity, "slab capacity overflow");
    slab        = (NDataSlab *)nexus_memory_allocate(NEXUS_SIZEOF(*slab) + payload_size);
    slab->next  = pool->first;
    pool->first = slab;
    /* A maximally aligned header keeps every slot aligned, including odd byte widths.
       Free-list links occupy dead payload bytes, so live slots need no node header. */
    index = pool->slab_capacity;
    while (index > 0) {
      byte *slot;
      index--;
      slot             = (byte *)(slab + 1) + (index * pool->stride);
      *(void **)slot   = pool->free_slots;
      pool->free_slots = slot;
    }
    pool->capacity += pool->slab_capacity;
  }
}

static void *n_data_slab_pool_take(NDataSlabPool *pool) {
  void *slot;
  if (pool->free_slots == NULL) {
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(pool->capacity < UINT_LARGE_MAX_VAL, "slab length overflow");
    n_data_slab_pool_reserve(pool, pool->capacity + 1U);
    NEXUS_PROGRAM_POSTCONDITION_ALWAYS(pool->free_slots != NULL, "slab growth did not provide a free slot");
  }
  slot             = pool->free_slots;
  pool->free_slots = *(void **)slot;
  return slot;
}

static void n_data_slab_pool_release(NDataSlabPool *pool, void *slot) {
  *(void **)slot   = pool->free_slots;
  pool->free_slots = slot;
}

static void n_data_slab_pool_destroy(NDataSlabPool *pool) {
  NDataSlab *slab;
  slab = pool->first;
  while (slab != NULL) {
    NDataSlab *next;
    next = slab->next;
    nexus_memory_deallocate(slab);
    slab = next;
  }
  pool->first      = NULL;
  pool->free_slots = NULL;
  pool->capacity   = 0;
}

/* ---------------------------------------------------------------------------- */
/* SLABBED LIST                                                                 */
/* ---------------------------------------------------------------------------- */

NexusDataSlabbedList *nexus_data_slabbed_list_create(uint_large element_size, uint_large initial_capacity, uint_large slab_capacity) {
  NexusDataSlabbedList *list;
  list = (NexusDataSlabbedList *)nexus_memory_allocate(NEXUS_SIZEOF(*list));
  n_data_slab_pool_initialize(&list->pool, element_size, slab_capacity);
  n_data_buffer_initialize(&list->order, NEXUS_SIZEOF(void *), 0);
  nexus_data_slabbed_list_reserve(list, initial_capacity);
  return list;
}

void nexus_data_slabbed_list_destroy(NexusDataSlabbedList *list) {
  if (list == NULL) {
    return;
  }
  n_data_buffer_destroy(&list->order);
  n_data_slab_pool_destroy(&list->pool);
  nexus_memory_deallocate(list);
}

uint_large nexus_data_slabbed_list_length_get(const NexusDataSlabbedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_length_get: list is NULL");
  return list->order.length;
}

uint_large nexus_data_slabbed_list_capacity_get(const NexusDataSlabbedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_capacity_get: list is NULL");
  return list->pool.capacity < list->order.capacity ? list->pool.capacity : list->order.capacity;
}

void nexus_data_slabbed_list_reserve(NexusDataSlabbedList *list, uint_large minimum_capacity) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_reserve: list is NULL");
  n_data_slab_pool_reserve(&list->pool, minimum_capacity);
  n_data_buffer_reserve(&list->order, minimum_capacity);
}

void *nexus_data_slabbed_list_at(NexusDataSlabbedList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_at: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->order.length, "data_slabbed_list_at: index is outside length");
  return ((void **)list->order.data)[index];
}

const void *nexus_data_slabbed_list_at_const(const NexusDataSlabbedList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_at_const: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->order.length, "data_slabbed_list_at_const: index is outside length");
  return ((void *const *)list->order.data)[index];
}

void *nexus_data_slabbed_list_append_uninitialized(NexusDataSlabbedList *list) {
  void *slot;
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_append_uninitialized: list is NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(list->order.length < UINT_LARGE_MAX_VAL, "slabbed list length overflow");
  n_data_buffer_reserve(&list->order, list->order.length + 1U);
  slot                                              = n_data_slab_pool_take(&list->pool);
  ((void **)list->order.data)[list->order.length++] = slot;
  return slot;
}

void *nexus_data_slabbed_list_append(NexusDataSlabbedList *list, const void *element) {
  void *slot;
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_append: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_slabbed_list_append: element is NULL");
  slot = nexus_data_slabbed_list_append_uninitialized(list);
  nexus_memory_bytes_copy(element, slot, list->pool.element_size);
  return slot;
}

void *nexus_data_slabbed_list_append_pointer(NexusDataSlabbedList *list, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_append_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_list_append_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_list_append(list, (const void *)&pointer);
}

void *nexus_data_slabbed_list_pointer_at(const NexusDataSlabbedList *list, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_pointer_at: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_list_pointer_at: element_size is not pointer-sized");
  return *(void *const *)nexus_data_slabbed_list_at_const(list, index);
}

void nexus_data_slabbed_list_insert(NexusDataSlabbedList *list, uint_large index, const void *element) {
  void  *slot;
  void **order;
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_insert: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_slabbed_list_insert: element is NULL");
  NEXUS_PROGRAM_PRECONDITION(index <= list->order.length, "data_slabbed_list_insert: index is outside insertion range");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(list->order.length < UINT_LARGE_MAX_VAL, "slabbed list length overflow");
  n_data_buffer_reserve(&list->order, list->order.length + 1U);
  slot = n_data_slab_pool_take(&list->pool);
  nexus_memory_bytes_copy(element, slot, list->pool.element_size);
  order = (void **)list->order.data;
  n_data_bytes_move((void *)(order + index + 1U), (const void *)(order + index), (list->order.length - index) * NEXUS_SIZEOF(void *));
  order[index] = slot;
  list->order.length++;
}

void nexus_data_slabbed_list_remove(NexusDataSlabbedList *list, uint_large index, void *out_element) {
  void **order;
  void  *slot;
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_remove: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(index < list->order.length, "data_slabbed_list_remove: index is outside length");
  order = (void **)list->order.data;
  slot  = order[index];
  if (out_element != NULL) {
    nexus_memory_bytes_copy(slot, out_element, list->pool.element_size);
  }
  n_data_bytes_move((void *)(order + index), (const void *)(order + index + 1U), (list->order.length - index - 1U) * NEXUS_SIZEOF(void *));
  list->order.length--;
  n_data_slab_pool_release(&list->pool, slot);
}

boolean nexus_data_slabbed_list_pop(NexusDataSlabbedList *list, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_pop: list is NULL");
  if (list->order.length == 0) {
    return FALSE;
  }
  nexus_data_slabbed_list_remove(list, list->order.length - 1U, out_element);
  return TRUE;
}

void nexus_data_slabbed_list_insert_pointer(NexusDataSlabbedList *list, uint_large index, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_insert_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_list_insert_pointer: element_size is not pointer-sized");
  nexus_data_slabbed_list_insert(list, index, (const void *)&pointer);
}

void nexus_data_slabbed_list_remove_pointer(NexusDataSlabbedList *list, uint_large index, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_remove_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_list_remove_pointer: element_size is not pointer-sized");
  nexus_data_slabbed_list_remove(list, index, (void *)out_pointer);
}

boolean nexus_data_slabbed_list_pop_pointer(NexusDataSlabbedList *list, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_pop_pointer: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(list->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_list_pop_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_list_pop(list, (void *)out_pointer);
}

void nexus_data_slabbed_list_reverse(NexusDataSlabbedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_reverse: list is NULL");
  n_data_buffer_reverse_range(&list->order, 0, list->order.length);
}

void nexus_data_slabbed_list_clear(NexusDataSlabbedList *list) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_clear: list is NULL");
  while (list->order.length > 0) {
    list->order.length--;
    n_data_slab_pool_release(&list->pool, ((void **)list->order.data)[list->order.length]);
  }
}

typedef struct NDataSlabbedSortContext {
  NexusDataCompareFunction *compare;
  void                     *user_data;
} NDataSlabbedSortContext;

static int n_data_slabbed_compare_pointers(const void *left, const void *right, void *user_data) {
  NDataSlabbedSortContext *context;
  context = (NDataSlabbedSortContext *)user_data;
  return context->compare(*(void *const *)left, *(void *const *)right, context->user_data);
}

void nexus_data_slabbed_list_sort(NexusDataSlabbedList *list, NexusDataCompareFunction *compare, void *user_data) {
  NexusDataArray          array;
  NDataSlabbedSortContext context;
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_slabbed_list_sort: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(compare != NULL, "data_slabbed_list_sort: compare is NULL");
  context.compare   = compare;
  context.user_data = user_data;
  array             = nexus_data_array_view_create(list->order.data, NEXUS_SIZEOF(void *), list->order.length);
  nexus_data_array_sort(&array, n_data_slabbed_compare_pointers, &context);
}

/* ---------------------------------------------------------------------------- */
/* SLABBED STACK                                                                */
/* ---------------------------------------------------------------------------- */

NexusDataSlabbedStack *nexus_data_slabbed_stack_create(uint_large element_size, uint_large initial_capacity, uint_large slab_capacity) {
  NexusDataSlabbedStack *stack;
  stack = (NexusDataSlabbedStack *)nexus_memory_allocate(NEXUS_SIZEOF(*stack));
  n_data_slab_pool_initialize(&stack->list.pool, element_size, slab_capacity);
  n_data_buffer_initialize(&stack->list.order, NEXUS_SIZEOF(void *), 0);
  nexus_data_slabbed_list_reserve(&stack->list, initial_capacity);
  return stack;
}

void nexus_data_slabbed_stack_destroy(NexusDataSlabbedStack *stack) {
  if (stack == NULL) {
    return;
  }
  n_data_buffer_destroy(&stack->list.order);
  n_data_slab_pool_destroy(&stack->list.pool);
  nexus_memory_deallocate(stack);
}

uint_large nexus_data_slabbed_stack_length_get(const NexusDataSlabbedStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_length_get: stack is NULL");
  return stack->list.order.length;
}

uint_large nexus_data_slabbed_stack_capacity_get(const NexusDataSlabbedStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_capacity_get: stack is NULL");
  return nexus_data_slabbed_list_capacity_get(&stack->list);
}

void nexus_data_slabbed_stack_reserve(NexusDataSlabbedStack *stack, uint_large minimum_capacity) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_reserve: stack is NULL");
  nexus_data_slabbed_list_reserve(&stack->list, minimum_capacity);
}

void *nexus_data_slabbed_stack_push(NexusDataSlabbedStack *stack, const void *element) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_push: stack is NULL");
  return nexus_data_slabbed_list_append(&stack->list, element);
}

boolean nexus_data_slabbed_stack_pop(NexusDataSlabbedStack *stack, void *out_element) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_pop: stack is NULL");
  return nexus_data_slabbed_list_pop(&stack->list, out_element);
}

void *nexus_data_slabbed_stack_peek(NexusDataSlabbedStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_peek: stack is NULL");
  if (stack->list.order.length == 0) {
    return NULL;
  }
  return ((void **)stack->list.order.data)[stack->list.order.length - 1U];
}

void *nexus_data_slabbed_stack_push_pointer(NexusDataSlabbedStack *stack, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_push_pointer: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(stack->list.pool.element_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_stack_push_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_stack_push(stack, (const void *)&pointer);
}

boolean nexus_data_slabbed_stack_pop_pointer(NexusDataSlabbedStack *stack, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_pop_pointer: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(stack->list.pool.element_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_stack_pop_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_stack_pop(stack, (void *)out_pointer);
}

void *nexus_data_slabbed_stack_peek_pointer(NexusDataSlabbedStack *stack) {
  void *slot;
  void *pointer;

  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_peek_pointer: stack is NULL");
  NEXUS_PROGRAM_PRECONDITION(stack->list.pool.element_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_stack_peek_pointer: element_size is not pointer-sized");
  slot = nexus_data_slabbed_stack_peek(stack);
  if (slot == NULL) {
    return NULL;
  }
  nexus_memory_bytes_copy(slot, (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

void nexus_data_slabbed_stack_reverse(NexusDataSlabbedStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_reverse: stack is NULL");
  n_data_buffer_reverse_range(&stack->list.order, 0, stack->list.order.length);
}

void nexus_data_slabbed_stack_clear(NexusDataSlabbedStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(stack != NULL, "data_slabbed_stack_clear: stack is NULL");
  nexus_data_slabbed_list_clear(&stack->list);
}

/* ---------------------------------------------------------------------------- */
/* SLABBED QUEUE                                                                */
/* ---------------------------------------------------------------------------- */

NexusDataSlabbedQueue *nexus_data_slabbed_queue_create(uint_large element_size, uint_large initial_capacity, uint_large slab_capacity) {
  NexusDataSlabbedQueue *queue;
  queue = (NexusDataSlabbedQueue *)nexus_memory_allocate(NEXUS_SIZEOF(*queue));
  n_data_slab_pool_initialize(&queue->pool, element_size, slab_capacity);
  n_data_buffer_initialize(&queue->order, NEXUS_SIZEOF(void *), 0);
  queue->head = 0;
  nexus_data_slabbed_queue_reserve(queue, initial_capacity);
  return queue;
}

void nexus_data_slabbed_queue_destroy(NexusDataSlabbedQueue *queue) {
  if (queue == NULL) {
    return;
  }
  n_data_buffer_destroy(&queue->order);
  n_data_slab_pool_destroy(&queue->pool);
  nexus_memory_deallocate(queue);
}

uint_large nexus_data_slabbed_queue_length_get(const NexusDataSlabbedQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_length_get: queue is NULL");
  return queue->order.length;
}

uint_large nexus_data_slabbed_queue_capacity_get(const NexusDataSlabbedQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_capacity_get: queue is NULL");
  return queue->pool.capacity < queue->order.capacity ? queue->pool.capacity : queue->order.capacity;
}

void nexus_data_slabbed_queue_reserve(NexusDataSlabbedQueue *queue, uint_large minimum_capacity) {
  NDataBuffer new_order;
  uint_large  index;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_reserve: queue is NULL");
  n_data_slab_pool_reserve(&queue->pool, minimum_capacity);
  if (minimum_capacity <= queue->order.capacity) {
    return;
  }
  /* Grow the directory geometrically, copying only references in FIFO order. */
  n_data_buffer_initialize(&new_order, NEXUS_SIZEOF(void *), 0);
  n_data_buffer_reserve(&new_order, minimum_capacity);
  for (index = 0; index < queue->order.length; index++) {
    uint_large physical_index;
    physical_index = index >= queue->order.capacity - queue->head ? index - (queue->order.capacity - queue->head) : queue->head + index;
    ((void **)new_order.data)[index] = ((void **)queue->order.data)[physical_index];
  }
  new_order.length = queue->order.length;
  n_data_buffer_destroy(&queue->order);
  queue->order = new_order;
  queue->head  = 0;
}

void *nexus_data_slabbed_queue_push(NexusDataSlabbedQueue *queue, const void *element) {
  void      *slot;
  uint_large index;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_push: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(element != NULL, "data_slabbed_queue_push: element is NULL");
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(queue->order.length < UINT_LARGE_MAX_VAL, "slabbed queue length overflow");
  if (queue->order.length == queue->order.capacity) {
    nexus_data_slabbed_queue_reserve(queue, queue->order.length + 1U);
  }
  slot = n_data_slab_pool_take(&queue->pool);
  nexus_memory_bytes_copy(element, slot, queue->pool.element_size);
  index = queue->order.length >= queue->order.capacity - queue->head ? queue->order.length - (queue->order.capacity - queue->head)
                                                                     : queue->head + queue->order.length;
  ((void **)queue->order.data)[index] = slot;
  queue->order.length++;
  return slot;
}

boolean nexus_data_slabbed_queue_pop(NexusDataSlabbedQueue *queue, void *out_element) {
  void *slot;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_pop: queue is NULL");
  if (queue->order.length == 0) {
    return FALSE;
  }
  slot = ((void **)queue->order.data)[queue->head];
  if (out_element != NULL) {
    nexus_memory_bytes_copy(slot, out_element, queue->pool.element_size);
  }
  n_data_slab_pool_release(&queue->pool, slot);
  queue->order.length--;
  queue->head++;
  if (queue->head == queue->order.capacity || queue->order.length == 0) {
    queue->head = 0;
  }
  return TRUE;
}

void *nexus_data_slabbed_queue_peek(NexusDataSlabbedQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_peek: queue is NULL");
  return queue->order.length == 0 ? NULL : ((void **)queue->order.data)[queue->head];
}

void *nexus_data_slabbed_queue_push_pointer(NexusDataSlabbedQueue *queue, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_push_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_queue_push_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_queue_push(queue, (const void *)&pointer);
}

boolean nexus_data_slabbed_queue_pop_pointer(NexusDataSlabbedQueue *queue, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_pop_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_queue_pop_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_queue_pop(queue, (void *)out_pointer);
}

void *nexus_data_slabbed_queue_peek_pointer(NexusDataSlabbedQueue *queue) {
  void *slot;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_peek_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->pool.element_size == NEXUS_SIZEOF(void *), "data_slabbed_queue_peek_pointer: element_size is not pointer-sized");
  slot = nexus_data_slabbed_queue_peek(queue);
  return slot == NULL ? NULL : *(void *const *)slot;
}

void nexus_data_slabbed_queue_reverse(NexusDataSlabbedQueue *queue) {
  uint_large left;
  uint_large right;
  uint_large remaining;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_reverse: queue is NULL");

  if (queue->order.length < 2U) {
    return;
  }

  left  = queue->head;
  right = queue->order.length - 1U;
  /* Map the last live index without overflowing head + index. */
  right = right >= queue->order.capacity - left ? right - (queue->order.capacity - left) : left + right;
  for (remaining = queue->order.length / 2U; remaining > 0; remaining--) {
    n_data_elements_swap(n_data_buffer_at(&queue->order, left), n_data_buffer_at(&queue->order, right), queue->order.element_size);
    left++;
    if (left == queue->order.capacity) {
      left = 0;
    }
    right = right == 0 ? queue->order.capacity - 1U : right - 1U;
  }
}

void nexus_data_slabbed_queue_clear(NexusDataSlabbedQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_queue_clear: queue is NULL");
  while (queue->order.length > 0) {
    n_data_slab_pool_release(&queue->pool, ((void **)queue->order.data)[queue->head]);
    queue->order.length--;
    queue->head++;
    if (queue->head == queue->order.capacity) {
      queue->head = 0;
    }
  }
  queue->head = 0;
}

/* ---------------------------------------------------------------------------- */
/* SLABBED PRIORITY QUEUE                                                       */
/* ---------------------------------------------------------------------------- */

NexusDataSlabbedPriorityQueue *nexus_data_slabbed_priority_queue_create(uint_large element_size, uint_large initial_capacity,
                                                                        uint_large slab_capacity, NexusDataCompareFunction *compare,
                                                                        void *user_data) {
  NexusDataSlabbedPriorityQueue *queue;
  NEXUS_PROGRAM_PRECONDITION(compare != NULL, "data_slabbed_priority_queue_create: compare is NULL");
  queue = (NexusDataSlabbedPriorityQueue *)nexus_memory_allocate(NEXUS_SIZEOF(*queue));
  n_data_slab_pool_initialize(&queue->list.pool, element_size, slab_capacity);
  n_data_buffer_initialize(&queue->list.order, NEXUS_SIZEOF(void *), 0);
  nexus_data_slabbed_list_reserve(&queue->list, initial_capacity);
  queue->compare   = compare;
  queue->user_data = user_data;
  return queue;
}

void nexus_data_slabbed_priority_queue_destroy(NexusDataSlabbedPriorityQueue *queue) {
  if (queue == NULL) {
    return;
  }
  n_data_buffer_destroy(&queue->list.order);
  n_data_slab_pool_destroy(&queue->list.pool);
  nexus_memory_deallocate(queue);
}

uint_large nexus_data_slabbed_priority_queue_length_get(const NexusDataSlabbedPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_length_get: queue is NULL");
  return queue->list.order.length;
}

uint_large nexus_data_slabbed_priority_queue_capacity_get(const NexusDataSlabbedPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_capacity_get: queue is NULL");
  return nexus_data_slabbed_list_capacity_get(&queue->list);
}

void nexus_data_slabbed_priority_queue_reserve(NexusDataSlabbedPriorityQueue *queue, uint_large minimum_capacity) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_reserve: queue is NULL");
  nexus_data_slabbed_list_reserve(&queue->list, minimum_capacity);
}

void *nexus_data_slabbed_priority_queue_push(NexusDataSlabbedPriorityQueue *queue, const void *element) {
  void      *slot;
  void     **order;
  uint_large index;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_push: queue is NULL");
  slot  = nexus_data_slabbed_list_append(&queue->list, element);
  index = queue->list.order.length - 1U;
  order = (void **)queue->list.order.data;
  while (index > 0) {
    uint_large parent;
    parent = (index - 1U) / 2U;
    if (queue->compare(order[parent], slot, queue->user_data) <= 0) {
      break;
    }
    order[index] = order[parent];
    index        = parent;
  }
  order[index] = slot;
  return slot;
}

boolean nexus_data_slabbed_priority_queue_pop(NexusDataSlabbedPriorityQueue *queue, void *out_element) {
  void     **order;
  void      *removed;
  void      *replacement;
  uint_large root;
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_pop: queue is NULL");
  if (queue->list.order.length == 0) {
    return FALSE;
  }
  order   = (void **)queue->list.order.data;
  removed = order[0];
  if (out_element != NULL) {
    nexus_memory_bytes_copy(removed, out_element, queue->list.pool.element_size);
  }
  queue->list.order.length--;
  replacement = order[queue->list.order.length];
  n_data_slab_pool_release(&queue->list.pool, removed);
  if (queue->list.order.length == 0) {
    return TRUE;
  }
  root = 0;
  while (root < queue->list.order.length / 2U) {
    uint_large child;
    child = (root * 2U) + 1U;
    if (child + 1U < queue->list.order.length && queue->compare(order[child + 1U], order[child], queue->user_data) < 0) {
      child++;
    }
    if (queue->compare(replacement, order[child], queue->user_data) <= 0) {
      break;
    }
    order[root] = order[child];
    root        = child;
  }
  order[root] = replacement;
  return TRUE;
}

void *nexus_data_slabbed_priority_queue_peek(NexusDataSlabbedPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_peek: queue is NULL");
  return queue->list.order.length == 0 ? NULL : ((void **)queue->list.order.data)[0];
}

void *nexus_data_slabbed_priority_queue_push_pointer(NexusDataSlabbedPriorityQueue *queue, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_push_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->list.pool.element_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_priority_queue_push_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_priority_queue_push(queue, (const void *)&pointer);
}

boolean nexus_data_slabbed_priority_queue_pop_pointer(NexusDataSlabbedPriorityQueue *queue, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_pop_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->list.pool.element_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_priority_queue_pop_pointer: element_size is not pointer-sized");
  return nexus_data_slabbed_priority_queue_pop(queue, (void *)out_pointer);
}

void *nexus_data_slabbed_priority_queue_peek_pointer(NexusDataSlabbedPriorityQueue *queue) {
  void *slot;
  void *pointer;

  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_peek_pointer: queue is NULL");
  NEXUS_PROGRAM_PRECONDITION(queue->list.pool.element_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_priority_queue_peek_pointer: element_size is not pointer-sized");
  slot = nexus_data_slabbed_priority_queue_peek(queue);
  if (slot == NULL) {
    return NULL;
  }
  nexus_memory_bytes_copy(slot, (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

void nexus_data_slabbed_priority_queue_clear(NexusDataSlabbedPriorityQueue *queue) {
  NEXUS_PROGRAM_PRECONDITION(queue != NULL, "data_slabbed_priority_queue_clear: queue is NULL");
  nexus_data_slabbed_list_clear(&queue->list);
}

/* ---------------------------------------------------------------------------- */
/* HASHMAP                                                                      */
/* ---------------------------------------------------------------------------- */

#define N_DATA_HASHMAP_GROUP_SLOT_COUNT 8U
#define N_DATA_HASHMAP_GROUP_LOAD_LIMIT 7U
#define N_DATA_HASHMAP_EMPTY            0x80U
#define N_DATA_HASHMAP_DELETED          0xfeU

struct NexusDataHashMap {
  NDataSlabPool *pool;
  byte          *metadata;
  byte          *entries;

  uint_large key_size;
  uint_large value_size;
  uint_large value_offset;
  uint_large entry_stride;

  uint_large group_count;
  uint_large length;
  uint_large deleted_count;

  uint64 seed;
  uint64 version;

  NexusDataHashFunction     *hash;
  NexusDataEqualityFunction *equals;
  void                      *user_data;
};

typedef struct NDataHashMapProbe {
  uint_large slot;
  boolean    found;
  boolean    insertable;
  uint8      fingerprint;
} NDataHashMapProbe;

static uint64 n_data_hashmap_hash_bytes(const void *key, uint_large key_size, uint64 seed, void *user_data) {
  (void)user_data;
  return nexus_hash_xxh3_64_seeded(key, key_size, seed);
}

static boolean n_data_hashmap_equals_bytes(const void *left, const void *right, uint_large key_size, void *user_data) {
  const byte *left_bytes;
  const byte *right_bytes;
  uint_large  index;
  (void)user_data;

  left_bytes  = (const byte *)left;
  right_bytes = (const byte *)right;
  for (index = 0; index < key_size; index++) {
    if (left_bytes[index] != right_bytes[index]) {
      return FALSE;
    }
  }
  return TRUE;
}

static uint_large n_data_hashmap_slot_count_get(const NexusDataHashMap *hashmap) {
  return hashmap->group_count * N_DATA_HASHMAP_GROUP_SLOT_COUNT;
}

static byte *n_data_hashmap_key_at(NexusDataHashMap *hashmap, uint_large slot) {
  return hashmap->pool == NULL ? hashmap->entries + (slot * hashmap->entry_stride) : ((byte **)hashmap->entries)[slot];
}

static const byte *n_data_hashmap_key_at_const(const NexusDataHashMap *hashmap, uint_large slot) {
  return hashmap->pool == NULL ? hashmap->entries + (slot * hashmap->entry_stride) : ((byte *const *)hashmap->entries)[slot];
}

static byte *n_data_hashmap_value_at(NexusDataHashMap *hashmap, uint_large slot) {
  return n_data_hashmap_key_at(hashmap, slot) + hashmap->value_offset;
}

static const byte *n_data_hashmap_value_at_const(const NexusDataHashMap *hashmap, uint_large slot) {
  return n_data_hashmap_key_at_const(hashmap, slot) + hashmap->value_offset;
}

static uint_large n_data_hashmap_group_count_for_capacity(uint_large capacity) {
  uint_large group_count;

  group_count = 1U;
  while (group_count <= UINT_LARGE_MAX_VAL / N_DATA_HASHMAP_GROUP_LOAD_LIMIT && group_count * N_DATA_HASHMAP_GROUP_LOAD_LIMIT < capacity) {
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(group_count <= UINT_LARGE_MAX_VAL / 2U, "hashmap capacity overflow");
    group_count *= 2U;
  }
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(group_count <= UINT_LARGE_MAX_VAL / N_DATA_HASHMAP_GROUP_SLOT_COUNT, "hashmap slot count overflow");
  return group_count;
}

static void n_data_hashmap_storage_initialize(NexusDataHashMap *hashmap, uint_large group_count) {
  uint_large metadata_size;
  uint_large entry_count;
  uint_large entries_size;

  metadata_size = group_count * N_DATA_HASHMAP_GROUP_SLOT_COUNT;
  entry_count   = group_count * N_DATA_HASHMAP_GROUP_SLOT_COUNT;
  entries_size  = n_data_allocation_size_get(hashmap->pool == NULL ? hashmap->entry_stride : NEXUS_SIZEOF(void *), entry_count);

  hashmap->metadata = (byte *)nexus_memory_allocate(metadata_size);
  hashmap->entries  = (byte *)nexus_memory_allocate(entries_size);
  nexus_memory_bytes_set(hashmap->metadata, metadata_size, N_DATA_HASHMAP_EMPTY);

  hashmap->group_count   = group_count;
  hashmap->length        = 0;
  hashmap->deleted_count = 0;
}

static NDataHashMapProbe n_data_hashmap_probe(const NexusDataHashMap *hashmap, const void *key) {
  NDataHashMapProbe result;
  uint64            hash;
  uint_large        group;
  uint_large        groups_scanned;
  uint_large        first_deleted;

  hash               = hashmap->hash(key, hashmap->key_size, hashmap->seed, hashmap->user_data);
  result.fingerprint = (uint8)(hash & 0x7fU);
  group              = (uint_large)((hash >> 7U) & (uint64)(hashmap->group_count - 1U));
  first_deleted      = UINT_LARGE_MAX_VAL;

  for (groups_scanned = 0; groups_scanned < hashmap->group_count; groups_scanned++) {
    uint_large group_slot;

    for (group_slot = 0; group_slot < N_DATA_HASHMAP_GROUP_SLOT_COUNT; group_slot++) {
      uint_large slot;
      uint8      control;

      slot    = (group * N_DATA_HASHMAP_GROUP_SLOT_COUNT) + group_slot;
      control = hashmap->metadata[slot];

      if (control == result.fingerprint && hashmap->equals(n_data_hashmap_key_at_const(hashmap, slot), key, hashmap->key_size, hashmap->user_data)) {
        result.slot       = slot;
        result.found      = TRUE;
        result.insertable = TRUE;
        return result;
      }

      if (control == N_DATA_HASHMAP_DELETED && first_deleted == UINT_LARGE_MAX_VAL) {
        first_deleted = slot;
      }

      if (control == N_DATA_HASHMAP_EMPTY) {
        result.slot       = first_deleted != UINT_LARGE_MAX_VAL ? first_deleted : slot;
        result.found      = FALSE;
        result.insertable = TRUE;
        return result;
      }
    }

    group = (group + 1U) & (hashmap->group_count - 1U);
  }

  result.slot       = first_deleted;
  result.found      = FALSE;
  result.insertable = first_deleted != UINT_LARGE_MAX_VAL;
  return result;
}

static void *n_data_hashmap_insert_without_rebuild(NexusDataHashMap *hashmap, const void *key, boolean *out_inserted) {
  NDataHashMapProbe probe;

  probe = n_data_hashmap_probe(hashmap, key);
  NEXUS_PROGRAM_INVARIANT(probe.insertable, "hashmap has no insertion slot below its load limit");

  if (probe.found) {
    if (out_inserted != NULL) {
      *out_inserted = FALSE;
    }
    return n_data_hashmap_value_at(hashmap, probe.slot);
  }

  if (hashmap->metadata[probe.slot] == N_DATA_HASHMAP_DELETED) {
    hashmap->deleted_count--;
  }

  if (hashmap->pool != NULL) {
    ((void **)hashmap->entries)[probe.slot] = n_data_slab_pool_take(hashmap->pool);
  }
  nexus_memory_bytes_copy(key, n_data_hashmap_key_at(hashmap, probe.slot), hashmap->key_size);
  hashmap->metadata[probe.slot] = probe.fingerprint;
  hashmap->length++;
  hashmap->version++;

  if (out_inserted != NULL) {
    *out_inserted = TRUE;
  }
  return n_data_hashmap_value_at(hashmap, probe.slot);
}

static void n_data_hashmap_rebuild(NexusDataHashMap *hashmap, uint_large new_group_count) {
  NexusDataHashMap rebuilt;
  uint_large       slot;
  uint_large       old_slot_count;

  rebuilt = *hashmap;
  n_data_hashmap_storage_initialize(&rebuilt, new_group_count);
  old_slot_count = n_data_hashmap_slot_count_get(hashmap);

  for (slot = 0; slot < old_slot_count; slot++) {
    uint8 control;

    control = hashmap->metadata[slot];
    if (control != N_DATA_HASHMAP_EMPTY && control != N_DATA_HASHMAP_DELETED) {
      void *new_value;

      if (hashmap->pool != NULL) {
        NDataHashMapProbe probe;
        /* Rehash references, never reinsert/copy the slab-owned payload. */
        probe = n_data_hashmap_probe(&rebuilt, n_data_hashmap_key_at(hashmap, slot));
        NEXUS_PROGRAM_INVARIANT(probe.insertable && !probe.found, "slabbed hashmap rebuild has no unique slot");
        ((void **)rebuilt.entries)[probe.slot] = n_data_hashmap_key_at(hashmap, slot);
        rebuilt.metadata[probe.slot]           = probe.fingerprint;
        rebuilt.length++;
      } else {
        new_value = n_data_hashmap_insert_without_rebuild(&rebuilt, n_data_hashmap_key_at(hashmap, slot), NULL);
        nexus_memory_bytes_copy(n_data_hashmap_value_at(hashmap, slot), new_value, hashmap->value_size);
      }
    }
  }

  nexus_memory_deallocate(hashmap->metadata);
  nexus_memory_deallocate(hashmap->entries);

  rebuilt.version = hashmap->version + 1U;
  *hashmap        = rebuilt;
}

void nexus_data_hashmap_config_initialize(NexusDataHashMapConfig *config, uint_large key_size, uint_large value_size) {
  NEXUS_PROGRAM_PRECONDITION(config != NULL, "data_hashmap_config_initialize: config is NULL");
  NEXUS_PROGRAM_PRECONDITION(key_size > 0, "data_hashmap_config_initialize: key_size must be greater than zero");
  NEXUS_PROGRAM_PRECONDITION(value_size > 0, "data_hashmap_config_initialize: value_size must be greater than zero");

  config->key_size         = key_size;
  config->value_size       = value_size;
  config->initial_capacity = 0;
  config->seed             = 0;
  config->seed_is_set      = FALSE;
  config->hash             = n_data_hashmap_hash_bytes;
  config->equals           = n_data_hashmap_equals_bytes;
  config->user_data        = NULL;
}

NexusError *nexus_data_hashmap_create(const NexusDataHashMapConfig *config, NexusDataHashMap **out_hashmap) {
  NexusDataHashMap *hashmap;
  NexusError       *error;
  uint_large        alignment;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_data_hashmap_create");
  NEXUS_PROGRAM_PRECONDITION(config != NULL, "data_hashmap_create: config is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_hashmap != NULL, "data_hashmap_create: out_hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(config->key_size > 0, "data_hashmap_create: key_size must be greater than zero");
  NEXUS_PROGRAM_PRECONDITION(config->value_size > 0, "data_hashmap_create: value_size must be greater than zero");
  NEXUS_PROGRAM_PRECONDITION(config->hash != NULL, "data_hashmap_create: hash is NULL");
  NEXUS_PROGRAM_PRECONDITION(config->equals != NULL, "data_hashmap_create: equals is NULL");

  *out_hashmap = NULL;
  hashmap      = (NexusDataHashMap *)nexus_memory_allocate(NEXUS_SIZEOF(*hashmap));

  hashmap->pool         = NULL;
  hashmap->key_size     = config->key_size;
  hashmap->value_size   = config->value_size;
  alignment             = (uint_large)NEXUS_ALIGNOF(NDataMaximumAlignment);
  hashmap->value_offset = n_data_align_up(hashmap->key_size, alignment);
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(hashmap->value_offset <= UINT_LARGE_MAX_VAL - hashmap->value_size, "hashmap entry size overflow");
  hashmap->entry_stride = n_data_align_up(hashmap->value_offset + hashmap->value_size, alignment);
  hashmap->version      = 1U;
  hashmap->hash         = config->hash;
  hashmap->equals       = config->equals;
  hashmap->user_data    = config->user_data;

  if (config->seed_is_set) {
    hashmap->seed = config->seed;
  } else {
    error = nexus_random_system_fill(NULL, (byte *)&hashmap->seed, NEXUS_SIZEOF(hashmap->seed));
    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      nexus_memory_deallocate(hashmap);
      error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  }

  n_data_hashmap_storage_initialize(hashmap, n_data_hashmap_group_count_for_capacity(config->initial_capacity));
  *out_hashmap = hashmap;

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void nexus_data_hashmap_destroy(NexusDataHashMap *hashmap) {
  if (hashmap == NULL) {
    return;
  }
  nexus_memory_deallocate(hashmap->metadata);
  nexus_memory_deallocate(hashmap->entries);
  nexus_memory_deallocate(hashmap);
}

uint_large nexus_data_hashmap_length_get(const NexusDataHashMap *hashmap) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_length_get: hashmap is NULL");
  return hashmap->length;
}

uint_large nexus_data_hashmap_capacity_get(const NexusDataHashMap *hashmap) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_capacity_get: hashmap is NULL");
  return hashmap->group_count * N_DATA_HASHMAP_GROUP_LOAD_LIMIT;
}

void nexus_data_hashmap_reserve(NexusDataHashMap *hashmap, uint_large minimum_capacity) {
  uint_large group_count;

  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_reserve: hashmap is NULL");
  group_count = n_data_hashmap_group_count_for_capacity(minimum_capacity);
  if (group_count > hashmap->group_count) {
    n_data_hashmap_rebuild(hashmap, group_count);
  }
}

void *nexus_data_hashmap_insert(NexusDataHashMap *hashmap, const void *key, boolean *out_inserted) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_insert: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(key != NULL, "data_hashmap_insert: key is NULL");

  NEXUS_PROGRAM_PRECONDITION_ALWAYS(
      hashmap->deleted_count < UINT_LARGE_MAX_VAL && hashmap->length <= UINT_LARGE_MAX_VAL - hashmap->deleted_count - 1U, "hashmap length overflow");
  if (hashmap->length + hashmap->deleted_count + 1U > hashmap->group_count * N_DATA_HASHMAP_GROUP_LOAD_LIMIT) {
    uint_large new_group_count;

    new_group_count = hashmap->deleted_count > hashmap->length ? hashmap->group_count : hashmap->group_count * 2U;
    n_data_hashmap_rebuild(hashmap, new_group_count);
  }

  return n_data_hashmap_insert_without_rebuild(hashmap, key, out_inserted);
}

void nexus_data_hashmap_put(NexusDataHashMap *hashmap, const void *key, const void *value) {
  void *stored_value;

  NEXUS_PROGRAM_PRECONDITION(value != NULL, "data_hashmap_put: value is NULL");
  stored_value = nexus_data_hashmap_insert(hashmap, key, NULL);
  if (stored_value != value) {
    nexus_memory_bytes_copy(value, stored_value, hashmap->value_size);
  }
}

void *nexus_data_hashmap_get(NexusDataHashMap *hashmap, const void *key) {
  NDataHashMapProbe probe;

  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_get: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(key != NULL, "data_hashmap_get: key is NULL");

  probe = n_data_hashmap_probe(hashmap, key);
  return probe.found ? n_data_hashmap_value_at(hashmap, probe.slot) : NULL;
}

const void *nexus_data_hashmap_get_const(const NexusDataHashMap *hashmap, const void *key) {
  NDataHashMapProbe probe;

  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_get_const: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(key != NULL, "data_hashmap_get_const: key is NULL");

  probe = n_data_hashmap_probe(hashmap, key);
  return probe.found ? n_data_hashmap_value_at_const(hashmap, probe.slot) : NULL;
}

boolean nexus_data_hashmap_get_copy(const NexusDataHashMap *hashmap, const void *key, void *out_value) {
  const void *stored_value;

  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "data_hashmap_get_copy: out_value is NULL");
  stored_value = nexus_data_hashmap_get_const(hashmap, key);
  if (stored_value == NULL) {
    return FALSE;
  }
  nexus_memory_bytes_copy(stored_value, out_value, hashmap->value_size);
  return TRUE;
}

boolean nexus_data_hashmap_contains(const NexusDataHashMap *hashmap, const void *key) {
  return nexus_data_hashmap_get_const(hashmap, key) != NULL;
}

boolean nexus_data_hashmap_remove(NexusDataHashMap *hashmap, const void *key, void *out_value) {
  NDataHashMapProbe probe;

  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_remove: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(key != NULL, "data_hashmap_remove: key is NULL");

  probe = n_data_hashmap_probe(hashmap, key);
  if (!probe.found) {
    return FALSE;
  }

  if (out_value != NULL) {
    nexus_memory_bytes_copy(n_data_hashmap_value_at(hashmap, probe.slot), out_value, hashmap->value_size);
  }

  if (hashmap->pool != NULL) {
    n_data_slab_pool_release(hashmap->pool, n_data_hashmap_key_at(hashmap, probe.slot));
  }
  hashmap->metadata[probe.slot] = N_DATA_HASHMAP_DELETED;
  hashmap->length--;
  hashmap->deleted_count++;
  hashmap->version++;
  return TRUE;
}

void nexus_data_hashmap_put_pointer(NexusDataHashMap *hashmap, const void *key, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_put_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->value_size == NEXUS_SIZEOF(void *), "data_hashmap_put_pointer: value_size is not pointer-sized");
  nexus_data_hashmap_put(hashmap, key, (const void *)&pointer);
}

void *nexus_data_hashmap_get_pointer(const NexusDataHashMap *hashmap, const void *key) {
  const void *slot;
  void       *pointer;

  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_get_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->value_size == NEXUS_SIZEOF(void *), "data_hashmap_get_pointer: value_size is not pointer-sized");
  slot = nexus_data_hashmap_get_const(hashmap, key);
  if (slot == NULL) {
    return NULL;
  }
  nexus_memory_bytes_copy(slot, (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

boolean nexus_data_hashmap_get_copy_pointer(const NexusDataHashMap *hashmap, const void *key, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_get_copy_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->value_size == NEXUS_SIZEOF(void *), "data_hashmap_get_copy_pointer: value_size is not pointer-sized");
  return nexus_data_hashmap_get_copy(hashmap, key, (void *)out_pointer);
}

boolean nexus_data_hashmap_remove_pointer(NexusDataHashMap *hashmap, const void *key, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_remove_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->value_size == NEXUS_SIZEOF(void *), "data_hashmap_remove_pointer: value_size is not pointer-sized");
  return nexus_data_hashmap_remove(hashmap, key, (void *)out_pointer);
}

void nexus_data_hashmap_clear(NexusDataHashMap *hashmap) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_clear: hashmap is NULL");

  if (hashmap->pool != NULL) {
    uint_large slot;
    uint_large slot_count;
    slot_count = n_data_hashmap_slot_count_get(hashmap);
    for (slot = 0; slot < slot_count; slot++) {
      if (hashmap->metadata[slot] != N_DATA_HASHMAP_EMPTY && hashmap->metadata[slot] != N_DATA_HASHMAP_DELETED) {
        n_data_slab_pool_release(hashmap->pool, n_data_hashmap_key_at(hashmap, slot));
      }
    }
  }
  nexus_memory_bytes_set(hashmap->metadata, n_data_hashmap_slot_count_get(hashmap), N_DATA_HASHMAP_EMPTY);
  hashmap->length        = 0;
  hashmap->deleted_count = 0;
  hashmap->version++;
}

void nexus_data_hashmap_enumerator_initialize(NexusDataHashMap *hashmap, NexusDataHashMapEnumerator *enumerator) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_hashmap_enumerator_initialize: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(enumerator != NULL, "data_hashmap_enumerator_initialize: enumerator is NULL");

  enumerator->hashmap    = hashmap;
  enumerator->slot_index = 0;
  enumerator->version    = hashmap->version;
}

boolean nexus_data_hashmap_enumerator_next(NexusDataHashMapEnumerator *enumerator, void **out_key, void **out_value) {
  uint_large slot_count;

  NEXUS_PROGRAM_PRECONDITION(enumerator != NULL, "data_hashmap_enumerator_next: enumerator is NULL");
  NEXUS_PROGRAM_PRECONDITION(enumerator->hashmap != NULL, "data_hashmap_enumerator_next: enumerator has no hashmap");
  NEXUS_PROGRAM_PRECONDITION(enumerator->version == enumerator->hashmap->version, "data_hashmap_enumerator_next: hashmap changed during enumeration");

  slot_count = n_data_hashmap_slot_count_get(enumerator->hashmap);
  while (enumerator->slot_index < slot_count) {
    uint_large slot;
    uint8      control;

    slot = enumerator->slot_index;
    enumerator->slot_index++;
    control = enumerator->hashmap->metadata[slot];

    if (control != N_DATA_HASHMAP_EMPTY && control != N_DATA_HASHMAP_DELETED) {
      if (out_key != NULL) {
        *out_key = n_data_hashmap_key_at(enumerator->hashmap, slot);
      }
      if (out_value != NULL) {
        *out_value = n_data_hashmap_value_at(enumerator->hashmap, slot);
      }
      return TRUE;
    }
  }
  return FALSE;
}

/* ---------------------------------------------------------------------------- */
/* SLABBED HASHMAP                                                              */
/* ---------------------------------------------------------------------------- */

struct NexusDataSlabbedHashMap {
  NexusDataHashMap table;
  NDataSlabPool    pool;
};

NexusError *nexus_data_slabbed_hashmap_create(const NexusDataHashMapConfig *config, uint_large slab_capacity, NexusDataSlabbedHashMap **out_hashmap) {
  NexusDataSlabbedHashMap *hashmap;
  NexusDataHashMap        *table;
  NexusError              *error;
  uint_large               alignment;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_data_slabbed_hashmap_create");
  NEXUS_PROGRAM_PRECONDITION(config != NULL, "data_slabbed_hashmap_create: config is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_hashmap != NULL, "data_slabbed_hashmap_create: out_hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(config->key_size > 0, "data_slabbed_hashmap_create: key_size is zero");
  NEXUS_PROGRAM_PRECONDITION(config->value_size > 0, "data_slabbed_hashmap_create: value_size is zero");
  NEXUS_PROGRAM_PRECONDITION(config->hash != NULL, "data_slabbed_hashmap_create: hash is NULL");
  NEXUS_PROGRAM_PRECONDITION(config->equals != NULL, "data_slabbed_hashmap_create: equals is NULL");

  *out_hashmap        = NULL;
  hashmap             = (NexusDataSlabbedHashMap *)nexus_memory_allocate(NEXUS_SIZEOF(*hashmap));
  table               = &hashmap->table;
  table->pool         = &hashmap->pool;
  table->key_size     = config->key_size;
  table->value_size   = config->value_size;
  alignment           = (uint_large)NEXUS_ALIGNOF(NDataMaximumAlignment);
  table->value_offset = n_data_align_up(table->key_size, alignment);
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(table->value_offset <= UINT_LARGE_MAX_VAL - table->value_size, "slabbed hashmap entry size overflow");
  table->entry_stride = n_data_align_up(table->value_offset + table->value_size, alignment);
  table->version      = 1U;
  table->hash         = config->hash;
  table->equals       = config->equals;
  table->user_data    = config->user_data;

  if (config->seed_is_set) {
    table->seed = config->seed;
  } else {
    error = nexus_random_system_fill(NULL, (byte *)&table->seed, NEXUS_SIZEOF(table->seed));
    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      nexus_memory_deallocate(hashmap);
      error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  }
  n_data_slab_pool_initialize(&hashmap->pool, table->entry_stride, slab_capacity);
  n_data_slab_pool_reserve(&hashmap->pool, config->initial_capacity);
  n_data_hashmap_storage_initialize(table, n_data_hashmap_group_count_for_capacity(config->initial_capacity));
  *out_hashmap = hashmap;
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void nexus_data_slabbed_hashmap_destroy(NexusDataSlabbedHashMap *hashmap) {
  if (hashmap == NULL) {
    return;
  }
  nexus_memory_deallocate(hashmap->table.metadata);
  nexus_memory_deallocate(hashmap->table.entries);
  n_data_slab_pool_destroy(&hashmap->pool);
  nexus_memory_deallocate(hashmap);
}

uint_large nexus_data_slabbed_hashmap_length_get(const NexusDataSlabbedHashMap *hashmap) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_length_get: hashmap is NULL");
  return hashmap->table.length;
}

uint_large nexus_data_slabbed_hashmap_capacity_get(const NexusDataSlabbedHashMap *hashmap) {
  uint_large table_capacity;
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_capacity_get: hashmap is NULL");
  table_capacity = hashmap->table.group_count * N_DATA_HASHMAP_GROUP_LOAD_LIMIT;
  return hashmap->pool.capacity < table_capacity ? hashmap->pool.capacity : table_capacity;
}

void nexus_data_slabbed_hashmap_reserve(NexusDataSlabbedHashMap *hashmap, uint_large minimum_capacity) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_reserve: hashmap is NULL");
  n_data_slab_pool_reserve(&hashmap->pool, minimum_capacity);
  nexus_data_hashmap_reserve(&hashmap->table, minimum_capacity);
}

void *nexus_data_slabbed_hashmap_insert(NexusDataSlabbedHashMap *hashmap, const void *key, boolean *out_inserted) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_insert: hashmap is NULL");
  return nexus_data_hashmap_insert(&hashmap->table, key, out_inserted);
}

void nexus_data_slabbed_hashmap_put(NexusDataSlabbedHashMap *hashmap, const void *key, const void *value) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_put: hashmap is NULL");
  nexus_data_hashmap_put(&hashmap->table, key, value);
}

void *nexus_data_slabbed_hashmap_get(NexusDataSlabbedHashMap *hashmap, const void *key) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_get: hashmap is NULL");
  return nexus_data_hashmap_get(&hashmap->table, key);
}

const void *nexus_data_slabbed_hashmap_get_const(const NexusDataSlabbedHashMap *hashmap, const void *key) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_get_const: hashmap is NULL");
  return nexus_data_hashmap_get_const(&hashmap->table, key);
}

boolean nexus_data_slabbed_hashmap_get_copy(const NexusDataSlabbedHashMap *hashmap, const void *key, void *out_value) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_get_copy: hashmap is NULL");
  return nexus_data_hashmap_get_copy(&hashmap->table, key, out_value);
}

boolean nexus_data_slabbed_hashmap_contains(const NexusDataSlabbedHashMap *hashmap, const void *key) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_contains: hashmap is NULL");
  return nexus_data_hashmap_contains(&hashmap->table, key);
}

boolean nexus_data_slabbed_hashmap_remove(NexusDataSlabbedHashMap *hashmap, const void *key, void *out_value) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_remove: hashmap is NULL");
  return nexus_data_hashmap_remove(&hashmap->table, key, out_value);
}

void nexus_data_slabbed_hashmap_put_pointer(NexusDataSlabbedHashMap *hashmap, const void *key, const void *pointer) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_put_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->table.value_size == NEXUS_SIZEOF(void *), "data_slabbed_hashmap_put_pointer: value_size is not pointer-sized");
  nexus_data_slabbed_hashmap_put(hashmap, key, (const void *)&pointer);
}

void *nexus_data_slabbed_hashmap_get_pointer(const NexusDataSlabbedHashMap *hashmap, const void *key) {
  const void *slot;
  void       *pointer;

  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_get_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->table.value_size == NEXUS_SIZEOF(void *), "data_slabbed_hashmap_get_pointer: value_size is not pointer-sized");
  slot = nexus_data_slabbed_hashmap_get_const(hashmap, key);
  if (slot == NULL) {
    return NULL;
  }
  nexus_memory_bytes_copy(slot, (void *)&pointer, NEXUS_SIZEOF(pointer));
  return pointer;
}

boolean nexus_data_slabbed_hashmap_get_copy_pointer(const NexusDataSlabbedHashMap *hashmap, const void *key, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_get_copy_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->table.value_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_hashmap_get_copy_pointer: value_size is not pointer-sized");
  return nexus_data_slabbed_hashmap_get_copy(hashmap, key, (void *)out_pointer);
}

boolean nexus_data_slabbed_hashmap_remove_pointer(NexusDataSlabbedHashMap *hashmap, const void *key, void **out_pointer) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_remove_pointer: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(hashmap->table.value_size == NEXUS_SIZEOF(void *),
                             "data_slabbed_hashmap_remove_pointer: value_size is not pointer-sized");
  return nexus_data_slabbed_hashmap_remove(hashmap, key, (void *)out_pointer);
}

void nexus_data_slabbed_hashmap_clear(NexusDataSlabbedHashMap *hashmap) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_clear: hashmap is NULL");
  nexus_data_hashmap_clear(&hashmap->table);
}

void nexus_data_slabbed_hashmap_enumerator_initialize(NexusDataSlabbedHashMap *hashmap, NexusDataSlabbedHashMapEnumerator *enumerator) {
  NEXUS_PROGRAM_PRECONDITION(hashmap != NULL, "data_slabbed_hashmap_enumerator_initialize: hashmap is NULL");
  NEXUS_PROGRAM_PRECONDITION(enumerator != NULL, "data_slabbed_hashmap_enumerator_initialize: enumerator is NULL");
  enumerator->hashmap    = hashmap;
  enumerator->slot_index = 0;
  enumerator->version    = hashmap->table.version;
}

boolean nexus_data_slabbed_hashmap_enumerator_next(NexusDataSlabbedHashMapEnumerator *enumerator, const void **out_key, void **out_value) {
  NexusDataHashMapEnumerator cursor;
  void                      *key;
  boolean                    found;
  NEXUS_PROGRAM_PRECONDITION(enumerator != NULL, "data_slabbed_hashmap_enumerator_next: enumerator is NULL");
  NEXUS_PROGRAM_PRECONDITION(enumerator->hashmap != NULL, "data_slabbed_hashmap_enumerator_next: enumerator has no hashmap");
  cursor.hashmap         = &enumerator->hashmap->table;
  cursor.slot_index      = enumerator->slot_index;
  cursor.version         = enumerator->version;
  found                  = nexus_data_hashmap_enumerator_next(&cursor, &key, out_value);
  enumerator->slot_index = cursor.slot_index;
  if (found && out_key != NULL) {
    *out_key = key;
  }
  return found;
}

/* ---------------------------------------------------------------------------- */
/* CHUNKED LIST AND WORD MAP                                                     */
/* ---------------------------------------------------------------------------- */

#define N_DATA_CHUNKED_LIST_SHIFT_MAX 23U
#define N_DATA_WORD_MAP_WORDS_MAX     4U

void nexus_data_chunked_list_initialize(NexusDataChunkedList *list, uint_large element_size, uint32 chunk_shift) {
  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_initialize: list is NULL");
  NEXUS_PROGRAM_PRECONDITION(element_size > 0, "data_chunked_list_initialize: element_size is zero");
  NEXUS_PROGRAM_PRECONDITION(chunk_shift >= 1U && chunk_shift <= N_DATA_CHUNKED_LIST_SHIFT_MAX,
                             "data_chunked_list_initialize: chunk_shift out of range");

  list->chunks         = NULL;
  list->chunk_count    = 0;
  list->chunk_capacity = 0;
  list->length         = 0;
  list->element_size   = element_size;
  list->mask           = ((uint_large)1 << chunk_shift) - 1;
  list->shift          = chunk_shift;
}

void nexus_data_chunked_list_release(NexusDataChunkedList *list) {
  uint_large index;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_release: list is NULL");

  for (index = 0; index < list->chunk_count; index++) {
    nexus_memory_deallocate(list->chunks[index]);
  }

  if (list->chunks != NULL) {
    nexus_memory_deallocate((void *)list->chunks);
  }

  list->chunks         = NULL;
  list->chunk_count    = 0;
  list->chunk_capacity = 0;
  list->length         = 0;
}

void nexus_data_chunked_list_chunk_add(NexusDataChunkedList *list) {
  byte     **grown;
  uint_large index;

  NEXUS_PROGRAM_PRECONDITION(list != NULL, "data_chunked_list_chunk_add: list is NULL");

  if (list->chunk_count == list->chunk_capacity) {
    list->chunk_capacity = list->chunk_capacity == 0 ? 16 : list->chunk_capacity * 2;
    grown                = (byte **)nexus_memory_allocate(list->chunk_capacity * NEXUS_SIZEOF(*grown));

    for (index = 0; index < list->chunk_count; index++) {
      grown[index] = list->chunks[index];
    }

    if (list->chunks != NULL) {
      nexus_memory_deallocate((void *)list->chunks);
    }

    list->chunks = grown;
  }

  list->chunks[list->chunk_count] = (byte *)nexus_memory_allocate(((uint_large)1 << list->shift) * list->element_size);
  list->chunk_count++;
}

struct NexusDataWordMap {
  uint_large *slots; /* capacity slots of slot_words words: the epoch stamp, the key, then the value. */
  uint_large  capacity;
  uint_large  count;
  uint_large  epoch; /* A slot is live while its stamp equals this. */
  uint32      key_words;
  uint32      value_words;
  uint32      slot_words;
};

static uint_large n_data_word_map_hash(const uint_large *key, uint32 key_words) {
  static const uint64 multipliers[N_DATA_WORD_MAP_WORDS_MAX] = {
      (((uint64)0x9E3779B9UL << 32) | (uint64)0x7F4A7C15UL), (((uint64)0xBF58476DUL << 32) | (uint64)0x1CE4E5B9UL),
      (((uint64)0x94D049BBUL << 32) | (uint64)0x133111EBUL), (((uint64)0xD6E8FEB8UL << 32) | (uint64)0x6659E871UL)};
  uint64 hash;
  uint32 word;

  hash = 0;

  for (word = 0; word < key_words; word++) {
    hash += (uint64)key[word] * multipliers[word];
    hash ^= hash >> (word % 2U == 0U ? 29U : 32U);
  }

  return (uint_large)hash;
}

NexusDataWordMap *nexus_data_word_map_create(uint32 key_words, uint32 value_words, uint_large initial_capacity) {
  NexusDataWordMap *map;
  uint_large        capacity;

  NEXUS_PROGRAM_PRECONDITION(key_words >= 1U && key_words <= N_DATA_WORD_MAP_WORDS_MAX, "data_word_map_create: key_words out of range");
  NEXUS_PROGRAM_PRECONDITION(value_words >= 1U && value_words <= N_DATA_WORD_MAP_WORDS_MAX, "data_word_map_create: value_words out of range");

  /* Half the slots are kept free so probing always meets an unused slot. */
  capacity = 16;

  while (capacity < initial_capacity * 2) {
    capacity *= 2;
  }

  map              = (NexusDataWordMap *)nexus_memory_allocate(NEXUS_SIZEOF(*map));
  map->key_words   = key_words;
  map->value_words = value_words;
  map->slot_words  = 1U + key_words + value_words;
  map->capacity    = capacity;
  map->count       = 0;
  map->epoch       = 1;
  map->slots       = (uint_large *)nexus_memory_allocate(capacity * map->slot_words * NEXUS_SIZEOF(uint_large));
  nexus_memory_bytes_clear(map->slots, capacity * map->slot_words * NEXUS_SIZEOF(uint_large));

  return map;
}

void nexus_data_word_map_destroy(NexusDataWordMap *map) {
  if (map == NULL) {
    return;
  }

  nexus_memory_deallocate(map->slots);
  nexus_memory_deallocate(map);
}

void nexus_data_word_map_clear(NexusDataWordMap *map) {
  NEXUS_PROGRAM_PRECONDITION(map != NULL, "data_word_map_clear: map is NULL");

  map->epoch++;
  map->count = 0;
}

uint_large nexus_data_word_map_length_get(const NexusDataWordMap *map) {
  NEXUS_PROGRAM_PRECONDITION(map != NULL, "data_word_map_length_get: map is NULL");

  return map->count;
}

uint_large *nexus_data_word_map_get(const NexusDataWordMap *map, const uint_large *key) {
  uint_large *slot;
  uint_large  mask;
  uint_large  index;
  uint32      word;

  NEXUS_PROGRAM_PRECONDITION(map != NULL && key != NULL, "data_word_map_get: map or key is NULL");

  mask  = map->capacity - 1;
  index = n_data_word_map_hash(key, map->key_words) & mask;

  for (;;) {
    slot = map->slots + (index * map->slot_words);

    if (slot[0] != map->epoch) {
      return NULL;
    }

    for (word = 0; word < map->key_words && slot[1U + word] == key[word]; word++) {
    }

    if (word == map->key_words) {
      return slot + 1U + map->key_words;
    }

    index = (index + 1) & mask;
  }
}

uint_large *nexus_data_word_map_insert(NexusDataWordMap *map, const uint_large *key) {
  uint_large *grown;
  uint_large *slot;
  uint_large  capacity;
  uint_large  mask;
  uint_large  index;
  uint_large  previous;
  uint32      word;

  NEXUS_PROGRAM_PRECONDITION(map != NULL && key != NULL, "data_word_map_insert: map or key is NULL");
  NEXUS_PROGRAM_PRECONDITION(nexus_data_word_map_get(map, key) == NULL, "data_word_map_insert: key is already present");

  if ((map->count + 1) * 2 > map->capacity) {
    capacity = map->capacity * 2;
    grown    = (uint_large *)nexus_memory_allocate(capacity * map->slot_words * NEXUS_SIZEOF(uint_large));
    nexus_memory_bytes_clear(grown, capacity * map->slot_words * NEXUS_SIZEOF(uint_large));
    mask = capacity - 1;

    for (previous = 0; previous < map->capacity; previous++) {
      slot = map->slots + (previous * map->slot_words);

      if (slot[0] != map->epoch) {
        continue;
      }

      index = n_data_word_map_hash(slot + 1, map->key_words) & mask;

      while (grown[index * map->slot_words] == map->epoch) {
        index = (index + 1) & mask;
      }

      for (word = 0; word < map->slot_words; word++) {
        grown[(index * map->slot_words) + word] = slot[word];
      }
    }

    nexus_memory_deallocate(map->slots);
    map->slots    = grown;
    map->capacity = capacity;
  }

  mask  = map->capacity - 1;
  index = n_data_word_map_hash(key, map->key_words) & mask;

  while (map->slots[index * map->slot_words] == map->epoch) {
    index = (index + 1) & mask;
  }

  slot    = map->slots + (index * map->slot_words);
  slot[0] = map->epoch;

  for (word = 0; word < map->key_words; word++) {
    slot[1U + word] = key[word];
  }

  map->count++;

  return slot + 1U + map->key_words;
}

/* ---------------------------------------------------------------------------- */
/* ARENA                                                                         */
/* ---------------------------------------------------------------------------- */

#define NEXUS_DATA_ARENA_BLOCK_SIZE ((uint_large)64 * 1024)

void nexus_data_arena_initialize(NexusDataArena *arena) {
  NEXUS_PROGRAM_PRECONDITION(arena != NULL, "arena must not be NULL");

  arena->blocks    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  arena->cursor    = NULL;
  arena->remaining = 0;
}

void nexus_data_arena_release(NexusDataArena *arena) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(arena != NULL && arena->blocks != NULL, "arena must be initialized");

  for (i = 0; i < nexus_data_list_length_get(arena->blocks); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(arena->blocks, i));
  }

  nexus_data_list_destroy(arena->blocks);
  arena->blocks    = NULL;
  arena->cursor    = NULL;
  arena->remaining = 0;
}

void *nexus_data_arena_allocate(NexusDataArena *arena, uint_large size) {
  byte      *block;
  void      *result;
  uint_large block_size;

  NEXUS_PROGRAM_PRECONDITION(arena != NULL && arena->blocks != NULL, "arena must be initialized");

  size = (size + 7) & ~(uint_large)7;

  if (size > arena->remaining) {
    block_size = size > NEXUS_DATA_ARENA_BLOCK_SIZE ? size : NEXUS_DATA_ARENA_BLOCK_SIZE;
    block      = (byte *)nexus_memory_allocate(block_size);
    (void)nexus_data_list_append_pointer(arena->blocks, block);
    arena->cursor    = block;
    arena->remaining = block_size;
  }

  result = arena->cursor;
  arena->cursor += size;
  arena->remaining -= size;
  nexus_memory_bytes_clear(result, size);

  return result;
}

char *nexus_data_arena_text_copy(NexusDataArena *arena, const char *text, uint_large size) {
  char *copy;

  copy = (char *)nexus_data_arena_allocate(arena, size + 1);

  if (size > 0) {
    nexus_memory_bytes_copy(text, copy, size);
  }

  copy[size] = '\0';

  return copy;
}
