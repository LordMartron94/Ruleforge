#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* ---------------------------------------------------------------------------- */
/* PER-CHART MEMOS                                                               */
/* ---------------------------------------------------------------------------- */

/* Summary: makes chart the one a memo describes; charts are built once each, in increasing order, so earlier entries are dead. */
static void l_parsing_memo_select(NexusDataWordMap *memo, uint_large *memo_chart, uint_large chart) {
  if (*memo_chart == chart) {
    return;
  }

  NEXUS_PROGRAM_INVARIANT(*memo_chart == LPARSER_INDEX_NONE || chart > *memo_chart, "a chart's items and nodes are built before the next chart's");
  nexus_data_word_map_clear(memo);
  *memo_chart = chart;
}

static boolean l_parsing_memo_get(const NexusDataWordMap *memo, const uint_large *key, uint_large *out_value) {
  const uint_large *value;

  value = nexus_data_word_map_get(memo, key);

  if (value == NULL) {
    return FALSE;
  }

  *out_value = value[0];

  return TRUE;
}

/* Summary: records a key known to be absent. */
static void l_parsing_memo_put(NexusDataWordMap *memo, const uint_large *key, uint_large value) {
  *nexus_data_word_map_insert(memo, key) = value;
}

/* ---------------------------------------------------------------------------- */
/* INTERNED FOREST AND CHART STORAGE                                             */
/* ---------------------------------------------------------------------------- */

LParsingNodeIndex logos_language_tooling_parsing_parser_process_node_create(LParserProcess *process, LParserGreenKind kind, uint_large width,
                                                                            uint_large symbol) {
  LParsingGreenNode *node;
  uint_large         index;

  index = nexus_data_chunked_list_length_get(&process->tree->nodes);

  if (!process->finishing && (process->limited || index >= process->parser->limits.max_nodes)) {
    process->limited = TRUE;

    return LPARSER_INDEX_NONE;
  }

  node                    = (LParsingGreenNode *)nexus_data_chunked_list_append_uninitialized(&process->tree->nodes);
  node->kind              = (uint8)kind;
  node->lexical_error     = (uint8)LLE_NONE;
  node->reserved          = 0;
  node->width             = (uint32)width;
  node->alternative_count = 0;
  node->aux               = (uint32)symbol;
  node->first_alternative = (uint32)LPARSER_INDEX_NONE;
  node->last_alternative  = (uint32)LPARSER_INDEX_NONE;
  node->payload.label     = NULL;

  return index;
}

LParsingNodeIndex logos_language_tooling_parsing_parser_process_node_get(LParserProcess *process, uint_large category, uint_large identity,
                                                                         uint_large origin, uint_large end, LParserGreenKind kind,
                                                                         uint_large symbol) {
  return logos_language_tooling_parsing_parser_process_node_marked_get(process, category, identity, origin, end, kind, symbol, 0);
}

LParsingNodeIndex logos_language_tooling_parsing_parser_process_node_marked_get(LParserProcess *process, uint_large category, uint_large identity,
                                                                                uint_large origin, uint_large end, LParserGreenKind kind,
                                                                                uint_large symbol, uint_large marker) {
  uint_large           key[4];
  uint_large           found;
  const LParsingChart *start_chart;
  const LParsingChart *end_chart;

  if (process->limited) {
    return LPARSER_INDEX_NONE;
  }

  key[0] = category;
  key[1] = identity;
  key[2] = origin;
  key[3] = marker; /* The end is the memo's chart. */

  l_parsing_memo_select(process->node_memo, &process->node_memo_chart, end);

  if (l_parsing_memo_get(process->node_memo, key, &found)) {
    return found;
  }

  start_chart = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, origin);
  end_chart   = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, end);
  NEXUS_PROGRAM_INVARIANT(end_chart->byte_offset >= start_chart->byte_offset, "forest extents must be ordered");

  found = logos_language_tooling_parsing_parser_process_node_create(process, kind, end_chart->byte_offset - start_chart->byte_offset, symbol);

  if (found != LPARSER_INDEX_NONE) {
    l_parsing_memo_put(process->node_memo, key, found);
  }

  return found;
}

void logos_language_tooling_parsing_parser_process_alternative_append(LParserProcess *process, LParsingNodeIndex parent, LParsingNodeIndex first,
                                                                      LParsingNodeIndex second) {
  uint_large           found;
  uint_large           width;
  LParsingGreenNode   *node;
  LParsingAlternative *alternative;
  LParsingAlternative *previous;

  if (parent == LPARSER_INDEX_NONE || (process->limited && !process->finishing)) {
    return;
  }

  found = nexus_data_chunked_list_length_get(&process->tree->alternatives);

  if (!process->finishing && found >= process->parser->limits.max_alternatives) {
    process->limited = TRUE;
    return;
  }

  width = 0;
  node  = (LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, parent);

  if (first != LPARSER_INDEX_NONE) {
    width = ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, first))->width;
  }

  if (second != LPARSER_INDEX_NONE) {
    width += ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, second))->width;
  }

  NEXUS_PROGRAM_INVARIANT(width == node->width, "every packed family must cover exactly its parent's bytes");

  alternative              = (LParsingAlternative *)nexus_data_chunked_list_append_uninitialized(&process->tree->alternatives);
  alternative->parent      = (uint32)parent;
  alternative->children[0] = (uint32)first;
  alternative->children[1] = (uint32)second;
  alternative->next        = (uint32)LPARSER_INDEX_NONE;

  if (node->last_alternative == LPARSER_INDEX_NONE) {
    node->first_alternative = (uint32)found;
  } else {
    previous       = (LParsingAlternative *)nexus_data_chunked_list_at(&process->tree->alternatives, node->last_alternative);
    previous->next = (uint32)found;
  }

  node->last_alternative = (uint32)found;
  node->alternative_count++;
}

/* Nodes with fewer alternatives than this detect duplicates by walking their chain; larger ones use the alternative memo. */
#define L_PARSING_ALTERNATIVE_MEMO_THRESHOLD 8

