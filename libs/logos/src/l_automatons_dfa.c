#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "./l_internal.h"

/* State ids are uint16 and the largest value is reserved as "no state", so a DFA holds at most this many states. */
#define L_DFA_STATE_LIMIT ((uint32)UINT16_MAX_VAL)
#define L_DFA_NO_STATE    ((uint16)UINT16_MAX_VAL)

/* State 0 is the initial state and every state is reachable from it. A byte class is a set of bytes that enter the same NFA states, so every
   byte of a class behaves identically in every state and the table needs one column per class instead of 256. */
struct LDFA {
  uint16  state_count;
  uint16  class_count;      /* 1..256. */
  uint16  dead_state;       /* The non-accepting state that only loops onto itself, or L_DFA_NO_STATE when the language has none. */
  byte    byte_class[256];  /* The column of every byte. */
  uint16 *transitions;      /* Row s, column c: the state entered from s on any byte of class c. */
  LBitset accepting_states; /* Over the states. */
  uint16 *accept_tags;      /* Per state: the winning accept tag, or L_ACCEPT_TAG_NONE. accepting_states holds exactly the states with a tag. */
};

/* The sets of NFA states found so far, one per DFA state, interned through an open-addressing hash table. */
typedef struct LDfaBuilder {
  uint_large word_count;  /* Words of one set. */
  uint_large class_count; /* Columns of one transition row. */
  uint_large state_count;
  uint_large capacity;    /* States the three arrays below hold before they grow. */
  uint64    *set_words;   /* Row s: the NFA states that DFA state s stands for. */
  uint32    *set_hashes;  /* Hash of every set, so growing the table never rehashes the words. */
  uint16    *transitions; /* Row s, column c, as in LDFA. Filled when s is processed. */
  uint16    *slots;       /* DFA state id + 1; 0 marks a free slot. */
  uint_large slot_count;  /* A power of two, kept at least twice the state count. */
} LDfaBuilder;

static uint_large l_dfa_accepting_count(const LDFA *dfa) {
  uint_large count;
  uint_large word;

  count = 0;

  for (word = 0; word < dfa->accepting_states.word_count; word++) {
    count += nexus_bits_uint64_popcount(dfa->accepting_states.words[word]);
  }

  return count;
}

/* The only non-accepting state whose every transition returns to itself, if there is one. */
static uint16 l_dfa_dead_state_find(const LDFA *dfa) {
  uint_large state;
  uint_large column;

  for (state = 0; state < dfa->state_count; state++) {
    if (logos_bitset_get(&dfa->accepting_states, (uint16)state)) {
      continue;
    }

    for (column = 0; column < dfa->class_count; column++) {
      if (dfa->transitions[(state * dfa->class_count) + column] != state) {
        break;
      }
    }

    if (column == dfa->class_count) {
      return (uint16)state;
    }
  }

  return L_DFA_NO_STATE;
}

static uint_large l_dfa_table_bytes(const LDFA *dfa) {
  return ((uint_large)dfa->state_count * dfa->class_count * NEXUS_SIZEOF(uint16)) + NEXUS_SIZEOF(dfa->byte_class) +
         ((uint_large)dfa->accepting_states.word_count * NEXUS_SIZEOF(uint64));
}

