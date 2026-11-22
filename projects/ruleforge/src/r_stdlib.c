#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The native part of the standard library. The library itself is Ruleforge: std/<name>.rf are the modules `std.<name>`, embedded in the
engine. What the language cannot express is declared there as `export intrinsic func Name(...) -> T;` and implemented here, bound by
the module and function name. Everything here is deterministic, works on immutable values and is bounded; an error is a diagnostic at
the call of the script that made it.
*/

/* ---------------------------------------------------------------------------- */
/* HELPERS                                                                      */
/* ---------------------------------------------------------------------------- */

static boolean r_stdlib_number_need(REval *eval, RuleforgeSpan span, const RuleforgeValue *value, const char *function, f_real *out) {
  if (!ruleforge_value_number_get(value, out)) {
    ruleforge_eval_fail(eval, "RF0500", span, NULL, "%s needs a number, got %s", function, ruleforge_value_type_name_get(value->kind));

    return FALSE;
  }

  return TRUE;
}

/* Summary: the items of a list or the rows of a dataset as values; FALSE (reported) for anything else. */
static boolean r_stdlib_items_get(REval *eval, RuleforgeSpan span, const RuleforgeValue *value, const char *function, uint32 *out_count,
                                  RuleforgeValue **out_items) {
  uint32 i;

  if (value->kind == RVK_LIST) {
    *out_count = value->as.list->count;
    *out_items = value->as.list->items;

    return TRUE;
  }

  if (value->kind == RVK_DATASET) {
    *out_count = value->as.dataset->count;
    *out_items = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)*out_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));

    for (i = 0; i < *out_count; i++) {
      (*out_items)[i].kind          = RVK_RECORD;
      (*out_items)[i].is_identifier = FALSE;
      (*out_items)[i].as.record     = value->as.dataset->rows[i];
    }

    return TRUE;
  }

  ruleforge_eval_fail(eval, "RF0501", span, NULL, "%s needs a list or a dataset, got %s", function, ruleforge_value_type_name_get(value->kind));

  return FALSE;
}

/* Summary: stable merge sort of an index array by key (ascending); ties keep their original order. */
static void r_stdlib_sort_indices(uint32 *indices, uint32 *scratch, const f_real *keys, uint32 count) {
  uint32  width;
  uint32  start;
  uint32  left;
  uint32  right;
  uint32  left_end;
  uint32  right_end;
  uint32  out;
  uint32 *from;
  uint32 *destination;
  uint32 *swap;

  from        = indices;
  destination = scratch;

  for (width = 1; width < count; width *= 2) {
    for (start = 0; start < count; start += 2 * width) {
      left      = start;
      left_end  = start + width < count ? start + width : count;
      right     = left_end;
      right_end = start + (2 * width) < count ? start + (2 * width) : count;
      out       = start;

      while (left < left_end && right < right_end) {
        if (keys[from[right]] < keys[from[left]]) {
          destination[out++] = from[right++];
        } else {
          destination[out++] = from[left++];
        }
      }

      while (left < left_end) {
        destination[out++] = from[left++];
      }

      while (right < right_end) {
        destination[out++] = from[right++];
      }
    }

    swap        = from;
    from        = destination;
    destination = swap;
  }

  if (from != indices) {
    nexus_memory_bytes_copy(from, indices, (uint_large)count * NEXUS_SIZEOF(uint32));
  }
}

/* ---------------------------------------------------------------------------- */
/* std.collections                                                              */
/* ---------------------------------------------------------------------------- */

static RuleforgeValue r_collections_count(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Count", &items_count, &items)) {
    return ruleforge_value_none();
  }

  return ruleforge_value_integer((int64)items_count);
}