/*
Summary: adds an alternative to a node that may already have it.
Description: a node's alternatives are built in the chart where it ends, and most nodes keep one or two, so
  a short chain walk finds a duplicate without hashing. A node reaching the threshold moves into the memo,
  which is seeded with its existing alternatives once.
*/
static void l_parsing_process_alternative_add(LParserProcess *process, uint_large chart, LParsingNodeIndex parent, LParsingNodeIndex first,
                                              LParsingNodeIndex second) {
  const LParsingGreenNode   *node;
  const LParsingAlternative *alternative;
  uint_large                 key[4];
  uint_large                 cursor;
  uint_large                 found;

  if (parent == LPARSER_INDEX_NONE || (process->limited && !process->finishing)) {
    return;
  }

  node   = (const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, parent);
  key[2] = second;
  key[3] = 0;

  if (node->alternative_count < L_PARSING_ALTERNATIVE_MEMO_THRESHOLD) {
    for (cursor = node->first_alternative; cursor != LPARSER_INDEX_NONE; cursor = alternative->next) {
      alternative = (const LParsingAlternative *)nexus_data_chunked_list_at(&process->tree->alternatives, cursor);

      if (alternative->children[0] == first && alternative->children[1] == second) {
        return;
      }
    }

    found = nexus_data_chunked_list_length_get(&process->tree->alternatives);
    logos_language_tooling_parsing_parser_process_alternative_append(process, parent, first, second);

    if (node->alternative_count == L_PARSING_ALTERNATIVE_MEMO_THRESHOLD) {
      l_parsing_memo_select(process->alternative_memo, &process->alternative_memo_chart, chart);

      for (cursor = node->first_alternative; cursor != LPARSER_INDEX_NONE; cursor = alternative->next) {
        alternative = (const LParsingAlternative *)nexus_data_chunked_list_at(&process->tree->alternatives, cursor);
        key[0]      = parent;
        key[1]      = alternative->children[0];
        key[2]      = alternative->children[1];
        l_parsing_memo_put(process->alternative_memo, key, cursor);
      }
    }

    (void)found;
    return;
  }

  l_parsing_memo_select(process->alternative_memo, &process->alternative_memo_chart, chart);
  key[0] = parent;
  key[1] = first;

  if (l_parsing_memo_get(process->alternative_memo, key, &found)) {
    return;
  }

  found = nexus_data_chunked_list_length_get(&process->tree->alternatives);
  logos_language_tooling_parsing_parser_process_alternative_append(process, parent, first, second);

  if (nexus_data_chunked_list_length_get(&process->tree->alternatives) > found) {
    l_parsing_memo_put(process->alternative_memo, key, found);
  }
}

boolean logos_language_tooling_parsing_parser_process_headroom_reserve(LParserProcess *process, uint_large count) {
  if (process->finishing) {
    return TRUE;
  }

  if (process->limited || nexus_data_chunked_list_length_get(&process->tree->nodes) + count > process->parser->limits.max_nodes ||
      nexus_data_chunked_list_length_get(&process->tree->alternatives) + count > process->parser->limits.max_alternatives) {
    process->limited = TRUE;

    return FALSE;
  }

  return TRUE;
}

static uint_large l_parsing_process_chart(LParserProcess *process, uint_large byte_offset) {
  LParsingChart chart;
  uint_large    index;

  index = nexus_data_chunked_list_length_get(&process->charts);

  if (index >= process->parser->limits.max_items) {
    process->limited = TRUE;

    return LPARSER_INDEX_NONE;
  }

  chart.byte_offset  = (uint32)byte_offset;
  chart.first        = (uint32)LPARSER_INDEX_NONE;
  chart.last         = (uint32)LPARSER_INDEX_NONE;
  chart.root         = (uint32)LPARSER_INDEX_NONE;
  chart.waiter_first = 0;
  chart.waiter_count = 0;
  chart.closed       = FALSE;
  (void)nexus_data_chunked_list_append(&process->charts, &chart);

  return index;
}

/*
Summary: adds the item (production, dot, origin) to the chart under construction, reached by child from item_prefix.
Description: an item and its prefix node are one identity, so one memo lookup serves both. A new item gets a fresh
  prefix node with its first alternative; an existing item only gains an alternative on its existing prefix.
*/
static void l_parsing_process_item(LParserProcess *process, uint_large chart_index, uint_large production, uint_large dot, uint_large origin,
                                   uint_large marker, LParsingNodeIndex first, LParsingNodeIndex second) {
  uint_large           key[4];
  uint_large           found;
  LParsingItem         item;
  LParsingItem        *previous;
  LParsingChart       *chart;
  const LParsingChart *origin_chart;
  LParsingNodeIndex    prefix;

  if (process->limited) {
    return;
  }

  key[0] = production;
  key[1] = dot;
  key[2] = origin;
  key[3] = marker;

  l_parsing_memo_select(process->item_memo, &process->item_memo_chart, chart_index);

  if (l_parsing_memo_get(process->item_memo, key, &found)) {
    l_parsing_process_alternative_add(process, chart_index, ((const LParsingItem *)nexus_data_chunked_list_at(&process->items, found))->prefix, first,
                                      second);

    return;
  }

  found = nexus_data_chunked_list_length_get(&process->items);

  if (found >= process->parser->limits.max_items) {
    process->limited = TRUE;
    return;
  }

  chart        = (LParsingChart *)nexus_data_chunked_list_at(&process->charts, chart_index);
  origin_chart = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, origin);
  NEXUS_PROGRAM_INVARIANT(!chart->closed, "closed chart items are immutable");
  NEXUS_PROGRAM_INVARIANT(chart->byte_offset >= origin_chart->byte_offset, "forest extents must be ordered");

  prefix = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_SEQUENCE, chart->byte_offset - origin_chart->byte_offset,
                                                                     LPARSER_INDEX_NONE);

  if (prefix == LPARSER_INDEX_NONE) {
    return;
  }

  logos_language_tooling_parsing_parser_process_alternative_append(process, prefix, first, second);

  item.production  = (uint32)production;
  item.dot         = (uint16)dot;
  item.marker      = (uint16)marker;
  item.origin      = (uint32)origin;
  item.prefix      = (uint32)prefix;
  item.next_chart  = (uint32)LPARSER_INDEX_NONE;
  item.next_waiter = (uint32)LPARSER_INDEX_NONE;
  (void)nexus_data_chunked_list_append(&process->items, &item);

  if (chart->last == LPARSER_INDEX_NONE) {
    chart->first = (uint32)found;
  } else {
    previous             = (LParsingItem *)nexus_data_chunked_list_at(&process->items, chart->last);
    previous->next_chart = (uint32)found;
  }

  chart->last = (uint32)found;
  l_parsing_memo_put(process->item_memo, key, found);
}

static void l_parsing_process_seed(LParserProcess *process, uint_large chart, uint_large production) {
  l_parsing_process_item(process, chart, production, 0, chart, 0, LPARSER_INDEX_NONE, LPARSER_INDEX_NONE);
}

/*
Summary: advances an item over child into chart; marker is that of the precedence rule child was completed through.
Description: only an item of a unit production carries the marker on, to the node it completes; a longer production's completion has none.
*/
static void l_parsing_process_advance(LParserProcess *process, uint_large chart, const LParsingItem *item, uint_large dot, LParsingNodeIndex child,
                                      uint_large marker) {
  if (child == LPARSER_INDEX_NONE || process->limited) {
    return;
  }

  if (marker != 0 && ((const LParsingProduction *)nexus_data_list_at_const(process->parser->productions, item->production))->length != 1) {
    marker = 0;
  }

  l_parsing_process_item(process, chart, item->production, dot, item->origin, marker, item->prefix, child);
}

