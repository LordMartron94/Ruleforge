/*
Glushkov compilation: see Zettel line 12/2.1a1c1a1.
*/

#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "./l_internal.h"

/* Adds the pairs last x first to the occurrence table: row p (the positions that may follow p) receives first for every p in last. */
static void l_nfa_occur_pairs_add(uint64 *occur_words, uint16 word_count, uint16 position_count, const LBitset *last, const LBitset *first) {
  LBitset row;
  uint16  position;

  row.word_count = word_count;

  for (position = 0; position < position_count; position++) {
    if (logos_bitset_get(last, position)) {
      row.words = occur_words + ((uint_large)position * word_count);
      logos_bitset_union(&row, first);
    }
  }
}

/* Marks a linearized operation that is not a symbol and so has no position. */
#define L_NFA_POSITION_NONE ((uint64) ~(uint64)0)

/* Expanded counts saturate here, so adding two of them never overflows a uint64. */
#define L_NFA_COUNT_LIMIT ((uint64)1 << 40)

/* State 0 is the initial state; state p (1 <= p <= symbol occurrences) is the position p of the linearized expression. */
struct LNFA {
  LRegula *regula;

  uint16                  state_count;
  uint64                 *transition_words; /* Row s: the states reachable from s. A transition into state t is always labelled state_symbols[t]. */
  LBitset                 accepting;
  uint16                 *accept_tags;   /* Per state: its accept tag, or L_ACCEPT_TAG_NONE when the state does not accept. */
  LRegulaObservationMask *state_symbols; /* The original symbol that enters each state; state 0 has none and stays empty. */

  uint16  word_count;       /* Words of every bitset and table row. */
  uint64 *byte_state_words; /* Row b: the states that a transition on byte b may enter, so a simulation step intersects with one row. */
};

/* One node of the linearized tree, kept in a list indexed by the copy's storage_index. */
typedef struct LRegulaLinearizationItem {
  LRegulaOperation *operation; /* The copy in the linearized tree. */
  uint64            position;  /* L_NFA_POSITION_NONE unless a symbol. */

  struct {
    boolean nullable;
    LBitset first; /* Views into one shared word arena. */
    LBitset last;
  } analysis;
} LRegulaLinearizationItem;

typedef struct LRegulaLinearizationFrame {
  const LRegulaOperation *original_operation;
  boolean                 children_processed;
} LRegulaLinearizationFrame;

/* What the linearized tree of a stored operation will contain; a subtree shared inside the source is counted once per path. */
typedef struct LRegulaLinearizationSize {
  uint64 symbols;
  uint64 nodes;
} LRegulaLinearizationSize;

static uint64 l_nfa_count_add(uint64 count_a, uint64 count_b) {
  uint64 sum;

  sum = count_a + count_b;

  return sum > L_NFA_COUNT_LIMIT ? L_NFA_COUNT_LIMIT : sum;
}

/* Operations are stored after their operands, so the operand's size is already final when its user is reached. */
static void l_nfa_size_operand_add(LRegulaLinearizationSize *sizes, uint_large index, const LRegulaOperation *operand) {
  NEXUS_PROGRAM_PRECONDITION(operand != NULL && operand->storage_index < index, "operands must be stored before the operations using them");

  sizes[index].symbols = l_nfa_count_add(sizes[index].symbols, sizes[operand->storage_index].symbols);
  sizes[index].nodes   = l_nfa_count_add(sizes[index].nodes, sizes[operand->storage_index].nodes);
}

static LRegulaLinearizationItem *l_nfa_results_pop(NexusDataStack *results) {
  void *popped;

  if (nexus_data_stack_pop_pointer(results, &popped) == FALSE) {
    NEXUS_PROGRAM_PANIC("linearization results stack is empty");
  }

  return (LRegulaLinearizationItem *)popped;
}