/* Finds the DFA state of candidate or adds it. Returns FALSE, adding nothing, when a new state would exceed L_DFA_STATE_LIMIT. */
static boolean l_dfa_builder_intern(LDfaBuilder *builder, const uint64 *candidate, uint_large *out_state) {
  uint_large set_bytes;
  uint_large slot;
  uint_large mask;
  uint32     hash;

  set_bytes = builder->word_count * NEXUS_SIZEOF(uint64);
  hash      = (uint32)nexus_hash_xxh3_64(candidate, set_bytes);
  mask      = builder->slot_count - 1;
  slot      = hash & mask;

  while (builder->slots[slot] != 0) {
    uint_large state;

    state = (uint_large)builder->slots[slot] - 1;

    if (builder->set_hashes[state] == hash && nexus_memory_bytes_equal(builder->set_words + (state * builder->word_count), candidate, set_bytes)) {
      *out_state = state;

      return TRUE;
    }

    slot = (slot + 1) & mask;
  }

  if (builder->state_count >= L_DFA_STATE_LIMIT) {
    return FALSE;
  }

  if (builder->state_count == builder->capacity) {
    builder->capacity *= 2;
    builder->set_words   = (uint64 *)nexus_memory_reallocate(builder->set_words, builder->capacity * set_bytes);
    builder->set_hashes  = (uint32 *)nexus_memory_reallocate(builder->set_hashes, builder->capacity * NEXUS_SIZEOF(uint32));
    builder->transitions = (uint16 *)nexus_memory_reallocate(builder->transitions, builder->capacity * builder->class_count * NEXUS_SIZEOF(uint16));
  }

  nexus_memory_bytes_copy(candidate, builder->set_words + (builder->state_count * builder->word_count), set_bytes);
  builder->set_hashes[builder->state_count] = hash;
  builder->slots[slot]                      = (uint16)(builder->state_count + 1);
  *out_state                                = builder->state_count;
  builder->state_count++;

  /* Keep the load at or below one half, so probe sequences stay short. */
  if (builder->state_count * 2 > builder->slot_count) {
    uint16    *old_slots;
    uint_large old_state;

    old_slots = builder->slots;
    builder->slot_count *= 2;
    builder->slots = (uint16 *)nexus_memory_allocate(builder->slot_count * NEXUS_SIZEOF(uint16));
    nexus_memory_bytes_clear(builder->slots, builder->slot_count * NEXUS_SIZEOF(uint16));
    mask = builder->slot_count - 1;

    for (old_state = 0; old_state < builder->state_count; old_state++) {
      slot = builder->set_hashes[old_state] & mask;

      while (builder->slots[slot] != 0) {
        slot = (slot + 1) & mask;
      }

      builder->slots[slot] = (uint16)(old_state + 1);
    }

    nexus_memory_deallocate(old_slots);
  }

  return TRUE;
}