static RuleforgeValue r_collections_take(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *list;
  uint32          take;
  uint32          skip;
  uint32          i;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Take", &items_count, &items)) {
    return ruleforge_value_none();
  }

  if (arguments[1].kind != RVK_INT || arguments[1].as.integer < 0 ||
      (arguments[2].kind != RVK_NONE && (arguments[2].kind != RVK_INT || arguments[2].as.integer < 0))) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.Take(list, count, skip) needs non-negative ints");

    return ruleforge_value_none();
  }

  skip = arguments[2].kind == RVK_INT ? (uint32)arguments[2].as.integer : 0;
  take = (uint32)arguments[1].as.integer;

  if (skip > items_count) {
    skip = items_count;
  }

  if (take > items_count - skip) {
    take = items_count - skip;
  }

  list = ruleforge_value_list_create(eval->compilation, take);

  for (i = 0; i < take; i++) {
    list->items[i] = items[skip + i];
  }

  return ruleforge_value_list(list);
}

static RuleforgeValue r_collections_reverse(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *list;
  uint32          i;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Reverse", &items_count, &items)) {
    return ruleforge_value_none();
  }

  list = ruleforge_value_list_create(eval->compilation, items_count);

  for (i = 0; i < items_count; i++) {
    list->items[i] = items[items_count - 1 - i];
  }

  return ruleforge_value_list(list);
}

static RuleforgeValue r_collections_concat(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          left_count;
  uint32          right_count;
  RuleforgeValue *left;
  RuleforgeValue *right;
  RuleforgeList  *list;
  uint32          i;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Concat", &left_count, &left) ||
      !r_stdlib_items_get(eval, span, &arguments[1], "collections.Concat", &right_count, &right)) {
    return ruleforge_value_none();
  }

  /* The newest version of a list with room left is extended in place; see RuleforgeList. */
  if (arguments[0].kind == RVK_LIST && arguments[0].as.list->used != NULL && *arguments[0].as.list->used == left_count &&
      (uint_large)left_count + right_count <= arguments[0].as.list->capacity) {
    list           = (RuleforgeList *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*list));
    list->items    = arguments[0].as.list->items;
    list->capacity = arguments[0].as.list->capacity;
    list->used     = arguments[0].as.list->used;
    list->count    = left_count + right_count;

    for (i = 0; i < right_count; i++) {
      list->items[left_count + i] = right[i];
    }

    *list->used = list->count;

    return ruleforge_value_list(list);
  }

  {
    uint32 total;
    uint32 capacity;

    total    = left_count + right_count;
    capacity = total < 8 ? 8 : total * 2;
    list     = (RuleforgeList *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*list));

    list->count    = total;
    list->capacity = capacity;
    list->items = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)capacity + 1U) * NEXUS_SIZEOF(RuleforgeValue));
    list->used  = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(uint32));
    *list->used = total;
  }

  for (i = 0; i < left_count; i++) {
    list->items[i] = left[i];
  }

  for (i = 0; i < right_count; i++) {
    list->items[left_count + i] = right[i];
  }

  return ruleforge_value_list(list);
}

static RuleforgeValue r_collections_range(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeList *list;
  int64          from;
  int64          upper;
  int64          i;

  (void)count;

  if (arguments[0].kind != RVK_INT || arguments[1].kind != RVK_INT) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.Range(from, to) needs ints");

    return ruleforge_value_none();
  }

  from  = arguments[0].as.integer;
  upper = arguments[1].as.integer;

  if (upper < from || upper - from > 100000) {
    ruleforge_eval_fail(eval, "RF0506", span, "ranges are bounded upper 100000 elements", "collections.Range(%lld, %lld) is empty or too large",
                        (long long)from, (long long)upper);

    return ruleforge_value_none();
  }

  list = ruleforge_value_list_create(eval->compilation, (uint32)(upper - from));

  for (i = from; i < upper; i++) {
    list->items[i - from] = ruleforge_value_integer(i);
  }

  return ruleforge_value_list(list);
}