/*
Summary: whether the parent's item, waiting for a nonterminal at dot, may take a node completed through the rule with this marker.
Description: this is yacc's resolution of a shift/reduce conflict between two rules with precedence. Where the child is the last element
  of the parent and begins with a nonterminal itself, the parent reduces first (the child may not be there) unless the child's operator
  binds tighter, or equally tight and groups to the right. Where the child is the first element of the parent and ends in a nonterminal,
  the child reduces first unless the parent's operator binds tighter, or equally tight and groups to the right. Equal levels of
  nonassoc rules allow neither.
*/
static boolean l_parsing_precedence_allows(const LParser *parser, uint_large production_index, uint_large dot, uint_large marker) {
  const LParsingProduction *production;
  const LParsingPrecedence *parent;
  const LParsingPrecedence *child;
  uint8                     role;

  production = (const LParsingProduction *)nexus_data_list_at_const(parser->productions, production_index);

  if (production->owner == 0 || dot > 1) {
    return TRUE;
  }

  role = production->role[dot];

  if (role == 0) {
    return TRUE;
  }

  parent = (const LParsingPrecedence *)nexus_data_list_at_const(parser->precedences, (uint_large)production->owner - 1);
  child  = (const LParsingPrecedence *)nexus_data_list_at_const(parser->precedences, marker - 1);

  if ((role & L_PARSING_ROLE_LAST) != 0 && parent->right_open && child->left_open) {
    if (child->level != parent->level) {
      return child->level > parent->level;
    }

    return child->associativity == LCPA_RIGHT;
  }

  if ((role & L_PARSING_ROLE_FIRST) != 0 && parent->left_open && child->right_open) {
    if (child->level != parent->level) {
      return child->level > parent->level;
    }

    return parent->associativity == LCPA_LEFT;
  }

  return TRUE;
}

/* A chart with at most this many waiter buckets is searched by scanning them; larger ones are indexed in the waiters map. */
#define L_PARSING_WAITER_SCAN_MAX 8

/* Summary: the bucket of items waiting for symbol at chart_index, or NULL. */
static LParsingWaiter *l_parsing_waiter_find(LParserProcess *process, uint_large chart_index, uint_large symbol) {
  const LParsingChart *chart;
  LParsingWaiter      *bucket;
  const uint_large    *index;
  uint_large           key[2];
  uint_large           i;

  chart = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, chart_index);

  if (chart->waiter_count > L_PARSING_WAITER_SCAN_MAX) {
    key[0] = chart_index;
    key[1] = symbol;
    index  = nexus_data_word_map_get(process->waiters, key);

    return index == NULL ? NULL : (LParsingWaiter *)nexus_data_chunked_list_at(&process->waiter_list, index[0]);
  }

  for (i = 0; i < chart->waiter_count; i++) {
    bucket = (LParsingWaiter *)nexus_data_chunked_list_at(&process->waiter_list, chart->waiter_first + i);

    if (bucket->symbol == symbol) {
      return bucket;
    }
  }

  return NULL;
}

/* Summary: starts the bucket of items waiting for symbol at chart_index, which has none yet, with one item. */
static void l_parsing_waiter_add(LParserProcess *process, uint_large chart_index, uint_large symbol, uint_large item) {
  LParsingChart  *chart;
  LParsingWaiter *bucket;
  uint_large      key[2];
  uint_large      index;
  uint_large      i;

  chart  = (LParsingChart *)nexus_data_chunked_list_at(&process->charts, chart_index);
  index  = nexus_data_chunked_list_length_get(&process->waiter_list);
  bucket = (LParsingWaiter *)nexus_data_chunked_list_append_uninitialized(&process->waiter_list);

  bucket->symbol = (uint32)symbol;
  bucket->first  = (uint32)item;
  bucket->last   = (uint32)item;

  if (chart->waiter_count == 0) {
    chart->waiter_first = (uint32)index;
  }

  NEXUS_PROGRAM_INVARIANT(chart->waiter_first + chart->waiter_count == index, "a chart's waiter buckets are consecutive");
  chart->waiter_count++;

  /* Past the scan limit the whole chart moves into the map at once, then each new bucket follows. */
  if (chart->waiter_count > L_PARSING_WAITER_SCAN_MAX) {
    for (i = chart->waiter_count == L_PARSING_WAITER_SCAN_MAX + 1 ? 0 : chart->waiter_count - 1; i < chart->waiter_count; i++) {
      key[0] = chart_index;
      key[1] = ((const LParsingWaiter *)nexus_data_chunked_list_at(&process->waiter_list, chart->waiter_first + i))->symbol;
      *nexus_data_word_map_insert(process->waiters, key) = chart->waiter_first + i;
    }
  }
}

void logos_language_tooling_parsing_parser_process_waiters_advance(LParserProcess *process, uint_large chart, uint_large origin, uint_large symbol,
                                                                   LParsingNodeIndex completed) {
  logos_language_tooling_parsing_parser_process_waiters_marked_advance(process, chart, origin, symbol, completed, 0);
}

void logos_language_tooling_parsing_parser_process_waiters_marked_advance(LParserProcess *process, uint_large chart, uint_large origin,
                                                                          uint_large symbol, LParsingNodeIndex completed, uint_large marker) {
  const LParsingWaiter *bucket;
  LParsingItem         *waiter;
  uint_large            waiting;

  bucket = l_parsing_waiter_find(process, origin, symbol);

  if (bucket == NULL) {
    return;
  }

  waiting = bucket->first;

  while (waiting != LPARSER_INDEX_NONE && !process->limited) {
    waiter = (LParsingItem *)nexus_data_chunked_list_at(&process->items, waiting);

    if (marker == 0 || l_parsing_precedence_allows(process->parser, waiter->production, waiter->dot, marker)) {
      l_parsing_process_advance(process, chart, waiter, (uint_large)waiter->dot + 1, completed, marker);
    }

    waiting = waiter->next_waiter;
  }
}

/* ---------------------------------------------------------------------------- */
/* NULLABLE-CORRECT EARLEY CLOSURE                                               */
/* ---------------------------------------------------------------------------- */