/* Indexes the labels by byte, so a simulation step intersects with one row instead of testing every state's label. */
static void l_nfa_byte_index_build(LNFA *nfa) {
  LBitset    byte_row;
  uint_large observation;
  uint_large state;

  byte_row.word_count = nfa->word_count;

  for (observation = 0; observation < 256; observation++) {
    byte_row.words = nfa->byte_state_words + (observation * nfa->word_count);

    for (state = 1; state < nfa->state_count; state++) {
      if (logos_languages_regula_observation_mask_get(nfa->state_symbols[state], (byte)observation)) {
        logos_bitset_set(&byte_row, (uint16)state, TRUE);
      }
    }
  }
}

LNFA *logos_automatons_nfa_from_regula(LRegula *regula, EchoContext *context) {
  LNFA                 *nfa;
  LRegula              *linearized;
  NexusDataSlabbedList *items;        /* LRegulaLinearizationItem per linearized node, indexed by its storage_index. */
  uint64                symbol_total; /* Symbol occurrences: the NFA positions. */
  uint64                node_total;   /* Nodes of the linearized tree. */
  uint_large            word_count;   /* Words of a bitset over all states. */

  NEXUS_PROGRAM_PRECONDITION(regula != NULL && context != NULL, "regula and context must not be NULL");

  nfa         = (LNFA *)nexus_memory_allocate(NEXUS_SIZEOF(*nfa));
  nfa->regula = regula;

  linearized = logos_languages_regula_regula_create("glushkov_local", NEXUS_VERSION_PACK(0, 0, 1, 0));
  items      = nexus_data_slabbed_list_create(NEXUS_SIZEOF(LRegulaLinearizationItem), 64, 64);

  /* Copies are only stored while the tree is wired by hand; attaching each one would concatenate it into the root. */
  linearized->internal_constructing = TRUE;

  /* Step 1: sizing. The set width is the number of states, so it must be known before the first set is built. One linear loop suffices because
     operands are stored before their users: each stored operation's counts follow from its operands' counts. An empty regula has no root and
     no symbols: its automaton is the initial state alone, accepting nothing. */
  symbol_total = 0;
  node_total   = 0;

  if (regula->root_rule != NULL) {
    LRegulaLinearizationSize *sizes;
    uint_large                stored_count;
    uint_large                i;

    stored_count = nexus_data_slabbed_list_length_get(regula->operations);
    sizes        = (LRegulaLinearizationSize *)nexus_memory_allocate(stored_count * NEXUS_SIZEOF(*sizes));

    for (i = 0; i < stored_count; i++) {
      const LRegulaOperation *operation;

      operation        = (const LRegulaOperation *)nexus_data_slabbed_list_at_const(regula->operations, i);
      sizes[i].symbols = 0;
      sizes[i].nodes   = 1;

      switch (operation->kind) {
      case LROK_EMPTY_LANGUAGE:
      case LROK_EMPTY_STRING:
        break;
      case LROK_SYMBOL:
        sizes[i].symbols = 1;
        break;
      case LROK_UNIONIZE:
        l_nfa_size_operand_add(sizes, i, operation->operation_data.unionize.operation_a);
        l_nfa_size_operand_add(sizes, i, operation->operation_data.unionize.operation_b);
        break;
      case LROK_CONCATENATE:
        l_nfa_size_operand_add(sizes, i, operation->operation_data.concatenate.operation_a);
        l_nfa_size_operand_add(sizes, i, operation->operation_data.concatenate.operation_b);
        break;
      case LROK_STAR:
        l_nfa_size_operand_add(sizes, i, operation->operation_data.operation_star);
        break;
      case LROK_COUNT:
      default:
        NEXUS_PROGRAM_PANIC("invalid operation kind");
      }
    }

    symbol_total = sizes[regula->root_rule->storage_index].symbols;
    node_total   = sizes[regula->root_rule->storage_index].nodes;
    nexus_memory_deallocate(sizes);
  }

  NEXUS_PROGRAM_PRECONDITION(node_total < L_NFA_COUNT_LIMIT, "regula expands to too many linearized nodes");
  NEXUS_PROGRAM_PRECONDITION(symbol_total < (uint64)UINT16_MAX_VAL, "regula has more symbol occurrences than the state numbering can hold");

  /* The automaton's tables exist before the traversal: the occurrence relation is accumulated straight into the transition rows. */
  nfa->state_count = (uint16)(symbol_total + 1);
  word_count       = logos_bitset_word_count_get(nfa->state_count);

  nfa->transition_words = (uint64 *)nexus_memory_allocate((uint_large)nfa->state_count * word_count * NEXUS_SIZEOF(uint64));
  nfa->state_symbols    = (LRegulaObservationMask *)nexus_memory_allocate((uint_large)nfa->state_count * NEXUS_SIZEOF(LRegulaObservationMask));
  nfa->accept_tags      = (uint16 *)nexus_memory_allocate((uint_large)nfa->state_count * NEXUS_SIZEOF(uint16));
  nexus_memory_bytes_clear(nfa->transition_words, (uint_large)nfa->state_count * word_count * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(nfa->state_symbols, (uint_large)nfa->state_count * NEXUS_SIZEOF(LRegulaObservationMask));
  logos_bitset_initialize(&nfa->accepting, nfa->state_count);

  nfa->word_count       = (uint16)word_count;
  nfa->byte_state_words = (uint64 *)nexus_memory_allocate(256 * word_count * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(nfa->byte_state_words, 256 * word_count * NEXUS_SIZEOF(uint64));

  if (regula->root_rule != NULL) {
    uint64                   *analysis_words;      /* first and last of every node, back to back. */
    LRegulaOperation        **position_operations; /* The linearized symbol of every position; index 0 (the initial state) has none. */
    LRegulaLinearizationItem *root_item;
    uint_large                i;

    analysis_words      = (uint64 *)nexus_memory_allocate((uint_large)node_total * 2 * word_count * NEXUS_SIZEOF(uint64));
    position_operations = (LRegulaOperation **)nexus_memory_allocate((uint_large)nfa->state_count * NEXUS_SIZEOF(LRegulaOperation *));
    nexus_memory_bytes_clear(analysis_words, (uint_large)node_total * 2 * word_count * NEXUS_SIZEOF(uint64));

    for (i = 0; i < nfa->state_count; i++) {
      position_operations[i] = NULL;
    }

    /* Step 2: linearisation and set computation in one post-order traversal. A node's copy is made after its operands' copies, so it is created
       complete, and its sets follow from its operands' sets. Positions are numbered from 1, left to right, so a position is also its state.
       Only the root's occur is needed, and occur is only ever unioned upward or extended at concatenate and star, so one relation is
       accumulated (into the transition rows) instead of one N x N matrix per node. */
    {
      NexusDataStack *traversal;
      NexusDataStack *results; /* Item of every finished subtree that no parent has consumed yet. */
      uint64          position_counter;

      traversal = nexus_data_stack_create(NEXUS_SIZEOF(LRegulaLinearizationFrame), 16);
      results   = nexus_data_stack_create(NEXUS_SIZEOF(LRegulaLinearizationItem *), 16);

      {
        LRegulaLinearizationFrame root;

        root.original_operation = regula->root_rule;
        root.children_processed = FALSE;
        nexus_data_stack_push(traversal, &root);
      }

      position_counter = 1;

      for (;;) {
        LRegulaLinearizationFrame current;
        LRegulaLinearizationFrame child;
        LRegulaLinearizationItem  new_item;
        LRegulaLinearizationItem *item;
        LRegulaLinearizationItem *item_a;
        LRegulaLinearizationItem *item_b;
        LRegulaOperation         *copy;
        uint_large                index;

        if (nexus_data_stack_pop(traversal, &current) == FALSE) {
          break;
        }

        if (!current.children_processed) {
          current.children_processed = TRUE;
          nexus_data_stack_push(traversal, &current);

          child.children_processed = FALSE;

          switch (current.original_operation->kind) {
          case LROK_EMPTY_LANGUAGE:
          case LROK_EMPTY_STRING:
          case LROK_SYMBOL:
            break;
          case LROK_UNIONIZE:
            child.original_operation = current.original_operation->operation_data.unionize.operation_b;
            nexus_data_stack_push(traversal, &child);

            child.original_operation = current.original_operation->operation_data.unionize.operation_a;
            nexus_data_stack_push(traversal, &child);
            break;
          case LROK_CONCATENATE:
            child.original_operation = current.original_operation->operation_data.concatenate.operation_b;
            nexus_data_stack_push(traversal, &child);

            child.original_operation = current.original_operation->operation_data.concatenate.operation_a;
            nexus_data_stack_push(traversal, &child);
            break;
          case LROK_STAR:
            child.original_operation = current.original_operation->operation_data.operation_star;
            nexus_data_stack_push(traversal, &child);
            break;
          case LROK_COUNT:
          default:
            NEXUS_PROGRAM_PANIC("invalid operation kind");
          }

          continue;
        }

        /* Operands are done. The copy has NULL operands until the switch below wires in their copies. */
        copy  = logos_languages_regula_regula_operation_duplicate_shallow(linearized, current.original_operation);
        index = nexus_data_slabbed_list_length_get(items);

        /* One item per copy, in creation order, so item i belongs to the copy whose storage_index is i. */
        NEXUS_PROGRAM_INVARIANT(index == copy->storage_index, "items must stay aligned with linearized storage");

        new_item.operation                 = copy;
        new_item.position                  = L_NFA_POSITION_NONE;
        new_item.analysis.nullable         = FALSE;
        new_item.analysis.first.words      = analysis_words + (2 * index * word_count);
        new_item.analysis.first.word_count = (uint16)word_count;
        new_item.analysis.last.words       = analysis_words + (((2 * index) + 1) * word_count);
        new_item.analysis.last.word_count  = (uint16)word_count;

        /* first and last start empty and nullable false (cleared arena) as the rules below expect. Slabbed storage is address-stable. */
        item = (LRegulaLinearizationItem *)nexus_data_slabbed_list_append(items, &new_item);

        switch (copy->kind) {
        case LROK_EMPTY_LANGUAGE:
          break;
        case LROK_EMPTY_STRING:
          item->analysis.nullable = TRUE;
          break;
        case LROK_SYMBOL:
          item->position                      = position_counter;
          position_operations[item->position] = copy;
          position_counter++;

          logos_bitset_set(&item->analysis.first, (uint16)item->position, TRUE);
          logos_bitset_set(&item->analysis.last, (uint16)item->position, TRUE);
          break;
        case LROK_UNIONIZE:
          item_b = l_nfa_results_pop(results);
          item_a = l_nfa_results_pop(results);

          copy->operation_data.unionize.operation_a = item_a->operation;
          copy->operation_data.unionize.operation_b = item_b->operation;

          item->analysis.nullable = item_a->analysis.nullable || item_b->analysis.nullable;

          logos_bitset_union(&item->analysis.first, &item_a->analysis.first);
          logos_bitset_union(&item->analysis.first, &item_b->analysis.first);
          logos_bitset_union(&item->analysis.last, &item_a->analysis.last);
          logos_bitset_union(&item->analysis.last, &item_b->analysis.last);
          break;
        case LROK_CONCATENATE:
          item_b = l_nfa_results_pop(results);
          item_a = l_nfa_results_pop(results);

          copy->operation_data.concatenate.operation_a = item_a->operation;
          copy->operation_data.concatenate.operation_b = item_b->operation;

          item->analysis.nullable = item_a->analysis.nullable && item_b->analysis.nullable;

          logos_bitset_union(&item->analysis.first, &item_a->analysis.first);

          if (item_a->analysis.nullable) {
            logos_bitset_union(&item->analysis.first, &item_b->analysis.first);
          }

          logos_bitset_union(&item->analysis.last, &item_b->analysis.last);

          if (item_b->analysis.nullable) {
            logos_bitset_union(&item->analysis.last, &item_a->analysis.last);
          }

          l_nfa_occur_pairs_add(nfa->transition_words, (uint16)word_count, nfa->state_count, &item_a->analysis.last, &item_b->analysis.first);
          break;
        case LROK_STAR:
          item_a = l_nfa_results_pop(results);

          copy->operation_data.operation_star = item_a->operation;

          item->analysis.nullable = TRUE;

          logos_bitset_union(&item->analysis.first, &item_a->analysis.first);
          logos_bitset_union(&item->analysis.last, &item_a->analysis.last);

          l_nfa_occur_pairs_add(nfa->transition_words, (uint16)word_count, nfa->state_count, &item_a->analysis.last, &item_a->analysis.first);
          break;
        case LROK_COUNT:
        default:
          NEXUS_PROGRAM_PANIC("invalid operation kind");
        }

        nexus_data_stack_push_pointer(results, item);
      }

      NEXUS_PROGRAM_INVARIANT(position_counter == symbol_total + 1, "the traversal must reach every counted symbol");
      NEXUS_PROGRAM_INVARIANT(nexus_data_slabbed_list_length_get(items) == node_total, "the traversal must create every counted node");

      /* The last finished subtree is the whole expression. */
      linearized->internal_constructing = FALSE;
      root_item                         = l_nfa_results_pop(results);
      NEXUS_PROGRAM_INVARIANT(nexus_data_stack_length_get(results) == 0, "exactly one subtree must remain");
      (void)logos_languages_regula_regula_operation_add(linearized, root_item->operation);

      nexus_data_stack_destroy(results);
      nexus_data_stack_destroy(traversal);
    }

    /* Step 3: the automaton of the local language. The occurrence pairs (a, b) are already the transition rows of state a, each into b labelled b;
       what remains is the initial state's transitions into every first position, the accepting states, and the empty string. */
    {
      LBitset initial_row;

      initial_row.words      = nfa->transition_words;
      initial_row.word_count = (uint16)word_count;

      logos_bitset_union(&initial_row, &root_item->analysis.first);
      logos_bitset_union(&nfa->accepting, &root_item->analysis.last);

      if (root_item->analysis.nullable) {
        logos_bitset_set(&nfa->accepting, 0, TRUE);
      }
    }

    /* Step 4: removing the linearisation. A transition into state t is labelled by t's position, so its label is a property of its target. Replacing
       each position by its original symbol therefore means recording that symbol per state; states, transitions and accepting states are unchanged.
       The linearized copy kept the original symbol's mask. */
    for (i = 1; i < nfa->state_count; i++) {
      nexus_memory_bytes_copy(position_operations[i]->operation_data.symbol_mask, nfa->state_symbols[i], NEXUS_SIZEOF(LRegulaObservationMask));
    }

    l_nfa_byte_index_build(nfa);

    nexus_memory_deallocate((void *)position_operations);
    nexus_memory_deallocate(analysis_words);
  }

  /* A plain language has one kind of acceptance, tag 0. */
  {
    uint_large state;

    for (state = 0; state < nfa->state_count; state++) {
      nfa->accept_tags[state] = logos_bitset_get(&nfa->accepting, (uint16)state) ? 0 : L_ACCEPT_TAG_NONE;
    }
  }

  /* Statistics of the constructed automaton. */
  {
    uint_large state;
    uint_large word;
    uint64     transitions;
    uint32     out_degree;
    uint32     out_degree_max;
    uint32     accepting_count;
    uint_large table_bytes;
    char       table_size_string[50];

    transitions    = 0;
    out_degree_max = 0;

    for (state = 0; state < nfa->state_count; state++) {
      out_degree = 0;

      for (word = 0; word < word_count; word++) {
        out_degree += nexus_bits_uint64_popcount(nfa->transition_words[(state * word_count) + word]);
      }

      transitions += out_degree;

      if (out_degree > out_degree_max) {
        out_degree_max = out_degree;
      }
    }

    accepting_count = 0;

    for (word = 0; word < word_count; word++) {
      accepting_count += nexus_bits_uint64_popcount(nfa->accepting.words[word]);
    }

    table_bytes = (((uint_large)nfa->state_count * word_count) + (256 * word_count) + word_count) * NEXUS_SIZEOF(uint64);
    table_bytes += (uint_large)nfa->state_count * NEXUS_SIZEOF(LRegulaObservationMask);

    (void)nexus_strings_bytes_format(table_size_string, NEXUS_SIZEOF(table_size_string), (uint64)table_bytes);

    (void)echo_debug(context, "NFA from regula \"%s\": %llu stored operations, %llu linearized nodes, %llu symbol occurrences.",
                     regula->label != NULL ? regula->label : "(unlabelled)",
                     (unsigned long long)nexus_data_slabbed_list_length_get(regula->operations), (unsigned long long)node_total,
                     (unsigned long long)symbol_total);
    (void)echo_debug(context, "NFA: %u states (%u accepting, initial state %s), %llu transitions, max out-degree %u, %s of tables.",
                     (unsigned int)nfa->state_count, (unsigned int)accepting_count,
                     logos_bitset_get(&nfa->accepting, 0) ? "accepting" : "not accepting", (unsigned long long)transitions,
                     (unsigned int)out_degree_max, table_size_string);
  }

  /* The linearized tree and its analysis only serve the construction. */
  nexus_data_slabbed_list_destroy(items);
  logos_languages_regula_regula_destroy(linearized);

  return nfa;
}

void logos_automatons_nfa_destroy(LNFA *nfa) {
  NEXUS_PROGRAM_PRECONDITION(nfa != NULL, "nfa must not be NULL");

  nexus_memory_deallocate(nfa->byte_state_words);
  nexus_memory_deallocate(nfa->accept_tags);
  nexus_memory_deallocate(nfa->state_symbols);
  nexus_memory_deallocate(nfa->accepting.words);
  nexus_memory_deallocate(nfa->transition_words);
  nexus_memory_deallocate(nfa);
}

uint16 logos_automatons_nfa_state_count_get(const LNFA *nfa) {
  NEXUS_PROGRAM_PRECONDITION(nfa != NULL, "nfa must not be NULL");

  return nfa->state_count;
}

void logos_automatons_nfa_initial_set_get(const LNFA *nfa, LBitset *set) {
  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && set != NULL, "nfa and set must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(set->word_count == nfa->word_count, "set must have the NFA's width");

  logos_bitset_clear(set);
  logos_bitset_set(set, 0, TRUE);
}

void logos_automatons_nfa_successors_get(const LNFA *nfa, const LBitset *source, LBitset *destination) {
  LBitset    row;
  uint_large word;

  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && source != NULL && destination != NULL, "nfa, source and destination must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(source->word_count == nfa->word_count && destination->word_count == nfa->word_count, "sets must have the NFA's width");
  NEXUS_PROGRAM_PRECONDITION(source != destination, "source and destination must be different sets");

  row.word_count = nfa->word_count;

  logos_bitset_clear(destination);

  for (word = 0; word < nfa->word_count; word++) {
    uint64 word_value;

    word_value = source->words[word];

    while (word_value != 0) {
      uint32     bit;
      uint_large state;

      bit        = nexus_bits_uint64_leading_zeros(word_value);
      state      = (word * 64) + bit;
      word_value = nexus_bits_uint64_set_msb(word_value, bit, FALSE);

      row.words = nfa->transition_words + (state * nfa->word_count);
      logos_bitset_union(destination, &row);
    }
  }
}

boolean logos_automatons_nfa_byte_restrict(const LNFA *nfa, byte value, LBitset *set) {
  const uint64 *entered;
  uint_large    word;
  uint64        live;

  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && set != NULL, "nfa and set must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(set->word_count == nfa->word_count, "set must have the NFA's width");

  entered = nfa->byte_state_words + ((uint_large)value * nfa->word_count);
  live    = 0;

  for (word = 0; word < nfa->word_count; word++) {
    set->words[word] &= entered[word];
    live |= set->words[word];
  }

  return live != 0;
}

boolean logos_automatons_nfa_set_accepts(const LNFA *nfa, const LBitset *set) {
  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && set != NULL, "nfa and set must not be NULL");

  return logos_bitset_intersects(set, &nfa->accepting);
}

uint16 logos_automatons_nfa_set_accept_tag_get(const LNFA *nfa, const LBitset *set) {
  uint_large word;
  uint16     winner;

  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && set != NULL, "nfa and set must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(set->word_count == nfa->word_count, "set must have the NFA's width");

  winner = L_ACCEPT_TAG_NONE;

  for (word = 0; word < nfa->word_count; word++) {
    uint64 accepted;

    accepted = set->words[word] & nfa->accepting.words[word];

    while (accepted != 0) {
      uint32 bit;
      uint16 tag;

      bit      = nexus_bits_uint64_leading_zeros(accepted);
      accepted = nexus_bits_uint64_set_msb(accepted, bit, FALSE);
      tag      = nfa->accept_tags[(word * 64) + bit];

      if (tag < winner) {
        winner = tag;
      }
    }
  }

  return winner;
}

uint16 logos_automatons_nfa_byte_classes_get(const LNFA *nfa, byte *byte_class, byte *class_representative) {
  uint_large class_count;
  uint_large value;
  uint_large candidate;

  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && byte_class != NULL && class_representative != NULL, "nfa and both output arrays must not be NULL");

  class_count = 0;

  for (value = 0; value < 256; value++) {
    for (candidate = 0; candidate < class_count; candidate++) {
      if (nexus_memory_bytes_equal(nfa->byte_state_words + (value * nfa->word_count),
                                   nfa->byte_state_words + ((uint_large)class_representative[candidate] * nfa->word_count),
                                   (uint_large)nfa->word_count * NEXUS_SIZEOF(uint64))) {
        break;
      }
    }

    if (candidate == class_count) {
      class_representative[candidate] = (byte)value;
      class_count++;
    }

    byte_class[value] = (byte)candidate;
  }

  return (uint16)class_count;
}