/* Summary: the field of every record of a list, as a list. */
static RuleforgeValue r_collections_pluck(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *list;
  uint32          i;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Pluck", &items_count, &items)) {
    return ruleforge_value_none();
  }

  if (arguments[1].kind != RVK_STRING) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.Pluck(list, field) needs a field name");

    return ruleforge_value_none();
  }

  list = ruleforge_value_list_create(eval->compilation, items_count);

  for (i = 0; i < items_count; i++) {
    const RuleforgeValue *field;

    field = items[i].kind == RVK_RECORD ? ruleforge_value_record_field_get(items[i].as.record, arguments[1].as.string) : NULL;

    if (field == NULL) {
      ruleforge_eval_fail(eval, "RF0312", span, NULL, "collections.Pluck: item %lu has no field `%s`", (unsigned long)i, arguments[1].as.string);

      return ruleforge_value_none();
    }

    list->items[i] = *field;
  }

  return ruleforge_value_list(list);
}

/* Summary: the items sorted by a numeric field (or the numbers themselves), descending when asked; items without a value sort last, ties keep their order. */
static RuleforgeValue r_collections_sort(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *list;
  uint32         *indices;
  uint32         *scratch;
  f_real         *keys;
  uint32          i;
  boolean         descending;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Sort", &items_count, &items)) {
    return ruleforge_value_none();
  }

  if ((arguments[1].kind != RVK_NONE && arguments[1].kind != RVK_STRING) || (arguments[2].kind != RVK_NONE && arguments[2].kind != RVK_BOOL)) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.Sort(items, field, descending) needs a field name or none");

    return ruleforge_value_none();
  }

  descending = arguments[2].kind == RVK_BOOL && arguments[2].as.flag;
  indices    = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(uint32));
  scratch    = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(uint32));
  keys       = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(f_real));

  for (i = 0; i < items_count; i++) {
    const RuleforgeValue *field;
    f_real                number;

    indices[i] = i;
    field      = &items[i];

    /* Without a field name the items are the numbers themselves. */
    if (arguments[1].kind == RVK_STRING) {
      field = items[i].kind == RVK_RECORD ? ruleforge_value_record_field_get(items[i].as.record, arguments[1].as.string) : NULL;

      if (field == NULL) {
        ruleforge_eval_fail(eval, "RF0312", span, NULL, "collections.Sort: item %lu has no field `%s`", (unsigned long)i, arguments[1].as.string);

        return ruleforge_value_none();
      }
    }

    if (!ruleforge_value_number_get(field, &number)) {
      /* No value: sorts after every value in either direction. */
      keys[i] = 1.0e300;
    } else {
      keys[i] = descending ? -number : number;
    }
  }

  r_stdlib_sort_indices(indices, scratch, keys, items_count);
  list = ruleforge_value_list_create(eval->compilation, items_count);

  for (i = 0; i < items_count; i++) {
    list->items[i] = items[indices[i]];
  }

  return ruleforge_value_list(list);
}

/* Summary: the numbers among the items (or among a field of their records), in order. */
static RuleforgeValue r_collections_values(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *list;
  RuleforgeValue *numbers;
  uint32          kept;
  uint32          i;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Values", &items_count, &items)) {
    return ruleforge_value_none();
  }

  numbers = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
  kept    = 0;

  for (i = 0; i < items_count; i++) {
    const RuleforgeValue *value;
    f_real                number;

    value = &items[i];

    if (items[i].kind == RVK_RECORD) {
      if (arguments[1].kind != RVK_STRING) {
        ruleforge_eval_fail(eval, "RF0507", span, NULL, "collections.Values needs the field name when the items are records");

        return ruleforge_value_none();
      }

      value = ruleforge_value_record_field_get(items[i].as.record, arguments[1].as.string);

      if (value == NULL) {
        ruleforge_eval_fail(eval, "RF0312", span, NULL, "collections.Values: item %lu has no field `%s`", (unsigned long)i, arguments[1].as.string);

        return ruleforge_value_none();
      }
    }

    /* A missing value is left out rather than counted as zero. */
    if (ruleforge_value_number_get(value, &number)) {
      numbers[kept++] = *value;
    }
  }

  list = ruleforge_value_list_create(eval->compilation, kept);

  for (i = 0; i < kept; i++) {
    list->items[i] = numbers[i];
  }

  return ruleforge_value_list(list);
}