static void l_parsing_process_close(LParserProcess *process, uint_large chart_index) {
  LParsingChart            *chart;
  LParsingItem             *item;
  LParsingItem             *waiter;
  const LParsingProduction *production;
  const LParsingProduction *predicted;
  const LParsingSymbol     *symbol;
  LParsingWaiter           *bucket;
  uint_large                cursor;
  uint_large                waiting;
  uint_large                next;
  uint_large                nullable_key[4];
  uint_large                nullable_node;
  uint_large                marker;
  boolean                   new_bucket;
  LParsingNodeIndex         completed;

  chart = (LParsingChart *)nexus_data_chunked_list_at(&process->charts, chart_index);

  if (chart->closed || process->limited) {
    return;
  }

  cursor = chart->first;

  while (cursor != LPARSER_INDEX_NONE && !process->limited) {
    item       = (LParsingItem *)nexus_data_chunked_list_at(&process->items, cursor);
    production = (const LParsingProduction *)nexus_data_list_at_const(process->parser->productions, item->production);

    if (item->dot == production->length) {
      symbol = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, production->lhs);

      /* A rule with a precedence marks what it completes; a unit production passes the mark of its child on, anything longer drops it. */
      marker = 0;

      if (symbol->marker != 0) {
        marker = symbol->marker;
      } else if (production->length == 1) {
        marker = item->marker;
      }

      completed = logos_language_tooling_parsing_parser_process_node_marked_get(
          process, 0, production->lhs, item->origin, chart_index, symbol->is_rule ? LPGK_RULE : LPGK_EXPRESSION, production->lhs, marker);
      l_parsing_process_alternative_add(process, chart_index, completed, item->prefix, LPARSER_INDEX_NONE);

      if (production->lhs == 0 && item->origin == 0 && chart->root == LPARSER_INDEX_NONE) {
        chart->root = (uint32)completed;
      }

      logos_language_tooling_parsing_parser_process_waiters_marked_advance(process, chart_index, item->origin, production->lhs, completed, marker);
    } else {
      next       = production->rhs[item->dot];
      symbol     = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, next);
      new_bucket = FALSE;

      if (symbol->kind != LCOK_TOKEN && symbol->kind != LCOK_SYMBOL) {
        bucket = l_parsing_waiter_find(process, chart_index, next);

        if (bucket == NULL) {
          l_parsing_waiter_add(process, chart_index, next, cursor);
          new_bucket = TRUE;
        } else {
          waiter              = (LParsingItem *)nexus_data_chunked_list_at(&process->items, bucket->last);
          waiter->next_waiter = (uint32)cursor;
          bucket->last        = (uint32)cursor;
        }

        /* The first item waiting on a Pratt symbol here starts its single deterministic run; later waiters share it. */
        if (new_bucket && symbol->kind == LCOK_PRATT) {
          logos_language_tooling_parsing_pratt_run_start(process, next, chart_index);
        }

        /* A nullable completion may precede this waiter in the same agenda. */
        nullable_key[0] = 0;
        nullable_key[1] = next;
        nullable_key[2] = chart_index;
        nullable_key[3] = 0;
        l_parsing_memo_select(process->node_memo, &process->node_memo_chart, chart_index);

        if (l_parsing_memo_get(process->node_memo, nullable_key, &nullable_node)) {
          l_parsing_process_advance(process, chart_index, item, (uint_large)item->dot + 1, nullable_node, 0);
        }

        waiting = symbol->first_production;

        while (waiting != LPARSER_INDEX_NONE && !process->limited) {
          predicted = (const LParsingProduction *)nexus_data_list_at_const(process->parser->productions, waiting);
          l_parsing_process_seed(process, chart_index, waiting);
          waiting = predicted->next;
        }
      }
    }

    cursor = item->next_chart;
  }

  chart->closed = TRUE;
}

/* ---------------------------------------------------------------------------- */
/* TOKEN TRANSITIONS                                                            */
/* ---------------------------------------------------------------------------- */

/*
Summary: scans a real token, any missing terminal, or an unexpected error leaf.
*/
static uint_large l_parsing_process_scan(LParserProcess *process, uint_large from, const LLexerToken *token, LParsingNodeIndex leaf, uint32 mode) {
  const LParsingChart      *source;
  LParsingChart            *target;
  const LParsingItem       *item;
  const LParsingProduction *production;
  const LParsingSymbol     *symbol;
  const LParsingSymbol     *lhs;
  const LParsingPolicy     *policy;
  LParsingNodeIndex         matched;
  uint_large                chart;
  uint_large                cursor;
  uint_large                terminal;
  uint_large                low;
  uint_large                high;
  uint_large                middle;
  boolean                   allowed;
  boolean                   accepts;

  source = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, from);
  chart  = l_parsing_process_chart(process, mode == 1 ? source->byte_offset : token->lexeme_offset + token->lexeme_width);

  if (chart == LPARSER_INDEX_NONE) {
    return LPARSER_INDEX_NONE;
  }

  cursor = source->first;

  while (cursor != LPARSER_INDEX_NONE && !process->limited) {
    item       = (const LParsingItem *)nexus_data_chunked_list_at(&process->items, cursor);
    production = (const LParsingProduction *)nexus_data_list_at_const(process->parser->productions, item->production);

    if (mode == 2) {
      l_parsing_process_advance(process, chart, item, item->dot, leaf, item->marker);
    } else {
      if (mode == 0 && token->kind >= L_LEXER_TOKEN_KIND_FIRST_USER) {
        lhs    = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, production->lhs);
        policy = &process->parser->policies[lhs->policy];
        low    = 0;
        high   = policy->count;

        while (low < high) {
          middle = low + ((high - low) / 2);

          if (policy->kinds[middle] < token->kind) {
            low = middle + 1;
          } else {
            high = middle;
          }
        }

        allowed = low < policy->count && policy->kinds[low] == token->kind;

        if (allowed) {
          l_parsing_process_advance(process, chart, item, item->dot, leaf, item->marker);
        }
      }

      if (item->dot < production->length) {
        terminal = production->rhs[item->dot];
        symbol   = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, terminal);
        accepts  = symbol->kind == LCOK_TOKEN || symbol->kind == LCOK_SYMBOL;

        if (mode == 1 && symbol->kind == LCOK_SYMBOL) {
          accepts = (symbol->terminal.symbol_mask[0] | symbol->terminal.symbol_mask[1] | symbol->terminal.symbol_mask[2] |
                     symbol->terminal.symbol_mask[3]) != 0;
        }

        if (mode == 0) {
          if (symbol->kind == LCOK_TOKEN) {
            accepts = symbol->terminal.token_kind == token->kind;
          } else if (symbol->kind == LCOK_SYMBOL) {
            accepts = token->kind >= L_LEXER_TOKEN_KIND_FIRST_USER && token->lexeme_width == 1 &&
                      logos_languages_regula_observation_mask_get(symbol->terminal.symbol_mask, process->tree->source[token->lexeme_offset]);
          }
        }

        if (accepts) {
          matched = logos_language_tooling_parsing_parser_process_node_get(process, 0, terminal, from, chart, LPGK_EXPRESSION, terminal);

          if (mode == 1) {
            leaf = logos_language_tooling_parsing_parser_process_node_get(process, 2, terminal, from, chart, LPGK_MISSING_TOKEN, terminal);
          }

          l_parsing_process_alternative_add(process, chart, matched, leaf, LPARSER_INDEX_NONE);
          l_parsing_process_advance(process, chart, item, (uint_large)item->dot + 1, matched, 0);
        }
      }
    }

    cursor = item->next_chart;
  }

  target = (LParsingChart *)nexus_data_chunked_list_at(&process->charts, chart);

  return target->first == LPARSER_INDEX_NONE ? LPARSER_INDEX_NONE : chart;
}