boolean logos_automatons_nfa_match(const LNFA *nfa, const byte *source, uint_large source_size) {
  LBitset    active; /* The simulation's working sets, local so that matching never mutates the NFA. */
  LBitset    scratch;
  LBitset   *current;
  LBitset   *next;
  LBitset   *swapped;
  uint64    *working_words; /* active and scratch back to back: one allocation per call. */
  uint_large position;
  boolean    alive;
  boolean    matched;

  NEXUS_PROGRAM_PRECONDITION(nfa != NULL && (source != NULL || source_size == 0), "nfa and a nonempty source must not be NULL");

  working_words = (uint64 *)nexus_memory_allocate(2 * (uint_large)nfa->word_count * NEXUS_SIZEOF(uint64));

  active.words       = working_words;
  active.word_count  = nfa->word_count;
  scratch.words      = working_words + nfa->word_count;
  scratch.word_count = nfa->word_count;

  current = &active;
  next    = &scratch;

  logos_automatons_nfa_initial_set_get(nfa, current);

  alive = TRUE;

  for (position = 0; position < source_size && alive; position++) {
    /* The states reachable from any active state in one transition, restricted to the states this byte enters. No survivor means no continuation
       can ever match. */
    logos_automatons_nfa_successors_get(nfa, current, next);
    alive = logos_automatons_nfa_byte_restrict(nfa, source[position], next);

    if (alive) {
      swapped = current;
      current = next;
      next    = swapped;
    }
  }

  matched = alive && logos_automatons_nfa_set_accepts(nfa, current);

  nexus_memory_deallocate(working_words);

  return matched;
}