/*
Summary: for every item, its place (0 is first) among the items that have a number in the field, or none for an item without one.
Description: the first place is the highest value when `descending` is true, the lowest otherwise; equal values keep their order.
*/
static RuleforgeValue r_collections_ranks(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *list;
  uint32         *indices;
  f_real         *keys;
  uint32          priced;
  uint32          i;
  boolean         descending;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Ranks", &items_count, &items)) {
    return ruleforge_value_none();
  }

  descending = arguments[2].kind == RVK_BOOL && arguments[2].as.flag;
  indices    = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(uint32));
  keys       = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(f_real));
  list       = ruleforge_value_list_create(eval->compilation, items_count);
  priced     = 0;

  for (i = 0; i < items_count; i++) {
    const RuleforgeValue *value;
    f_real                number;

    list->items[i] = ruleforge_value_none();

    if (items[i].kind != RVK_RECORD) {
      ruleforge_eval_fail(eval, "RF0511", span, NULL, "collections.Ranks needs records, item %lu is a %s", (unsigned long)i,
                          ruleforge_value_type_name_get(items[i].kind));

      return ruleforge_value_none();
    }

    value = ruleforge_value_record_field_get(items[i].as.record, arguments[1].as.string);

    if (value == NULL) {
      ruleforge_eval_fail(eval, "RF0312", span, NULL, "collections.Ranks: item %lu has no field `%s`", (unsigned long)i, arguments[1].as.string);

      return ruleforge_value_none();
    }

    if (ruleforge_value_number_get(value, &number)) {
      keys[priced]    = descending ? -number : number;
      indices[priced] = i;
      priced++;
    }
  }

  /* The sort moves a table of positions; the keys stay in the order they were collected. */
  {
    uint32 *order;
    uint32 *order_scratch;

    order         = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(uint32));
    order_scratch = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(uint32));

    for (i = 0; i < priced; i++) {
      order[i] = i;
    }

    r_stdlib_sort_indices(order, order_scratch, keys, priced);

    for (i = 0; i < priced; i++) {
      list->items[indices[order[i]]] = ruleforge_value_integer((int64)i);
    }
  }

  return ruleforge_value_list(list);
}

/* Summary: the value of a field of a record; a field the record does not have is an error, never a silent none. */
static RuleforgeValue r_collections_field(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  const RuleforgeValue *field;

  (void)count;

  field = ruleforge_value_record_field_get(arguments[0].as.record, arguments[1].as.string);

  if (field == NULL) {
    ruleforge_eval_fail(eval, "RF0312", span, NULL, "the record has no field `%s`", arguments[1].as.string);

    return ruleforge_value_none();
  }

  return *field;
}

/* Summary: the value of a field of a record, or none when the record does not have it. */
static RuleforgeValue r_collections_get(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  const RuleforgeValue *field;

  (void)eval;
  (void)span;
  (void)count;

  field = ruleforge_value_record_field_get(arguments[0].as.record, arguments[1].as.string);

  return field == NULL ? ruleforge_value_none() : *field;
}

static RuleforgeValue r_collections_has(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  (void)eval;
  (void)span;
  (void)count;

  return ruleforge_value_flag(ruleforge_value_record_field_get(arguments[0].as.record, arguments[1].as.string) != NULL);
}

/* Summary: a copy of a record with one field set: replaced where it is, or added at the end. */
static RuleforgeValue r_collections_with_field(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  const RuleforgeRecord *source;
  RuleforgeRecord       *record;
  RuleforgeValue         result;
  uint32                 i;
  boolean                exists;

  (void)span;
  (void)count;

  source = arguments[0].as.record;
  exists = ruleforge_value_record_field_get(source, arguments[1].as.string) != NULL;
  record = ruleforge_value_record_create(eval->compilation, source->count + (exists ? 0U : 1U));

  record->dataset = source->dataset;
  record->row     = source->row;

  for (i = 0; i < source->count; i++) {
    record->names[i]  = source->names[i];
    record->values[i] = source->values[i];

    if (nexus_strings_string_equals(source->names[i], arguments[1].as.string)) {
      record->values[i] = arguments[2];
    }
  }

  if (!exists) {
    record->names[source->count]  = arguments[1].as.string;
    record->values[source->count] = arguments[2];
  }

  result.kind          = RVK_RECORD;
  result.is_identifier = FALSE;
  result.as.record     = record;

  return result;
}