/*
Summary: scans a real token, accepting an item-less chart when a Pratt run took the token.
Description: scan leaves its empty target chart as the last chart when no item advances.
*/
static uint_large l_parsing_process_scan_token(LParserProcess *process, uint_large from, const LLexerToken *token, LParsingNodeIndex leaf) {
  uint_large chart;

  chart = l_parsing_process_scan(process, from, token, leaf, 0);

  if (chart == LPARSER_INDEX_NONE && process->pratt_consumed && !process->limited) {
    chart = nexus_data_chunked_list_length_get(&process->charts) - 1;
  }

  return chart;
}

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTICS AND IMMUTABLE RESULT                                              */
/* ---------------------------------------------------------------------------- */

static void l_parsing_process_diagnostic(LParserProcess *process, LParserDiagnosticKind kind, uint_large chart_index, const LLexerToken *token,
                                         LLexerError lexical_error) {
  LParsingDiagnostic        diagnostic;
  LParsingDiagnostic       *previous;
  const LParsingChart      *chart;
  const LParsingItem       *item;
  const LParsingProduction *production;
  const LParsingSymbol     *symbol;
  uint_large                cursor;
  uint_large                expected;
  uint_large                generation;

  generation = nexus_data_slabbed_list_length_get(process->tree->diagnostics);

  if (kind == LPDK_MISSING_TOKEN && generation > 0) {
    previous = (LParsingDiagnostic *)nexus_data_slabbed_list_at(process->tree->diagnostics, generation - 1);

    if (previous->view.kind == LPDK_MISSING_TOKEN && previous->view.byte_offset == token->lexeme_offset) {
      previous->view.missing_token_count++;
      return;
    }
  }

  if (kind == LPDK_UNEXPECTED_TOKEN && process->last_unexpected != LPARSER_INDEX_NONE) {
    previous = (LParsingDiagnostic *)nexus_data_slabbed_list_at(process->tree->diagnostics, process->last_unexpected);

    if (previous->view.byte_offset + previous->view.byte_width == token->lexeme_offset) {
      previous->view.byte_width += token->lexeme_width;
      return;
    }
  }

  if (generation >= process->parser->limits.max_diagnostics) {
    process->limited = TRUE;
    return;
  }

  diagnostic.view.kind                = kind;
  diagnostic.view.byte_offset         = token->lexeme_offset;
  diagnostic.view.byte_width          = kind == LPDK_MISSING_TOKEN ? 0 : token->lexeme_width;
  diagnostic.view.actual_kind         = token->kind;
  diagnostic.view.lexical_error       = lexical_error;
  diagnostic.expected_begin           = nexus_data_slabbed_list_length_get(process->tree->expectations);
  diagnostic.view.expected_count      = 0;
  diagnostic.view.missing_token_count = kind == LPDK_MISSING_TOKEN ? 1 : 0;
  chart                               = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, chart_index);
  cursor                              = chart->first;

  while (cursor != LPARSER_INDEX_NONE) {
    item       = (const LParsingItem *)nexus_data_chunked_list_at(&process->items, cursor);
    production = (const LParsingProduction *)nexus_data_list_at_const(process->parser->productions, item->production);

    if (item->dot < production->length) {
      expected = production->rhs[item->dot];
      symbol   = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, expected);

      if ((symbol->kind == LCOK_TOKEN || symbol->kind == LCOK_SYMBOL) && process->expectation_marks[expected] != generation + 1) {
        process->expectation_marks[expected] = generation + 1;
        (void)nexus_data_slabbed_list_append(process->tree->expectations, &symbol->terminal);
        diagnostic.view.expected_count++;
      }
    }

    cursor = item->next_chart;
  }

  /* Pratt runs that ended on this token contribute the terminals their table allowed there. */
  diagnostic.view.expected_count += logos_language_tooling_parsing_pratt_expectations_flush(process, diagnostic.expected_begin);

  (void)nexus_data_slabbed_list_append(process->tree->diagnostics, &diagnostic);
  process->last_unexpected = kind == LPDK_UNEXPECTED_TOKEN ? generation : LPARSER_INDEX_NONE;
}

static LParsingNodeIndex l_parsing_process_leaf(LParserProcess *process, const LLexerToken *token, LLexerError error) {
  LParsingNodeIndex  leaf;
  LParsingGreenNode *node;

  leaf = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_TOKEN, token->lexeme_width, LPARSER_INDEX_NONE);

  if (leaf != LPARSER_INDEX_NONE) {
    node                     = (LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, leaf);
    node->aux                = (uint32)token->lexeme_offset;
    node->payload.token_kind = token->kind;
    node->lexical_error      = (uint8)error;
  }

  return leaf;
}

static void l_parsing_process_finish(LParserProcess *process, LParserStatus status, LParsingNodeIndex eof) {
  LParserTree         *tree;
  LParsingGreenNode   *node;
  const LParsingChart *chart;
  const LParserToken  *record;
  LParsingNodeIndex    content;
  LParsingNodeIndex    sequence;
  LParsingNodeIndex    tail;
  uint_large           i;
  LLexerToken          token;

  tree               = process->tree;
  process->finishing = TRUE;

  if (status == LPS_LIMIT && nexus_data_slabbed_list_length_get(tree->diagnostics) < process->parser->limits.max_diagnostics) {
    token.kind          = L_LEXER_TOKEN_KIND_EOF;
    token.lexeme_offset = process->decided_end;
    token.lexeme_width  = 0;
    l_parsing_process_diagnostic(process, LPDK_LIMIT, process->current_chart, &token, LLE_NONE);
  }

  if (status == LPS_ACCEPTED || status == LPS_RECOVERED) {
    chart   = (const LParsingChart *)nexus_data_chunked_list_at(&process->charts, process->current_chart);
    content = chart->root;
    NEXUS_PROGRAM_INVARIANT(content != LPARSER_INDEX_NONE && eof != LPARSER_INDEX_NONE, "accepted result must include grammar and EOF");

    tree->root = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_SOURCE, tree->source_size, LPARSER_INDEX_NONE);
    logos_language_tooling_parsing_parser_process_alternative_append(process, tree->root, content, eof);
  } else {
    /* Only a failed or limited parse needs the raw cover of its tokens, so it is built here instead of for every token. */
    content = LPARSER_INDEX_NONE;

    for (i = 0; i < nexus_data_chunked_list_length_get(&tree->tokens); i++) {
      record = (const LParserToken *)nexus_data_chunked_list_at(&tree->tokens, i);

      if (record->leaf == LPARSER_INDEX_NONE) {
        continue;
      }

      if (content == LPARSER_INDEX_NONE) {
        content = record->leaf;
        continue;
      }

      sequence =
          logos_language_tooling_parsing_parser_process_node_create(process, LPGK_SEQUENCE, record->offset + record->width, LPARSER_INDEX_NONE);
      logos_language_tooling_parsing_parser_process_alternative_append(process, sequence, content, record->leaf);
      content = sequence;
    }

    if (process->decided_end < tree->source_size) {
      tail      = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_UNPARSED, tree->source_size - process->decided_end,
                                                                            LPARSER_INDEX_NONE);
      node      = (LParsingGreenNode *)nexus_data_chunked_list_at(&tree->nodes, tail);
      node->aux = (uint32)process->decided_end;

      if (content == LPARSER_INDEX_NONE) {
        content = tail;
      } else {
        sequence = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_SEQUENCE, tree->source_size, LPARSER_INDEX_NONE);
        logos_language_tooling_parsing_parser_process_alternative_append(process, sequence, content, tail);
        content = sequence;
      }
    }

    tree->root = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_ERROR, tree->source_size, LPARSER_INDEX_NONE);
    logos_language_tooling_parsing_parser_process_alternative_append(process, tree->root, content, LPARSER_INDEX_NONE);
  }

  tree->status    = status;
  process->status = status;
  process->done   = TRUE;

  /* The result may be destroyed before the process is reused. */
  logos_language_tooling_lexing_lexer_process_reset(process->lexer);
}