LNFA *logos_automatons_nfa_union(const LNFA *const *parts, const uint16 *tags, uint_large count, EchoContext *context) {
  LNFA      *nfa;
  uint_large total;
  uint_large word_count;
  uint_large part_index;
  uint_large base;
  uint_large state;

  NEXUS_PROGRAM_PRECONDITION(parts != NULL && tags != NULL && context != NULL, "parts, tags and context must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(count > 0, "at least one part is required");

  /* The initial state is shared; every other state of every part is its own. */
  total = 1;

  for (part_index = 0; part_index < count; part_index++) {
    NEXUS_PROGRAM_PRECONDITION(parts[part_index] != NULL, "a part must not be NULL");
    NEXUS_PROGRAM_PRECONDITION(tags[part_index] != L_ACCEPT_TAG_NONE, "a tag must not be L_ACCEPT_TAG_NONE");

    total += (uint_large)parts[part_index]->state_count - 1;
  }

  if (total > (uint_large)UINT16_MAX_VAL) {
    (void)echo_error(context, "NFA union: %llu parts need %llu states, more than the %u a state numbering can hold; no NFA was built.",
                     (unsigned long long)count, (unsigned long long)total, (unsigned int)UINT16_MAX_VAL);

    return NULL;
  }

  word_count = logos_bitset_word_count_get((uint16)total);

  nfa              = (LNFA *)nexus_memory_allocate(NEXUS_SIZEOF(*nfa));
  nfa->regula      = NULL;
  nfa->state_count = (uint16)total;
  nfa->word_count  = (uint16)word_count;

  nfa->transition_words = (uint64 *)nexus_memory_allocate(total * word_count * NEXUS_SIZEOF(uint64));
  nfa->state_symbols    = (LRegulaObservationMask *)nexus_memory_allocate(total * NEXUS_SIZEOF(LRegulaObservationMask));
  nfa->accept_tags      = (uint16 *)nexus_memory_allocate(total * NEXUS_SIZEOF(uint16));
  nfa->byte_state_words = (uint64 *)nexus_memory_allocate(256 * word_count * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(nfa->transition_words, total * word_count * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(nfa->state_symbols, total * NEXUS_SIZEOF(LRegulaObservationMask));
  nexus_memory_bytes_clear(nfa->byte_state_words, 256 * word_count * NEXUS_SIZEOF(uint64));
  logos_bitset_initialize(&nfa->accepting, nfa->state_count);

  for (state = 0; state < total; state++) {
    nfa->accept_tags[state] = L_ACCEPT_TAG_NONE;
  }

  /* State 0 of every part is the shared initial state. State s >= 1 of a part becomes base + s, where base counts the non-initial states of the
     parts before it, so the first part keeps its numbers. */
  base = 0;

  for (part_index = 0; part_index < count; part_index++) {
    const LNFA *part;
    LBitset     source_row;
    LBitset     destination_row;

    part                  = parts[part_index];
    source_row.word_count = part->word_count;

    destination_row.word_count = nfa->word_count;

    for (state = 0; state < part->state_count; state++) {
      uint_large target_word;
      uint_large mapped_state;

      mapped_state = state == 0 ? 0 : base + state;

      source_row.words      = part->transition_words + (state * part->word_count);
      destination_row.words = nfa->transition_words + (mapped_state * nfa->word_count);

      for (target_word = 0; target_word < part->word_count; target_word++) {
        uint64 members;

        members = source_row.words[target_word];

        while (members != 0) {
          uint32     bit;
          uint_large target;

          bit     = nexus_bits_uint64_leading_zeros(members);
          members = nexus_bits_uint64_set_msb(members, bit, FALSE);
          target  = (target_word * 64) + bit;

          /* A Glushkov state is never entered into the initial state, so every target is a symbol position. */
          logos_bitset_set(&destination_row, (uint16)(base + target), TRUE);
        }
      }

      if (logos_bitset_get(&part->accepting, (uint16)state)) {
        logos_bitset_set(&nfa->accepting, (uint16)mapped_state, TRUE);

        if (tags[part_index] < nfa->accept_tags[mapped_state]) {
          nfa->accept_tags[mapped_state] = tags[part_index];
        }
      }

      if (state > 0) {
        nexus_memory_bytes_copy(part->state_symbols[state], nfa->state_symbols[mapped_state], NEXUS_SIZEOF(LRegulaObservationMask));
      }
    }

    base += (uint_large)part->state_count - 1;
  }

  l_nfa_byte_index_build(nfa);

  {
    uint_large accepting_count;
    uint_large word;

    accepting_count = 0;

    for (word = 0; word < nfa->word_count; word++) {
      accepting_count += nexus_bits_uint64_popcount(nfa->accepting.words[word]);
    }

    (void)echo_debug(context, "NFA union: %llu parts became one NFA of %u states, %u accepting.", (unsigned long long)count,
                     (unsigned int)nfa->state_count, (unsigned int)accepting_count);
  }

  return nfa;
}