/* Summary: a record made of parallel lists of field names and values. */
static RuleforgeValue r_collections_make_record(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  const RuleforgeList *names;
  const RuleforgeList *values;
  RuleforgeRecord     *record;
  RuleforgeValue       result;
  uint32               i;

  (void)count;

  names  = arguments[0].as.list;
  values = arguments[1].as.list;

  if (names->count != values->count) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.MakeRecord needs as many values (%lu) as names (%lu)", (unsigned long)values->count,
                        (unsigned long)names->count);

    return ruleforge_value_none();
  }

  record = ruleforge_value_record_create(eval->compilation, names->count);

  /* A record made here comes from no dataset row. */
  record->dataset = 0xFFFFFFFFU;
  record->row     = 0;

  for (i = 0; i < names->count; i++) {
    if (names->items[i].kind != RVK_STRING) {
      ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.MakeRecord needs field names that are strings");

      return ruleforge_value_none();
    }

    record->names[i]  = names->items[i].as.string;
    record->values[i] = values->items[i];
  }

  result.kind          = RVK_RECORD;
  result.is_identifier = FALSE;
  result.as.record     = record;

  return result;
}

/* ---------------------------------------------------------------------------- */
/* std.stats                                                                    */
/* ---------------------------------------------------------------------------- */

/*
Summary: the places of the `cuts` widest gaps between neighbouring numbers of a list ordered from high to low, in ascending order.
Description: gap g lies between item g and item g + 1; of equal gaps the earlier comes first. Cutting a ranked list at these places splits
  it into `cuts + 1` groups whose members are close together: a bounded one-dimensional natural-breaks method.
*/
static RuleforgeValue r_stats_widest_gaps(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  const RuleforgeList *values;
  RuleforgeList       *result;
  uint32               gap_count;
  uint32              *order;
  uint32              *scratch;
  f_real              *keys;
  boolean             *chosen;
  uint32               cuts;
  uint32               i;
  uint32               kept;

  (void)count;

  values = arguments[0].as.list;

  if (arguments[1].as.integer < 0 || arguments[1].as.integer > 1000000) {
    ruleforge_eval_fail(eval, "RF0515", span, NULL, "stats.WidestGaps needs a count of cuts from 0 to 1000000");

    return ruleforge_value_none();
  }

  gap_count = values->count > 0 ? values->count - 1 : 0;
  cuts      = (uint32)arguments[1].as.integer;

  if (cuts > gap_count) {
    cuts = gap_count;
  }

  order   = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)gap_count + 1U) * NEXUS_SIZEOF(uint32));
  scratch = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)gap_count + 1U) * NEXUS_SIZEOF(uint32));
  keys    = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)gap_count + 1U) * NEXUS_SIZEOF(f_real));
  chosen  = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)gap_count + 1U) * NEXUS_SIZEOF(boolean));

  for (i = 0; i < gap_count; i++) {
    f_real upper;
    f_real lower;

    if (!r_stdlib_number_need(eval, span, &values->items[i], "stats.WidestGaps", &upper) ||
        !r_stdlib_number_need(eval, span, &values->items[i + 1], "stats.WidestGaps", &lower)) {
      return ruleforge_value_none();
    }

    /* A wider gap sorts first: the key is the negated gap. */
    keys[i]  = -(upper - lower);
    order[i] = i;
  }

  r_stdlib_sort_indices(order, scratch, keys, gap_count);

  for (i = 0; i < cuts; i++) {
    chosen[order[i]] = TRUE;
  }

  result = ruleforge_value_list_create(eval->compilation, cuts);
  kept   = 0;

  for (i = 0; i < gap_count; i++) {
    if (chosen[i]) {
      result->items[kept++] = ruleforge_value_integer((int64)i);
    }
  }

  return ruleforge_value_list(result);
}

/* ---------------------------------------------------------------------------- */
/* std.math, std.core                                                           */
/* ---------------------------------------------------------------------------- */