LDFA *logos_automatons_dfa_from_nfa(const LNFA *nfa, EchoContext *context) {
  LDFA       *dfa;
  LDfaBuilder builder;
  LBitset     row;
  LBitset     current_set;
  LBitset     reach;
  LBitset     candidate_set;
  uint64     *scratch_words;
  uint_large  nfa_state_count;
  uint_large  word_count;
  uint_large  class_count;
  uint_large  current;
  uint_large  target;
  boolean     failed;
  uint_large  column;
  byte        byte_class[256];
  byte        class_representative[256];
  char        table_size_string[50];

  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && context != NULL, "nfa and context must not be NULL");

  nfa_state_count = logos_automatons_nfa_state_count_get(nfa);
  word_count      = logos_bitset_word_count_get((uint16)nfa_state_count);

  /* Step 1: byte classes, so the table needs one column per class instead of 256. */
  class_count = logos_automatons_nfa_byte_classes_get(nfa, byte_class, class_representative);

  /* Step 2: subset construction. DFA states are numbered in discovery order and processed in that order, so the sets found so far double as the
     work queue. The set of a state's successors on a class is the NFA's successors of its members, restricted to what the class enters; the
     successors are the same for every class, so they are computed once per state. */
  builder.word_count  = word_count;
  builder.class_count = class_count;
  builder.state_count = 0;
  builder.capacity    = 16;
  builder.set_words   = (uint64 *)nexus_memory_allocate(builder.capacity * word_count * NEXUS_SIZEOF(uint64));
  builder.set_hashes  = (uint32 *)nexus_memory_allocate(builder.capacity * NEXUS_SIZEOF(uint32));
  builder.transitions = (uint16 *)nexus_memory_allocate(builder.capacity * class_count * NEXUS_SIZEOF(uint16));
  builder.slot_count  = 64;
  builder.slots       = (uint16 *)nexus_memory_allocate(builder.slot_count * NEXUS_SIZEOF(uint16));
  nexus_memory_bytes_clear(builder.slots, builder.slot_count * NEXUS_SIZEOF(uint16));

  scratch_words = (uint64 *)nexus_memory_allocate(3 * word_count * NEXUS_SIZEOF(uint64));

  current_set.words        = scratch_words;
  current_set.word_count   = (uint16)word_count;
  reach.words              = scratch_words + word_count;
  reach.word_count         = (uint16)word_count;
  candidate_set.words      = scratch_words + (2 * word_count);
  candidate_set.word_count = (uint16)word_count;
  row.word_count           = (uint16)word_count;

  /* The initial DFA state is the NFA's initial state set. */
  logos_automatons_nfa_initial_set_get(nfa, &candidate_set);
  (void)l_dfa_builder_intern(&builder, candidate_set.words, &target);

  failed = FALSE;

  for (current = 0; current < builder.state_count && !failed; current++) {
    /* Interning below may move the sets, so work from a copy of this one. */
    nexus_memory_bytes_copy(builder.set_words + (current * word_count), current_set.words, word_count * NEXUS_SIZEOF(uint64));
    logos_automatons_nfa_successors_get(nfa, &current_set, &reach);

    for (column = 0; column < class_count; column++) {
      nexus_memory_bytes_copy(reach.words, candidate_set.words, word_count * NEXUS_SIZEOF(uint64));
      (void)logos_automatons_nfa_byte_restrict(nfa, class_representative[column], &candidate_set);

      if (!l_dfa_builder_intern(&builder, candidate_set.words, &target)) {
        failed = TRUE;
        break;
      }

      builder.transitions[(current * class_count) + column] = (uint16)target;
    }
  }

  nexus_memory_deallocate(scratch_words);

  if (failed) {
    (void)echo_error(context, "DFA from NFA: the subset construction needs more than %u states, which a DFA cannot number; no DFA was built.",
                     (unsigned int)L_DFA_STATE_LIMIT);

    nexus_memory_deallocate(builder.slots);
    nexus_memory_deallocate(builder.transitions);
    nexus_memory_deallocate(builder.set_hashes);
    nexus_memory_deallocate(builder.set_words);

    return NULL;
  }

  /* Step 3: the finished automaton, with tables of exactly the final size. */
  dfa              = (LDFA *)nexus_memory_allocate(NEXUS_SIZEOF(*dfa));
  dfa->state_count = (uint16)builder.state_count;
  dfa->class_count = (uint16)class_count;
  dfa->transitions = (uint16 *)nexus_memory_allocate(builder.state_count * class_count * NEXUS_SIZEOF(uint16));
  nexus_memory_bytes_copy(builder.transitions, dfa->transitions, builder.state_count * class_count * NEXUS_SIZEOF(uint16));
  nexus_memory_bytes_copy(byte_class, dfa->byte_class, NEXUS_SIZEOF(byte_class));

  logos_bitset_initialize(&dfa->accepting_states, dfa->state_count);
  dfa->accept_tags = (uint16 *)nexus_memory_allocate(builder.state_count * NEXUS_SIZEOF(uint16));

  for (current = 0; current < builder.state_count; current++) {
    row.words = builder.set_words + (current * word_count);

    dfa->accept_tags[current] = logos_automatons_nfa_set_accept_tag_get(nfa, &row);

    if (dfa->accept_tags[current] != L_ACCEPT_TAG_NONE) {
      logos_bitset_set(&dfa->accepting_states, (uint16)current, TRUE);
    }
  }

  dfa->dead_state = l_dfa_dead_state_find(dfa);

  nexus_memory_deallocate(builder.slots);
  nexus_memory_deallocate(builder.transitions);
  nexus_memory_deallocate(builder.set_hashes);
  nexus_memory_deallocate(builder.set_words);

  (void)nexus_strings_bytes_format(table_size_string, NEXUS_SIZEOF(table_size_string), (uint64)l_dfa_table_bytes(dfa));

  (void)echo_debug(context,
                   "DFA from NFA: %u NFA states became %u DFA states (%u accepting, dead state %s) over %u byte classes of 256, %s of tables.",
                   (unsigned int)nfa_state_count, (unsigned int)dfa->state_count, (unsigned int)l_dfa_accepting_count(dfa),
                   dfa->dead_state == L_DFA_NO_STATE ? "absent" : "present", (unsigned int)dfa->class_count, table_size_string);

  return dfa;
}