/* ---------------------------------------------------------------------------- */
/* WORKSPACE LIFECYCLE                                                           */
/* ---------------------------------------------------------------------------- */

LParserTree *logos_language_tooling_parsing_parser_process_tree_create(LParser *parser) {
  LParserTree *tree;

  tree                  = (LParserTree *)nexus_memory_allocate(NEXUS_SIZEOF(*tree));
  tree->parser          = parser;
  tree->source_capacity = parser->limits.initial_source_bytes;
  tree->source_size     = 0;
  tree->source          = (byte *)nexus_memory_allocate(tree->source_capacity);
  nexus_data_chunked_list_initialize(&tree->nodes, NEXUS_SIZEOF(LParsingGreenNode), 12);
  nexus_data_chunked_list_initialize(&tree->alternatives, NEXUS_SIZEOF(LParsingAlternative), 12);
  nexus_data_chunked_list_initialize(&tree->tokens, NEXUS_SIZEOF(LParserToken), 12);
  tree->diagnostics  = nexus_data_slabbed_list_create(NEXUS_SIZEOF(LParsingDiagnostic), 16, 32);
  tree->expectations = nexus_data_slabbed_list_create(NEXUS_SIZEOF(LParserExpectation), 32, 64);
  tree->root         = LPARSER_INDEX_NONE;
  tree->status       = LPS_NEED_INPUT;
  tree->taken        = FALSE;

  return tree;
}

void logos_language_tooling_parsing_parser_process_tree_clear(LParserTree *tree) {
  nexus_data_chunked_list_clear(&tree->nodes);
  nexus_data_chunked_list_clear(&tree->alternatives);
  nexus_data_chunked_list_clear(&tree->tokens);
  nexus_data_slabbed_list_clear(tree->diagnostics);
  nexus_data_slabbed_list_clear(tree->expectations);
  tree->source_size = 0;
  tree->root        = LPARSER_INDEX_NONE;
  tree->status      = LPS_NEED_INPUT;
}

NexusError *logos_language_tooling_parsing_parser_process_source_append(LParserTree *tree, const LParser *parser, const byte *chunk,
                                                                        uint_large chunk_size) {
  uint_large required;
  uint_large capacity;
  uint_large offset;

  offset = tree->source_size;

  if (chunk_size > parser->limits.max_source_bytes - offset) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_PARSER_SOURCE_LIMIT, "parser source byte limit exceeded");
  }

  required = offset + chunk_size;
  capacity = tree->source_capacity;

  while (capacity < required) {
    if (capacity > parser->limits.max_source_bytes / 2) {
      capacity = parser->limits.max_source_bytes;
    } else {
      capacity *= 2;
    }
  }

  if (capacity != tree->source_capacity) {
    tree->source          = (byte *)nexus_memory_reallocate(tree->source, capacity);
    tree->source_capacity = capacity;
  }

  if (chunk_size > 0) {
    nexus_memory_bytes_copy(chunk, tree->source + offset, chunk_size);
  }

  tree->source_size = required;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static void l_parsing_process_result_create(LParserProcess *process) {
  process->tree = logos_language_tooling_parsing_parser_process_tree_create(process->parser);
}

static void l_parsing_process_earley_reset(LParserProcess *process);

static LParserProcess *l_parsing_process_earley_create(LParser *parser) {
  LParserProcess *process;

  NEXUS_PROGRAM_PRECONDITION(parser != NULL, "parser must not be NULL");

  process         = (LParserProcess *)nexus_memory_allocate(NEXUS_SIZEOF(*process));
  process->parser = parser;
  process->lexer  = logos_language_tooling_lexing_lexer_process_create(parser->lexer);
  process->tree   = NULL;
  nexus_data_chunked_list_initialize(&process->charts, NEXUS_SIZEOF(LParsingChart), 12);
  nexus_data_chunked_list_initialize(&process->items, NEXUS_SIZEOF(LParsingItem), 12);
  process->expectation_marks = (uint_large *)nexus_memory_allocate(nexus_data_list_length_get(parser->symbols) * NEXUS_SIZEOF(uint_large));
  process->pratt_runs        = nexus_data_list_create(NEXUS_SIZEOF(LParsingPrattRun), 16);
  nexus_data_chunked_list_initialize(&process->pratt_frames, NEXUS_SIZEOF(LParsingPrattFrame), 8);
  process->pratt_expected         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  process->item_memo              = nexus_data_word_map_create(4, 1, 128);
  process->node_memo              = nexus_data_word_map_create(4, 1, 128);
  process->alternative_memo       = nexus_data_word_map_create(4, 1, 16);
  process->item_memo_chart        = LPARSER_INDEX_NONE;
  process->node_memo_chart        = LPARSER_INDEX_NONE;
  process->alternative_memo_chart = LPARSER_INDEX_NONE;
  process->waiters                = nexus_data_word_map_create(2, 1, 16);
  nexus_data_chunked_list_initialize(&process->waiter_list, NEXUS_SIZEOF(LParsingWaiter), 12);

  parser->process_count++;
  l_parsing_process_earley_reset(process);

  return process;
}

static void l_parsing_process_earley_destroy(LParserProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  logos_language_tooling_lexing_lexer_process_destroy(process->lexer);

  if (process->tree != NULL) {
    logos_language_tooling_parsing_syntax_tree_destroy(process->tree);
  }

  nexus_data_word_map_destroy(process->item_memo);
  nexus_data_word_map_destroy(process->node_memo);
  nexus_data_word_map_destroy(process->alternative_memo);
  nexus_data_word_map_destroy(process->waiters);
  nexus_data_chunked_list_release(&process->waiter_list);
  nexus_data_chunked_list_release(&process->charts);
  nexus_data_chunked_list_release(&process->items);
  nexus_data_list_destroy(process->pratt_runs);
  nexus_data_chunked_list_release(&process->pratt_frames);
  nexus_data_list_destroy(process->pratt_expected);
  nexus_memory_deallocate(process->expectation_marks);
  process->parser->process_count--;
  nexus_memory_deallocate(process);
}