/* Summary: the whole part of a number, toward zero. */
static RuleforgeValue r_math_truncate(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real number;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Truncate", &number)) {
    return ruleforge_value_none();
  }

  if (number > 9.0e18 || number < -9.0e18) {
    ruleforge_eval_fail(eval, "RF0503", span, NULL, "math.Truncate: the number is too large for an int");

    return ruleforge_value_none();
  }

  return ruleforge_value_integer((int64)number);
}

/* Summary: the name of the kind of a value. */
static RuleforgeValue r_core_type_of(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  (void)eval;
  (void)span;
  (void)count;

  return ruleforge_value_string(ruleforge_value_type_name_get(arguments[0].kind));
}

/* Summary: stops the evaluation with an error at the call in the script, whatever depth of library functions it was raised in. */
static RuleforgeValue r_core_fail(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  (void)count;

  ruleforge_eval_fail(eval, "RF0599", eval->has_user_call ? eval->user_call_span : span, NULL, "%s", arguments[0].as.string);

  return ruleforge_value_none();
}

/* Summary: reports a warning at the call in the script and goes on. */
static RuleforgeValue r_core_warn(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  (void)count;

  ruleforge_diagnostic_report(eval->compilation, RDS_WARNING, "RF0598", eval->has_user_call ? eval->user_call_span : span, NULL, "%s",
                              arguments[0].as.string);

  return ruleforge_value_none();
}

/* ---------------------------------------------------------------------------- */
/* std.rules                                                                    */
/* ---------------------------------------------------------------------------- */

static RuleforgeValue r_rules_concat(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeRuleset *result;
  uint32            i;

  (void)count;

  if (arguments[0].kind != RVK_RULESET || arguments[1].kind != RVK_RULESET) {
    ruleforge_eval_fail(eval, "RF0520", span, NULL, "rules.Concat needs two rulesets");

    return ruleforge_value_none();
  }

  result = ruleforge_value_ruleset_create(eval->compilation, "concat");

  for (i = 0; i < arguments[0].as.ruleset->count; i++) {
    ruleforge_value_ruleset_rule_add(eval->compilation, result, arguments[0].as.ruleset->rules[i]);
  }

  for (i = 0; i < arguments[1].as.ruleset->count; i++) {
    ruleforge_value_ruleset_rule_add(eval->compilation, result, arguments[1].as.ruleset->rules[i]);
  }

  return ruleforge_value_ruleset(result);
}

static RuleforgeValue r_rules_count(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  (void)count;

  if (arguments[0].kind != RVK_RULESET) {
    ruleforge_eval_fail(eval, "RF0520", span, NULL, "rules.Count needs a ruleset");

    return ruleforge_value_none();
  }

  return ruleforge_value_integer((int64)arguments[0].as.ruleset->count);
}

static RuleforgeValue r_rules_reverse(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeRuleset *result;
  uint32            i;

  (void)count;

  if (arguments[0].kind != RVK_RULESET) {
    ruleforge_eval_fail(eval, "RF0520", span, NULL, "rules.Reverse needs a ruleset");

    return ruleforge_value_none();
  }

  result = ruleforge_value_ruleset_create(eval->compilation, "reverse");

  for (i = arguments[0].as.ruleset->count; i > 0; i--) {
    ruleforge_value_ruleset_rule_add(eval->compilation, result, arguments[0].as.ruleset->rules[i - 1]);
  }

  return ruleforge_value_ruleset(result);
}

/* Summary: a copy of a rule with a transformation noted in its provenance; never touches the original. */
static RuleforgeRule *r_rules_copy(REval *eval, RuleforgeSpan span, const RuleforgeRule *source, const char *label) {
  RuleforgeRule *rule;

  rule  = (RuleforgeRule *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule));
  *rule = *source;

  {
    const RuleforgeProvenance *saved;

    saved            = eval->provenance;
    eval->provenance = source->provenance;
    rule->provenance = ruleforge_eval_provenance_push(eval, RPK_TRANSFORM, span, label, NULL);
    eval->provenance = saved;
  }

  return rule;
}