void logos_automatons_dfa_destroy(LDFA *dfa) {
  NEXUS_PROGRAM_PRECONDITION(dfa != NULL, "dfa must not be NULL");

  nexus_memory_deallocate(dfa->accept_tags);
  nexus_memory_deallocate(dfa->accepting_states.words);
  nexus_memory_deallocate(dfa->transitions);
  nexus_memory_deallocate(dfa);
}

void logos_automatons_dfa_view_get(const LDFA *dfa, LDFAView *view) {
  NEXUS_PROGRAM_PRECONDITION(dfa != NULL && view != NULL, "dfa and view must not be NULL");

  view->transitions = dfa->transitions;
  view->byte_class  = dfa->byte_class;
  view->accept_tags = dfa->accept_tags;
  view->state_count = dfa->state_count;
  view->class_count = dfa->class_count;
  view->dead_state  = dfa->dead_state;
}

uint16 logos_automatons_dfa_state_count_get(const LDFA *dfa) {
  NEXUS_PROGRAM_PRECONDITION(dfa != NULL, "dfa must not be NULL");

  return dfa->state_count;
}

boolean logos_automatons_dfa_match(const LDFA *dfa, const byte *source, uint_large source_size) {
  const uint16 *transitions;
  uint_large    class_count;
  uint_large    position;
  uint16        dead_state;
  uint16        current_state;

  NEXUS_PROGRAM_PRECONDITION(dfa != NULL && (source != NULL || source_size == 0), "dfa and a nonempty source must not be NULL");

  transitions   = dfa->transitions;
  class_count   = dfa->class_count;
  dead_state    = dfa->dead_state;
  current_state = 0;

  for (position = 0; position < source_size; position++) {
    current_state = transitions[((uint_large)current_state * class_count) + dfa->byte_class[source[position]]];

    if (current_state == dead_state) {
      return FALSE;
    }
  }

  return logos_bitset_get(&dfa->accepting_states, current_state);
}

/* ---------------------------------------------------------------------------- */
/* MINIMIZATION                                                                  */
/* ---------------------------------------------------------------------------- */

/* Hopcroft's partition refinement. The partition lives in one permutation of the states, each block a contiguous range of it, so a split moves
   the marked states to the front of their block instead of building new lists. Blocks only ever split, and the worklist holds (block, symbol)
   pairs whose predecessors may still separate a block. A symbol here is a byte class, which is sound because every byte of a class behaves alike. */