static void l_parsing_process_earley_reset(LParserProcess *process) {
  const LParsingSymbol     *start;
  const LParsingProduction *production;
  uint_large                index;

  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  logos_language_tooling_lexing_lexer_process_reset(process->lexer);
  nexus_data_chunked_list_clear(&process->charts);
  nexus_data_chunked_list_clear(&process->items);
  nexus_data_word_map_clear(process->item_memo);
  nexus_data_word_map_clear(process->node_memo);
  nexus_data_word_map_clear(process->alternative_memo);
  nexus_data_word_map_clear(process->waiters);
  nexus_data_chunked_list_clear(&process->waiter_list);
  process->item_memo_chart        = LPARSER_INDEX_NONE;
  process->node_memo_chart        = LPARSER_INDEX_NONE;
  process->alternative_memo_chart = LPARSER_INDEX_NONE;
  logos_language_tooling_parsing_pratt_runs_reset(process);
  process->pratt_consumed = FALSE;
  nexus_memory_bytes_clear(process->expectation_marks, nexus_data_list_length_get(process->parser->symbols) * NEXUS_SIZEOF(uint_large));

  if (process->tree == NULL) {
    l_parsing_process_result_create(process);
  } else {
    logos_language_tooling_parsing_parser_process_tree_clear(process->tree);
  }

  process->current_chart   = l_parsing_process_chart(process, 0);
  process->decided_end     = 0;
  process->last_unexpected = LPARSER_INDEX_NONE;
  process->ready_for_chunk = TRUE;
  process->last_chunk      = FALSE;
  process->limited         = FALSE;
  process->finishing       = FALSE;
  process->done            = FALSE;
  process->status          = LPS_NEED_INPUT;
  start                    = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, 0);
  index                    = start->first_production;

  while (index != LPARSER_INDEX_NONE && !process->limited) {
    production = (const LParsingProduction *)nexus_data_list_at_const(process->parser->productions, index);
    l_parsing_process_seed(process, process->current_chart, index);
    index = production->next;
  }
}

static NexusError *l_parsing_process_earley_chunk_add(LParserProcess *process, const byte *chunk, uint_large chunk_size, boolean is_last_chunk) {
  uint_large  offset;
  NexusError *error;

  NEXUS_PROGRAM_PRECONDITION(process != NULL && (chunk != NULL || chunk_size == 0), "process and nonempty input must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(process->tree != NULL && process->ready_for_chunk && !process->last_chunk && !process->done,
                             "input needs a reset process with no outstanding or final chunk");

  offset = process->tree->source_size;
  error  = logos_language_tooling_parsing_parser_process_source_append(process->tree, process->parser, chunk, chunk_size);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return error;
  }

  process->ready_for_chunk = FALSE;
  process->last_chunk      = is_last_chunk;
  logos_language_tooling_lexing_lexer_process_chunk_add(process->lexer, process->tree->source + offset, chunk_size, is_last_chunk);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

/* ---------------------------------------------------------------------------- */
/* STREAMING EXECUTION AND BOUNDED STRUCTURAL RECOVERY                            */
/* ---------------------------------------------------------------------------- */