/* Summary: the rules of a ruleset with their baseline action replaced ("show" or "hide"). Variants keep their own actions. */
static RuleforgeValue r_rules_with_action(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeRuleset *result;
  uint32            i;

  (void)count;

  if (arguments[0].kind != RVK_RULESET || arguments[1].kind != RVK_STRING ||
      (!nexus_strings_string_equals(arguments[1].as.string, "show") && !nexus_strings_string_equals(arguments[1].as.string, "hide"))) {
    ruleforge_eval_fail(eval, "RF0521", span, NULL, "rules.WithAction(ruleset, \"show\" or \"hide\")");

    return ruleforge_value_none();
  }

  result = ruleforge_value_ruleset_create(eval->compilation, "withAction");

  for (i = 0; i < arguments[0].as.ruleset->count; i++) {
    RuleforgeRule *rule;

    rule = r_rules_copy(eval, span, arguments[0].as.ruleset->rules[i], "rules.WithAction");

    if (rule->native == NULL) {
      rule->has_action = TRUE;
      rule->show       = nexus_strings_string_equals(arguments[1].as.string, "show");
    }

    ruleforge_value_ruleset_rule_add(eval->compilation, result, rule);
  }

  return ruleforge_value_ruleset(result);
}

static RuleforgeValue r_rules_with_style(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeRuleset *result;
  uint32            i;

  (void)count;

  if (arguments[0].kind != RVK_RULESET || arguments[1].kind != RVK_STYLE) {
    ruleforge_eval_fail(eval, "RF0522", span, NULL, "rules.WithStyle(ruleset, style)");

    return ruleforge_value_none();
  }

  result = ruleforge_value_ruleset_create(eval->compilation, "withStyle");

  for (i = 0; i < arguments[0].as.ruleset->count; i++) {
    RuleforgeRule *rule;

    rule = r_rules_copy(eval, span, arguments[0].as.ruleset->rules[i], "rules.WithStyle");

    if (rule->native == NULL) {
      rule->style = arguments[1].as.style;
    }

    ruleforge_value_ruleset_rule_add(eval->compilation, result, rule);
  }

  return ruleforge_value_ruleset(result);
}

/* Summary: the rules with one more scope condition (key == value), e.g. to restrict them to max-roll items. */
static RuleforgeValue r_rules_with_scope(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeRuleset             *result;
  const RuleforgeConditionInfo *info;
  RuleforgeValue                holder;
  uint32                        i;

  (void)count;

  if (arguments[0].kind != RVK_RULESET || arguments[1].kind != RVK_STRING) {
    ruleforge_eval_fail(eval, "RF0523", span, NULL, "rules.WithScope(ruleset, condition, value)");

    return ruleforge_value_none();
  }

  info = ruleforge_condition_find(eval, eval->module, arguments[1].as.string);

  if (info == NULL) {
    ruleforge_eval_fail(eval, "RF0357", span, NULL, "`%s` is not a game condition; import the module that declares it", arguments[1].as.string);

    return ruleforge_value_none();
  }

  holder = arguments[2];
  result = ruleforge_value_ruleset_create(eval->compilation, "withScope");

  for (i = 0; i < arguments[0].as.ruleset->count; i++) {
    RuleforgeRule       *rule;
    RuleforgeCondition  *condition;
    RuleforgeCondition **grown;
    uint32               k;

    rule = r_rules_copy(eval, span, arguments[0].as.ruleset->rules[i], "rules.WithScope");

    if (rule->native == NULL) {
      if (holder.kind != RVK_STRING && holder.kind != RVK_INT && holder.kind != RVK_BOOL) {
        ruleforge_eval_fail(eval, "RF0523", span, NULL, "rules.WithScope: the value must be a string, int or bool");

        return ruleforge_value_none();
      }

      condition = ruleforge_condition_value_make(eval, span, info, ROP_EQ, &holder);

      if (condition == NULL) {
        return ruleforge_value_none();
      }

      grown = (RuleforgeCondition **)nexus_data_arena_allocate(&eval->compilation->arena,
                                                               ((uint_large)rule->scope.count + 2U) * NEXUS_SIZEOF(RuleforgeCondition *));

      for (k = 0; k < rule->scope.count; k++) {
        grown[k] = rule->scope.conditions[k];
      }

      grown[rule->scope.count] = condition;
      rule->scope.conditions   = grown;
      rule->scope.count++;
    }

    ruleforge_value_ruleset_rule_add(eval->compilation, result, rule);
  }

  return ruleforge_value_ruleset(result);
}