LDFA *logos_automatons_dfa_minimize(const LDFA *dfa, EchoContext *context) {
  LDFA      *minimized;
  uint_large state_count;
  uint_large class_count;
  uint_large block_count;
  uint_large next_place;
  uint_large accepting_count;
  uint32 *
      inverse_offsets; /* Per symbol a, n + 1 offsets: the predecessors of target on a are inverse_states[a * n + offsets[target] .. offsets[target + 1]). */
  uint16         *inverse_states;
  uint32         *cursor;
  uint16         *elements; /* A permutation of the states; every block is a contiguous range of it. */
  uint32         *location; /* Index of every state in elements. */
  uint16         *block_of;
  uint32         *block_first;
  uint32         *block_size;
  uint32         *block_marked; /* Leading states of the block that the current step marked. */
  uint16         *members;      /* Copy of the popped block, because marking permutes the block while it is read. */
  uint16         *touched;      /* Blocks with marked states in the current step. */
  NexusDataStack *worklist;
  uint_large      refinement_steps;
  uint_large      splits;
  uint_large      state;
  uint_large      a;
  uint_large      target;
  uint_large      j;
  char            table_before_string[50];
  char            table_after_string[50];

  NEXUS_PROGRAM_PRECONDITION(dfa != NULL && context != NULL, "dfa and context must not be NULL");
  NEXUS_PROGRAM_INVARIANT(dfa->state_count > 0 && dfa->class_count > 0, "a dfa has a state and a byte class");

  state_count = dfa->state_count;
  class_count = dfa->class_count;

  /* Step 1: the inverse transition relation, as one compressed list per symbol. Every (state, a) pair is exactly one entry, in the list of its target. */
  inverse_offsets = (uint32 *)nexus_memory_allocate(class_count * (state_count + 1) * NEXUS_SIZEOF(uint32));
  inverse_states  = (uint16 *)nexus_memory_allocate(class_count * state_count * NEXUS_SIZEOF(uint16));
  cursor          = (uint32 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint32));
  nexus_memory_bytes_clear(inverse_offsets, class_count * (state_count + 1) * NEXUS_SIZEOF(uint32));

  for (a = 0; a < class_count; a++) {
    uint32 *offsets;

    offsets = inverse_offsets + (a * (state_count + 1));

    for (state = 0; state < state_count; state++) {
      offsets[dfa->transitions[(state * class_count) + a] + 1]++;
    }

    for (target = 0; target < state_count; target++) {
      offsets[target + 1] += offsets[target];
      cursor[target] = offsets[target];
    }

    for (state = 0; state < state_count; state++) {
      target = dfa->transitions[(state * class_count) + a];

      inverse_states[(a * state_count) + cursor[target]] = (uint16)state;
      cursor[target]++;
    }
  }

  nexus_memory_deallocate(cursor);

  /* Step 2: the initial partition, one block per accept tag (the non-accepting states are the tag L_ACCEPT_TAG_NONE); states with different tags
     are already told apart by the empty word. Hopcroft may leave one block out of the worklist, so the largest is the one left out. */
  elements     = (uint16 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint16));
  location     = (uint32 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint32));
  block_of     = (uint16 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint16));
  block_first  = (uint32 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint32));
  block_size   = (uint32 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint32));
  block_marked = (uint32 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint32));
  members      = (uint16 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint16));
  touched      = (uint16 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint16));
  nexus_memory_bytes_clear(block_marked, state_count * NEXUS_SIZEOF(uint32));

  worklist = nexus_data_stack_create(NEXUS_SIZEOF(uint32), 64);

  accepting_count = l_dfa_accepting_count(dfa);

  {
    uint16    *block_of_tag; /* Indexed by tag: the initial block of the tag, or L_DFA_NO_STATE while no state has it. */
    uint32    *fill;         /* Per block: the next free place. */
    uint_large largest;

    block_of_tag = (uint16 *)nexus_memory_allocate(((uint_large)UINT16_MAX_VAL + 1) * NEXUS_SIZEOF(uint16));
    fill         = (uint32 *)nexus_memory_allocate(state_count * NEXUS_SIZEOF(uint32));

    for (j = 0; j <= (uint_large)UINT16_MAX_VAL; j++) {
      block_of_tag[j] = L_DFA_NO_STATE;
    }

    block_count = 0;

    for (state = 0; state < state_count; state++) {
      uint16 tag;

      tag = dfa->accept_tags[state];

      if (block_of_tag[tag] == L_DFA_NO_STATE) {
        block_of_tag[tag]       = (uint16)block_count;
        block_size[block_count] = 0;
        block_count++;
      }

      block_size[block_of_tag[tag]]++;
    }

    next_place = 0;
    largest    = 0;

    for (j = 0; j < block_count; j++) {
      block_first[j] = (uint32)next_place;
      fill[j]        = (uint32)next_place;
      next_place += block_size[j];

      if (block_size[j] > block_size[largest]) {
        largest = j;
      }
    }

    for (state = 0; state < state_count; state++) {
      uint_large place;
      uint16     owner;

      owner = block_of_tag[dfa->accept_tags[state]];
      place = fill[owner]++;

      elements[place] = (uint16)state;
      location[state] = (uint32)place;
    }

    for (j = 0; j < block_count; j++) {
      if (j == largest) {
        continue;
      }

      for (a = 0; a < class_count; a++) {
        uint32 entry;

        entry = (uint32)((j * class_count) + a);
        (void)nexus_data_stack_push(worklist, &entry);
      }
    }

    nexus_memory_deallocate(fill);
    nexus_memory_deallocate(block_of_tag);
  }

  for (state = 0; state < block_count; state++) {
    for (j = block_first[state]; j < block_first[state] + block_size[state]; j++) {
      block_of[elements[j]] = (uint16)state;
    }
  }

  /* Step 3: refinement. Popping (A, a): every block that holds both states entering A on a and states that do not is split along that line. */
  refinement_steps = 0;
  splits           = 0;

  for (;;) {
    uint32     entry;
    uint_large block;
    uint_large symbol;
    uint_large member_count;
    uint_large touched_count;
    uint_large i;

    if (nexus_data_stack_pop(worklist, &entry) == FALSE) {
      break;
    }

    refinement_steps++;

    block  = entry / class_count;
    symbol = entry % class_count;

    member_count = block_size[block];

    for (i = 0; i < member_count; i++) {
      members[i] = elements[block_first[block] + i];
    }

    touched_count = 0;

    for (i = 0; i < member_count; i++) {
      const uint32 *offsets;

      target  = members[i];
      offsets = inverse_offsets + (symbol * (state_count + 1));

      for (j = offsets[target]; j < offsets[target + 1]; j++) {
        uint_large predecessor;
        uint_large owner;
        uint_large destination;
        uint_large other;

        predecessor = inverse_states[(symbol * state_count) + j];
        owner       = block_of[predecessor];

        if (block_marked[owner] == 0) {
          touched[touched_count++] = (uint16)owner;
        }

        /* Swap the predecessor into the next free place of the marked prefix of its block. */
        destination = block_first[owner] + block_marked[owner];
        other       = elements[destination];

        elements[location[predecessor]] = (uint16)other;
        location[other]                 = location[predecessor];
        elements[destination]           = (uint16)predecessor;
        location[predecessor]           = (uint32)destination;

        block_marked[owner]++;
      }
    }

    for (i = 0; i < touched_count; i++) {
      uint_large owner;
      uint_large marked;
      uint_large size;
      uint_large fresh;
      uint_large k;

      owner  = touched[i];
      marked = block_marked[owner];
      size   = block_size[owner];

      block_marked[owner] = 0;

      if (marked == size) {
        continue;
      }

      /* The smaller part becomes the new block, so fewer states are renumbered and the new block is the one to queue. */
      fresh = block_count++;
      splits++;

      if (marked <= size - marked) {
        block_first[fresh] = block_first[owner];
        block_size[fresh]  = (uint32)marked;
        block_first[owner] += (uint32)marked;
        block_size[owner] = (uint32)(size - marked);
      } else {
        block_first[fresh] = block_first[owner] + (uint32)marked;
        block_size[fresh]  = (uint32)(size - marked);
        block_size[owner]  = (uint32)marked;
      }

      for (k = block_first[fresh]; k < block_first[fresh] + block_size[fresh]; k++) {
        block_of[elements[k]] = (uint16)fresh;
      }

      /* The smaller part is queued for every symbol. A pair already waiting for owner keeps waiting for what remains of it. */
      for (a = 0; a < class_count; a++) {
        uint32 queued_entry;

        queued_entry = (uint32)((fresh * class_count) + a);
        (void)nexus_data_stack_push(worklist, &queued_entry);
      }
    }
  }

  nexus_data_stack_destroy(worklist);

  /* Step 4: one state per block, numbered by the lowest state it holds, so the block of state 0 stays the initial state. */
  {
    uint16    *new_id;         /* Per block. */
    uint16    *representative; /* Per new state: its lowest old state. */
    uint_large next;

    new_id         = (uint16 *)nexus_memory_allocate(block_count * NEXUS_SIZEOF(uint16));
    representative = (uint16 *)nexus_memory_allocate(block_count * NEXUS_SIZEOF(uint16));

    for (j = 0; j < block_count; j++) {
      new_id[j] = L_DFA_NO_STATE;
    }

    next = 0;

    for (state = 0; state < state_count; state++) {
      if (new_id[block_of[state]] == L_DFA_NO_STATE) {
        new_id[block_of[state]] = (uint16)next;
        representative[next]    = (uint16)state;
        next++;
      }
    }

    minimized              = (LDFA *)nexus_memory_allocate(NEXUS_SIZEOF(*minimized));
    minimized->state_count = (uint16)block_count;
    minimized->class_count = dfa->class_count;
    minimized->transitions = (uint16 *)nexus_memory_allocate(block_count * class_count * NEXUS_SIZEOF(uint16));
    nexus_memory_bytes_copy(dfa->byte_class, minimized->byte_class, NEXUS_SIZEOF(minimized->byte_class));

    logos_bitset_initialize(&minimized->accepting_states, minimized->state_count);
    minimized->accept_tags = (uint16 *)nexus_memory_allocate(block_count * NEXUS_SIZEOF(uint16));

    for (j = 0; j < block_count; j++) {
      state = representative[j];

      for (a = 0; a < class_count; a++) {
        minimized->transitions[(j * class_count) + a] = new_id[block_of[dfa->transitions[(state * class_count) + a]]];
      }

      minimized->accept_tags[j] = dfa->accept_tags[state];

      if (minimized->accept_tags[j] != L_ACCEPT_TAG_NONE) {
        logos_bitset_set(&minimized->accepting_states, (uint16)j, TRUE);
      }
    }

    minimized->dead_state = l_dfa_dead_state_find(minimized);

    nexus_memory_deallocate(representative);
    nexus_memory_deallocate(new_id);
  }

  nexus_memory_deallocate(touched);
  nexus_memory_deallocate(members);
  nexus_memory_deallocate(block_marked);
  nexus_memory_deallocate(block_size);
  nexus_memory_deallocate(block_first);
  nexus_memory_deallocate(block_of);
  nexus_memory_deallocate(location);
  nexus_memory_deallocate(elements);
  nexus_memory_deallocate(inverse_states);
  nexus_memory_deallocate(inverse_offsets);

  (void)nexus_strings_bytes_format(table_before_string, NEXUS_SIZEOF(table_before_string), (uint64)l_dfa_table_bytes(dfa));
  (void)nexus_strings_bytes_format(table_after_string, NEXUS_SIZEOF(table_after_string), (uint64)l_dfa_table_bytes(minimized));

  (void)echo_debug(context, "DFA minimized (Hopcroft): %u states became %u (%u%% remain; %u accepting before, %u after), dead state %s.",
                   (unsigned int)dfa->state_count, (unsigned int)minimized->state_count,
                   (unsigned int)((100 * (uint_large)minimized->state_count) / dfa->state_count), (unsigned int)accepting_count,
                   (unsigned int)l_dfa_accepting_count(minimized), minimized->dead_state == L_DFA_NO_STATE ? "absent" : "present");
  (void)echo_debug(context, "DFA minimization: %llu refinement steps, %llu block splits, %s of tables before and %s after.",
                   (unsigned long long)refinement_steps, (unsigned long long)splits, table_before_string, table_after_string);

  return minimized;
}