static LParserStatus l_parsing_process_earley_parse(LParserProcess *process, uint_large token_budget) {
  LLexerStatus      lexer_status;
  LParserToken      record;
  LLexerToken       token;
  LLexerToken       missing;
  LLexerError       error;
  LParsingChart    *chart;
  LParsingNodeIndex leaf;
  LParsingNodeIndex error_node;
  uint_large        consumed;
  uint_large        first_run;
  uint_large        next;
  uint_large        trial;
  uint_large        original;
  uint32            inserted;

  NEXUS_PROGRAM_PRECONDITION(process != NULL && token_budget > 0, "process must not be NULL and token budget must be positive");

  if (process->done) {
    return process->status;
  }

  NEXUS_PROGRAM_PRECONDITION(process->tree != NULL && !process->ready_for_chunk, "add input before parsing");

  consumed = 0;

  while (consumed < token_budget && !process->done) {
    if (process->limited || nexus_data_chunked_list_length_get(&process->tree->tokens) >= process->parser->limits.max_tokens) {
      l_parsing_process_finish(process, LPS_LIMIT, LPARSER_INDEX_NONE);
      break;
    }

    lexer_status = logos_language_tooling_lexing_lexer_process_lex(process->lexer, &token);

    if (lexer_status == LLS_NEED_INPUT) {
      process->ready_for_chunk = TRUE;
      process->status          = LPS_NEED_INPUT;

      return process->status;
    }

    NEXUS_PROGRAM_INVARIANT(lexer_status == LLS_TOKEN || lexer_status == LLS_ERROR, "lexer must produce a token or request input before EOF");

    error         = lexer_status == LLS_ERROR ? logos_language_tooling_lexing_lexer_process_error_get(process->lexer, NULL) : LLE_NONE;
    record.kind   = (uint32)token.kind;
    record.offset = token.lexeme_offset;
    record.width  = token.lexeme_width;
    record.error  = (uint8)error;
    leaf          = l_parsing_process_leaf(process, &token, error);
    record.leaf   = (uint32)leaf;
    (void)nexus_data_chunked_list_append(&process->tree->tokens, &record);

    if (!process->limited) {
      process->decided_end = token.lexeme_offset + token.lexeme_width;
    }

    if (process->limited) {
      l_parsing_process_finish(process, LPS_LIMIT, LPARSER_INDEX_NONE);
      break;
    }

    consumed++;

    /*
    The token is lexed before the chart is closed. Pratt runs started at earlier charts take it first, so one that
    ends on it completes into this still-open chart; runs the closure then starts take it afterwards.
    */
    process->pratt_consumed = FALSE;
    nexus_data_list_clear(process->pratt_expected);
    logos_language_tooling_parsing_pratt_runs_feed(process, 0, &token, leaf, process->current_chart);
    first_run = nexus_data_list_length_get(process->pratt_runs);
    l_parsing_process_close(process, process->current_chart);
    logos_language_tooling_parsing_pratt_runs_feed(process, first_run, &token, leaf, process->current_chart);

    if (process->limited) {
      l_parsing_process_finish(process, LPS_LIMIT, LPARSER_INDEX_NONE);
      break;
    }

    if (error != LLE_NONE) {
      l_parsing_process_diagnostic(process, LPDK_LEXICAL, process->current_chart, &token, error);
    }

    if (token.kind == L_LEXER_TOKEN_KIND_EOF) {
      inserted = 0;
      chart    = (LParsingChart *)nexus_data_chunked_list_at(&process->charts, process->current_chart);

      while (chart->root == LPARSER_INDEX_NONE && !process->limited && inserted < process->parser->limits.recovery_insertions) {
        missing.kind          = L_LEXER_TOKEN_KIND_ERROR;
        missing.lexeme_offset = token.lexeme_offset;
        missing.lexeme_width  = 0;
        trial                 = l_parsing_process_scan(process, process->current_chart, &missing, LPARSER_INDEX_NONE, 1);

        if (trial == LPARSER_INDEX_NONE) {
          break;
        }

        l_parsing_process_diagnostic(process, LPDK_MISSING_TOKEN, process->current_chart, &token, LLE_NONE);
        process->current_chart = trial;
        l_parsing_process_close(process, trial);
        chart = (LParsingChart *)nexus_data_chunked_list_at(&process->charts, trial);
        inserted++;
      }

      if (process->limited) {
        l_parsing_process_finish(process, LPS_LIMIT, LPARSER_INDEX_NONE);
      } else if (chart->root == LPARSER_INDEX_NONE) {
        l_parsing_process_diagnostic(process, LPDK_UNEXPECTED_EOF, process->current_chart, &token, LLE_NONE);
        l_parsing_process_finish(process, process->limited ? LPS_LIMIT : LPS_REJECTED, LPARSER_INDEX_NONE);
      } else {
        l_parsing_process_finish(process, nexus_data_slabbed_list_length_get(process->tree->diagnostics) == 0 ? LPS_ACCEPTED : LPS_RECOVERED, leaf);
      }

      break;
    }

    original = process->current_chart;
    next     = l_parsing_process_scan_token(process, original, &token, leaf);

    if (next != LPARSER_INDEX_NONE && !process->limited) {
      process->current_chart   = next;
      process->last_unexpected = LPARSER_INDEX_NONE;
      continue;
    }

    if (!process->limited && error == LLE_NONE && process->parser->limits.recovery_insertions > 0) {
      missing.kind          = L_LEXER_TOKEN_KIND_ERROR;
      missing.lexeme_offset = token.lexeme_offset;
      missing.lexeme_width  = 0;
      trial                 = original;
      inserted              = 0;

      while (inserted < process->parser->limits.recovery_insertions && !process->limited) {
        trial = l_parsing_process_scan(process, trial, &missing, LPARSER_INDEX_NONE, 1);

        if (trial == LPARSER_INDEX_NONE) {
          break;
        }

        inserted++;
        first_run = nexus_data_list_length_get(process->pratt_runs);
        l_parsing_process_close(process, trial);
        logos_language_tooling_parsing_pratt_runs_feed(process, first_run, &token, leaf, trial);
        next = l_parsing_process_scan_token(process, trial, &token, leaf);

        if (next != LPARSER_INDEX_NONE) {
          break;
        }
      }

      if (next != LPARSER_INDEX_NONE && !process->limited) {
        l_parsing_process_diagnostic(process, LPDK_MISSING_TOKEN, original, &token, LLE_NONE);

        if (!process->limited) {
          LParsingDiagnostic *repair;

          repair                           = (LParsingDiagnostic *)nexus_data_slabbed_list_at(process->tree->diagnostics,
                                                                                              nexus_data_slabbed_list_length_get(process->tree->diagnostics) - 1);
          repair->view.missing_token_count = inserted;
        }

        process->current_chart   = next;
        process->last_unexpected = LPARSER_INDEX_NONE;
        continue;
      }
    }

    if (error == LLE_NONE && !process->limited) {
      l_parsing_process_diagnostic(process, LPDK_UNEXPECTED_TOKEN, original, &token, LLE_NONE);
    }

    if (!process->limited) {
      error_node = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_ERROR, token.lexeme_width, LPARSER_INDEX_NONE);
      logos_language_tooling_parsing_parser_process_alternative_append(process, error_node, leaf, LPARSER_INDEX_NONE);
      next = l_parsing_process_scan(process, original, &token, error_node, 2);

      if (next == LPARSER_INDEX_NONE && !process->limited) {
        next = l_parsing_process_chart(process, token.lexeme_offset + token.lexeme_width);
      }

      if (next != LPARSER_INDEX_NONE) {
        process->current_chart = next;
      }
    }

    if (process->limited) {
      l_parsing_process_finish(process, LPS_LIMIT, LPARSER_INDEX_NONE);
    }
  }

  if (!process->done) {
    process->status = LPS_PROGRESS;
  }

  return process->status;
}

static LParserTree *l_parsing_process_earley_tree_take(LParserProcess *process) {
  LParserTree *tree;

  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(process->done && process->tree != NULL, "taking a tree requires a terminated untaken result");

  tree          = process->tree;
  tree->taken   = TRUE;
  process->tree = NULL;
  process->parser->tree_count++;

  return tree;
}

/* ---------------------------------------------------------------------------- */
/* PUBLIC PROCESS API: DISPATCH BY ENGINE                                        */
/* ---------------------------------------------------------------------------- */

LParserProcess *logos_language_tooling_parsing_parser_process_create(LParser *parser) {
  NEXUS_PROGRAM_PRECONDITION(parser != NULL, "parser must not be NULL");

  if (parser->engine == LPE_BACKTRACKING) {
    return logos_language_tooling_parsing_backtracking_process_create(parser);
  }

  return l_parsing_process_earley_create(parser);
}

void logos_language_tooling_parsing_parser_process_destroy(LParserProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  if (((const LParserProcessHeader *)process)->parser->engine == LPE_BACKTRACKING) {
    logos_language_tooling_parsing_backtracking_process_destroy(process);

    return;
  }

  l_parsing_process_earley_destroy(process);
}

void logos_language_tooling_parsing_parser_process_reset(LParserProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  if (((const LParserProcessHeader *)process)->parser->engine == LPE_BACKTRACKING) {
    logos_language_tooling_parsing_backtracking_process_reset(process);

    return;
  }

  l_parsing_process_earley_reset(process);
}

NexusError *logos_language_tooling_parsing_parser_process_chunk_add(LParserProcess *process, const byte *chunk, uint_large chunk_size,
                                                                    boolean is_last_chunk) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL && (chunk != NULL || chunk_size == 0), "process and nonempty input must not be NULL");

  if (((const LParserProcessHeader *)process)->parser->engine == LPE_BACKTRACKING) {
    return logos_language_tooling_parsing_backtracking_process_chunk_add(process, chunk, chunk_size, is_last_chunk);
  }

  return l_parsing_process_earley_chunk_add(process, chunk, chunk_size, is_last_chunk);
}

LParserStatus logos_language_tooling_parsing_parser_process_parse(LParserProcess *process, uint_large token_budget) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL && token_budget > 0, "process must not be NULL and token budget must be positive");

  if (((const LParserProcessHeader *)process)->parser->engine == LPE_BACKTRACKING) {
    return logos_language_tooling_parsing_backtracking_process_parse(process, token_budget);
  }

  return l_parsing_process_earley_parse(process, token_budget);
}

LParserTree *logos_language_tooling_parsing_parser_process_tree_take(LParserProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  if (((const LParserProcessHeader *)process)->parser->engine == LPE_BACKTRACKING) {
    return logos_language_tooling_parsing_backtracking_process_tree_take(process);
  }

  return l_parsing_process_earley_tree_take(process);
}