/* ---------------------------------------------------------------------------- */
/* REGISTRY                                                                     */
/* ---------------------------------------------------------------------------- */

static const RuleforgeNative r_stdlib_natives[] = {
    {"std.core", "Fail", r_core_fail},
    {"std.core", "Warn", r_core_warn},
    {"std.core", "TypeOf", r_core_type_of},
    {"std.math", "Truncate", r_math_truncate},
    {"std.collections", "Count", r_collections_count},
    {"std.collections", "Take", r_collections_take},
    {"std.collections", "Reverse", r_collections_reverse},
    {"std.collections", "Concat", r_collections_concat},
    {"std.collections", "Range", r_collections_range},
    {"std.collections", "Pluck", r_collections_pluck},
    {"std.collections", "Sort", r_collections_sort},
    {"std.collections", "Values", r_collections_values},
    {"std.collections", "Ranks", r_collections_ranks},
    {"std.collections", "Field", r_collections_field},
    {"std.collections", "Get", r_collections_get},
    {"std.collections", "Has", r_collections_has},
    {"std.collections", "WithField", r_collections_with_field},
    {"std.collections", "MakeRecord", r_collections_make_record},
    {"std.stats", "WidestGaps", r_stats_widest_gaps},
    {"std.rules", "Concat", r_rules_concat},
    {"std.rules", "Count", r_rules_count},
    {"std.rules", "Reverse", r_rules_reverse},
    {"std.rules", "WithAction", r_rules_with_action},
    {"std.rules", "WithStyle", r_rules_with_style},
    {"std.rules", "WithScope", r_rules_with_scope},
};

const RuleforgeNative *ruleforge_stdlib_native_find(const char *module, const char *name) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(module != NULL && name != NULL, "ruleforge_stdlib_native_find: NULL argument");

  for (i = 0; i < (uint32)(NEXUS_SIZEOF(r_stdlib_natives) / NEXUS_SIZEOF(r_stdlib_natives[0])); i++) {
    if (nexus_strings_string_equals(r_stdlib_natives[i].module, module) && nexus_strings_string_equals(r_stdlib_natives[i].name, name)) {
      return &r_stdlib_natives[i];
    }
  }

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* EMBEDDED MODULES                                                             */
/* ---------------------------------------------------------------------------- */

/* The modules of std/, embedded by cmake/RuleforgeEmbed.cmake: names, bytes and sizes, each ended by a NULL / 0 entry. */
extern const char *const          ruleforge_std_names[];
extern const unsigned char *const ruleforge_std_data[];
extern const unsigned long        ruleforge_std_sizes[];

uint32 ruleforge_stdlib_module_count_get(void) {
  uint32 count;

  for (count = 0; ruleforge_std_names[count] != NULL; count++) {
  }

  return count;
}

const char *ruleforge_stdlib_module_name_get(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < ruleforge_stdlib_module_count_get(), "ruleforge_stdlib_module_name_get: index out of range");

  return ruleforge_std_names[index];
}

boolean ruleforge_stdlib_module_source_get(const char *module, const byte **out_text, uint_large *out_size) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(module != NULL && out_text != NULL && out_size != NULL, "ruleforge_stdlib_module_source_get: NULL argument");

  for (i = 0; ruleforge_std_names[i] != NULL; i++) {
    if (nexus_strings_string_equals(ruleforge_std_names[i], module)) {
      *out_text = ruleforge_std_data[i];
      *out_size = (uint_large)ruleforge_std_sizes[i];

      return TRUE;
    }
  }

  return FALSE;
}
