#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The native part of the standard library: pure, bounded functions the language calls like its own (`economy.AssignTiers(...)`).
Everything here is deterministic and works on immutable values; an error is a diagnostic at the call. The modules are std.math,
std.collections, std.stats, std.economy and std.rules.
*/

#define R_STDLIB_ARGUMENT_LIMIT 16

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
  uint32 *to;
  uint32 *swap;

  from = indices;
  to   = scratch;

  for (width = 1; width < count; width *= 2) {
    for (start = 0; start < count; start += 2 * width) {
      left      = start;
      left_end  = start + width < count ? start + width : count;
      right     = left_end;
      right_end = start + (2 * width) < count ? start + (2 * width) : count;
      out       = start;

      while (left < left_end && right < right_end) {
        if (keys[from[right]] < keys[from[left]]) {
          to[out++] = from[right++];
        } else {
          to[out++] = from[left++];
        }
      }

      while (left < left_end) {
        to[out++] = from[left++];
      }

      while (right < right_end) {
        to[out++] = from[right++];
      }
    }

    swap = from;
    from = to;
    to   = swap;
  }

  if (from != indices) {
    nexus_memory_bytes_copy(from, indices, (uint_large)count * NEXUS_SIZEOF(uint32));
  }
}

/* ---------------------------------------------------------------------------- */
/* std.math                                                                     */
/* ---------------------------------------------------------------------------- */

static RuleforgeValue r_math_abs(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real number;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Abs", &number)) {
    return ruleforge_value_none();
  }

  if (arguments[0].kind == RVK_INT) {
    return ruleforge_value_integer(arguments[0].as.integer < 0 ? -arguments[0].as.integer : arguments[0].as.integer);
  }

  return ruleforge_value_real(number < 0.0 ? -number : number);
}

static RuleforgeValue r_math_min(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real a;
  f_real b;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Min", &a) || !r_stdlib_number_need(eval, span, &arguments[1], "math.Min", &b)) {
    return ruleforge_value_none();
  }

  return a <= b ? arguments[0] : arguments[1];
}

static RuleforgeValue r_math_max(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real a;
  f_real b;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Max", &a) || !r_stdlib_number_need(eval, span, &arguments[1], "math.Max", &b)) {
    return ruleforge_value_none();
  }

  return a >= b ? arguments[0] : arguments[1];
}

static RuleforgeValue r_math_clamp(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real value;
  f_real low;
  f_real high;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Clamp", &value) ||
      !r_stdlib_number_need(eval, span, &arguments[1], "math.Clamp", &low) || !r_stdlib_number_need(eval, span, &arguments[2], "math.Clamp", &high)) {
    return ruleforge_value_none();
  }

  if (low > high) {
    ruleforge_eval_fail(eval, "RF0502", span, NULL, "math.Clamp: the lower bound is above the upper bound");

    return ruleforge_value_none();
  }

  if (value < low) {
    return arguments[1];
  }

  return value > high ? arguments[2] : arguments[0];
}

static RuleforgeValue r_math_floor(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real number;
  int64  whole;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Floor", &number)) {
    return ruleforge_value_none();
  }

  whole = (int64)number;

  if ((f_real)whole > number) {
    whole--;
  }

  return ruleforge_value_integer(whole);
}

static RuleforgeValue r_math_ceil(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real number;
  int64  whole;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Ceil", &number)) {
    return ruleforge_value_none();
  }

  whole = (int64)number;

  if ((f_real)whole < number) {
    whole++;
  }

  return ruleforge_value_integer(whole);
}

static RuleforgeValue r_math_round(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real number;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Round", &number)) {
    return ruleforge_value_none();
  }

  return ruleforge_value_integer(number < 0.0 ? (int64)(number - 0.5) : (int64)(number + 0.5));
}

static RuleforgeValue r_math_sqrt(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real number;
  f_real guess;
  uint32 i;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Sqrt", &number)) {
    return ruleforge_value_none();
  }

  if (number < 0.0) {
    ruleforge_eval_fail(eval, "RF0503", span, NULL, "math.Sqrt of a negative number");

    return ruleforge_value_none();
  }

  if (number == 0.0) {
    return ruleforge_value_real(0.0);
  }

  /* Newton's method with a fixed iteration count: bounded and identical on every platform. */
  guess = number > 1.0 ? number : 1.0;

  for (i = 0; i < 64; i++) {
    guess = 0.5 * (guess + (number / guess));
  }

  return ruleforge_value_real(guess);
}

static RuleforgeValue r_math_pow(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real base;
  f_real result;
  int64  exponent;
  int64  step;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "math.Pow", &base)) {
    return ruleforge_value_none();
  }

  if (arguments[1].kind != RVK_INT || arguments[1].as.integer < -1024 || arguments[1].as.integer > 1024) {
    ruleforge_eval_fail(eval, "RF0504", span, "the exponent is an int from -1024 to 1024", "math.Pow needs a small integer exponent");

    return ruleforge_value_none();
  }

  exponent = arguments[1].as.integer < 0 ? -arguments[1].as.integer : arguments[1].as.integer;
  result   = 1.0;

  for (step = 0; step < exponent; step++) {
    result *= base;
  }

  if (arguments[1].as.integer < 0) {
    if (result == 0.0) {
      ruleforge_eval_fail(eval, "RF0342", span, NULL, "division by zero");

      return ruleforge_value_none();
    }

    result = 1.0 / result;
  }

  return ruleforge_value_real(result);
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

  list = ruleforge_value_list_create(eval->compilation, left_count + right_count);

  for (i = 0; i < left_count; i++) {
    list->items[i] = left[i];
  }

  for (i = 0; i < right_count; i++) {
    list->items[left_count + i] = right[i];
  }

  return ruleforge_value_list(list);
}

static RuleforgeValue r_collections_contains(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  uint32          i;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "collections.Contains", &items_count, &items)) {
    return ruleforge_value_none();
  }

  for (i = 0; i < items_count; i++) {
    if (ruleforge_value_equals(&items[i], &arguments[1])) {
      return ruleforge_value_flag(TRUE);
    }
  }

  return ruleforge_value_flag(FALSE);
}

static RuleforgeValue r_collections_range(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeList *list;
  int64          from;
  int64          to;
  int64          i;

  (void)count;

  if (arguments[0].kind != RVK_INT || arguments[1].kind != RVK_INT) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.Range(from, to) needs ints");

    return ruleforge_value_none();
  }

  from = arguments[0].as.integer;
  to   = arguments[1].as.integer;

  if (to < from || to - from > 100000) {
    ruleforge_eval_fail(eval, "RF0506", span, "ranges are bounded to 100000 elements", "collections.Range(%lld, %lld) is empty or too large",
                        (long long)from, (long long)to);

    return ruleforge_value_none();
  }

  list = ruleforge_value_list_create(eval->compilation, (uint32)(to - from));

  for (i = from; i < to; i++) {
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

/* Summary: the records sorted by a numeric field, descending when asked; records without a value sort last. */
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

  if (arguments[1].kind != RVK_STRING || (arguments[2].kind != RVK_NONE && arguments[2].kind != RVK_BOOL)) {
    ruleforge_eval_fail(eval, "RF0505", span, NULL, "collections.Sort(list, field, descending) needs a field name");

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
    field      = items[i].kind == RVK_RECORD ? ruleforge_value_record_field_get(items[i].as.record, arguments[1].as.string) : NULL;

    if (field == NULL) {
      ruleforge_eval_fail(eval, "RF0312", span, NULL, "collections.Sort: item %lu has no field `%s`", (unsigned long)i, arguments[1].as.string);

      return ruleforge_value_none();
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

/* ---------------------------------------------------------------------------- */
/* std.stats                                                                    */
/* ---------------------------------------------------------------------------- */

/* Summary: the numbers of a list (or a field of its records) sorted ascending in the arena. */
static boolean r_stats_numbers(REval *eval, RuleforgeSpan span, const RuleforgeValue *source, const RuleforgeValue *field, const char *function,
                               uint32 *out_count, f_real **out_numbers) {
  uint32          items_count;
  RuleforgeValue *items;
  f_real         *numbers;
  uint32         *indices;
  uint32         *scratch;
  f_real         *sorted;
  uint32          kept;
  uint32          i;

  if (!r_stdlib_items_get(eval, span, source, function, &items_count, &items)) {
    return FALSE;
  }

  numbers = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(f_real));
  kept    = 0;

  for (i = 0; i < items_count; i++) {
    const RuleforgeValue *value;
    f_real                number;

    value = &items[i];

    if (items[i].kind == RVK_RECORD) {
      if (field == NULL || field->kind != RVK_STRING) {
        ruleforge_eval_fail(eval, "RF0507", span, NULL, "%s needs the field name when the items are records", function);

        return FALSE;
      }

      value = ruleforge_value_record_field_get(items[i].as.record, field->as.string);

      if (value == NULL) {
        ruleforge_eval_fail(eval, "RF0312", span, NULL, "%s: item %lu has no field `%s`", function, (unsigned long)i, field->as.string);

        return FALSE;
      }
    }

    /* A missing value is left out rather than counted as zero. */
    if (ruleforge_value_number_get(value, &number)) {
      numbers[kept++] = number;
    }
  }

  indices = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)kept + 1U) * NEXUS_SIZEOF(uint32));
  scratch = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)kept + 1U) * NEXUS_SIZEOF(uint32));
  sorted  = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)kept + 1U) * NEXUS_SIZEOF(f_real));

  for (i = 0; i < kept; i++) {
    indices[i] = i;
  }

  r_stdlib_sort_indices(indices, scratch, numbers, kept);

  for (i = 0; i < kept; i++) {
    sorted[i] = numbers[indices[i]];
  }

  *out_count   = kept;
  *out_numbers = sorted;

  return TRUE;
}

static RuleforgeValue r_stats_mean(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32  kept;
  f_real *numbers;
  f_real  sum;
  uint32  i;

  (void)count;

  if (!r_stats_numbers(eval, span, &arguments[0], &arguments[1], "stats.Mean", &kept, &numbers)) {
    return ruleforge_value_none();
  }

  if (kept == 0) {
    return ruleforge_value_none();
  }

  sum = 0.0;

  for (i = 0; i < kept; i++) {
    sum += numbers[i];
  }

  return ruleforge_value_real(sum / (f_real)kept);
}

/* Summary: the q-quantile (0..1) by linear interpolation between the closest ranks. */
static RuleforgeValue r_stats_quantile(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32  kept;
  f_real *numbers;
  f_real  quantile;
  f_real  position;
  uint32  lower;
  f_real  fraction;

  (void)count;

  if (!r_stats_numbers(eval, span, &arguments[0], &arguments[2], "stats.Quantile", &kept, &numbers) ||
      !r_stdlib_number_need(eval, span, &arguments[1], "stats.Quantile", &quantile)) {
    return ruleforge_value_none();
  }

  if (quantile < 0.0 || quantile > 1.0) {
    ruleforge_eval_fail(eval, "RF0508", span, NULL, "stats.Quantile needs q between 0 and 1");

    return ruleforge_value_none();
  }

  if (kept == 0) {
    return ruleforge_value_none();
  }

  position = quantile * (f_real)(kept - 1);
  lower    = (uint32)position;
  fraction = position - (f_real)lower;

  if (lower + 1 >= kept) {
    return ruleforge_value_real(numbers[kept - 1]);
  }

  return ruleforge_value_real(numbers[lower] + (fraction * (numbers[lower + 1] - numbers[lower])));
}

static RuleforgeValue r_stats_median(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeValue forwarded[3];

  (void)count;

  forwarded[0] = arguments[0];
  forwarded[1] = ruleforge_value_real(0.5);
  forwarded[2] = arguments[1];

  return r_stats_quantile(eval, span, forwarded, 3);
}

/* ---------------------------------------------------------------------------- */
/* std.economy                                                                  */
/* ---------------------------------------------------------------------------- */

/* Summary: price * quantity, the value of a stack. */
static RuleforgeValue r_economy_stack_value(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real price;
  f_real quantity;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[0], "economy.StackValue", &price) ||
      !r_stdlib_number_need(eval, span, &arguments[1], "economy.StackValue", &quantity)) {
    return ruleforge_value_none();
  }

  return ruleforge_value_real(price * quantity);
}

/* Summary: market * (1 - weight) + crafting * weight, with a missing component taking the other's whole weight. */
static RuleforgeValue r_economy_score(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  f_real market;
  f_real crafting;
  f_real weight;

  (void)count;

  if (!r_stdlib_number_need(eval, span, &arguments[2], "economy.Score", &weight)) {
    return ruleforge_value_none();
  }

  if (weight < 0.0 || weight > 1.0) {
    ruleforge_eval_fail(eval, "RF0509", span, NULL, "economy.Score needs a weight between 0 and 1");

    return ruleforge_value_none();
  }

  if (arguments[0].kind == RVK_NONE && arguments[1].kind == RVK_NONE) {
    return ruleforge_value_none();
  }

  if (arguments[0].kind == RVK_NONE) {
    return arguments[1];
  }

  if (arguments[1].kind == RVK_NONE) {
    return arguments[0];
  }

  if (!r_stdlib_number_need(eval, span, &arguments[0], "economy.Score", &market) ||
      !r_stdlib_number_need(eval, span, &arguments[1], "economy.Score", &crafting)) {
    return ruleforge_value_none();
  }

  return ruleforge_value_real((market * (1.0 - weight)) + (crafting * weight));
}

/*
Summary: assigns every record a tier from a numeric field and returns the records with `tier` (1 is the most valuable) and `score`.
Description: strategies. "quantile" splits the ranked records into `count` groups of equal size. "threshold" uses the descending
  `thresholds` list: tier k when the value is at least the k-th threshold, the last tier below all of them. "cluster" splits at
  the `count - 1` widest gaps between the sorted values (a bounded 1-D natural-breaks method). A record without a value keeps
  `tier` and `score` none: an unpriced item is not assumed to be worthless.
*/
static RuleforgeValue r_economy_assign_tiers(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          items_count;
  RuleforgeValue *items;
  RuleforgeList  *result;
  const char     *field;
  const char     *strategy;
  int64           tiers;
  uint32          i;
  uint32         *indices;
  f_real         *keys;
  uint32          priced;
  int64          *assigned;
  f_real         *scores;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "economy.AssignTiers", &items_count, &items)) {
    return ruleforge_value_none();
  }

  if (arguments[1].kind != RVK_STRING || arguments[2].kind != RVK_INT || arguments[2].as.integer < 1 || arguments[2].as.integer > 64 ||
      (arguments[3].kind != RVK_NONE && arguments[3].kind != RVK_STRING)) {
    ruleforge_eval_fail(eval, "RF0510", span, "economy.AssignTiers(data, field, count, strategy, thresholds)",
                        "economy.AssignTiers needs a field name and a tier count from 1 to 64");

    return ruleforge_value_none();
  }

  field    = arguments[1].as.string;
  tiers    = arguments[2].as.integer;
  strategy = arguments[3].kind == RVK_STRING ? arguments[3].as.string : "quantile";

  indices  = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(uint32));
  keys     = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(f_real));
  scores   = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(f_real));
  assigned = (int64 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)items_count + 1U) * NEXUS_SIZEOF(int64));
  priced   = 0;

  /* Rank the priced records from the most valuable down; unpriced ones are left out of the ranking. */
  for (i = 0; i < items_count; i++) {
    const RuleforgeValue *value;
    f_real                number;

    assigned[i] = 0;

    if (items[i].kind != RVK_RECORD) {
      ruleforge_eval_fail(eval, "RF0511", span, NULL, "economy.AssignTiers needs records, item %lu is a %s", (unsigned long)i,
                          ruleforge_value_type_name_get(items[i].kind));

      return ruleforge_value_none();
    }

    value = ruleforge_value_record_field_get(items[i].as.record, field);

    if (value == NULL) {
      ruleforge_eval_fail(eval, "RF0312", span, NULL, "economy.AssignTiers: item %lu has no field `%s`", (unsigned long)i, field);

      return ruleforge_value_none();
    }

    if (ruleforge_value_number_get(value, &number)) {
      scores[i]       = number;
      keys[priced]    = -number;
      indices[priced] = i;
      priced++;
    }
  }

  {
    uint32 *rank_indices;
    uint32 *rank_scratch;
    f_real *rank_keys;
    uint32  r;

    rank_indices = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(uint32));
    rank_scratch = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(uint32));
    rank_keys    = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(f_real));

    for (r = 0; r < priced; r++) {
      rank_indices[r] = r;
      rank_keys[r]    = keys[r];
    }

    r_stdlib_sort_indices(rank_indices, rank_scratch, rank_keys, priced);

    if (nexus_strings_string_equals(strategy, "quantile")) {
      for (r = 0; r < priced; r++) {
        assigned[indices[rank_indices[r]]] = 1 + (int64)(((uint64)r * (uint64)tiers) / (uint64)priced);
      }
    } else if (nexus_strings_string_equals(strategy, "threshold")) {
      uint32          threshold_count;
      RuleforgeValue *thresholds;

      if (arguments[4].kind != RVK_LIST || arguments[4].as.list->count + 1 != (uint32)tiers) {
        ruleforge_eval_fail(eval, "RF0512", span, "pass `count - 1` thresholds, highest first",
                            "the \"threshold\" strategy needs a thresholds list with count - 1 entries");

        return ruleforge_value_none();
      }

      threshold_count = arguments[4].as.list->count;
      thresholds      = arguments[4].as.list->items;

      for (r = 0; r < priced; r++) {
        uint32 index;
        uint32 t;
        f_real bound;

        index           = indices[rank_indices[r]];
        assigned[index] = tiers;

        for (t = 0; t < threshold_count; t++) {
          if (!ruleforge_value_number_get(&thresholds[t], &bound)) {
            ruleforge_eval_fail(eval, "RF0512", span, NULL, "the thresholds must be numbers");

            return ruleforge_value_none();
          }

          if (scores[index] >= bound) {
            assigned[index] = (int64)t + 1;

            break;
          }
        }
      }
    } else if (nexus_strings_string_equals(strategy, "cluster")) {
      uint32 *cut_indices;
      uint32 *cut_scratch;
      f_real *gaps;
      uint32  cuts;
      uint32  g;

      /* Cut after the (tiers - 1) widest gaps between neighbouring ranked values. */
      cuts        = priced > (uint32)tiers ? (uint32)tiers - 1 : (priced > 0 ? priced - 1 : 0);
      gaps        = (f_real *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(f_real));
      cut_indices = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(uint32));
      cut_scratch = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(uint32));

      for (g = 0; g + 1 < priced; g++) {
        /* A larger gap sorts first: the key is the negated gap. */
        gaps[g]        = -(scores[indices[rank_indices[g]]] - scores[indices[rank_indices[g + 1]]]);
        cut_indices[g] = g;
      }

      r_stdlib_sort_indices(cut_indices, cut_scratch, gaps, priced > 0 ? priced - 1 : 0);

      {
        boolean *cut_after;
        int64    tier;

        cut_after = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)priced + 1U) * NEXUS_SIZEOF(boolean));

        for (g = 0; g < cuts; g++) {
          cut_after[cut_indices[g]] = TRUE;
        }

        tier = 1;

        for (r = 0; r < priced; r++) {
          assigned[indices[rank_indices[r]]] = tier;

          if (cut_after[r]) {
            tier++;
          }
        }
      }
    } else {
      ruleforge_eval_fail(eval, "RF0513", span, "strategies: quantile, threshold, cluster", "unknown tiering strategy \"%s\"", strategy);

      return ruleforge_value_none();
    }
  }

  result = ruleforge_value_list_create(eval->compilation, items_count);

  for (i = 0; i < items_count; i++) {
    RuleforgeRecord *source;
    RuleforgeRecord *record;
    uint32           k;

    source          = items[i].as.record;
    record          = ruleforge_value_record_create(eval->compilation, source->count + 2);
    record->dataset = source->dataset;
    record->row     = source->row;

    for (k = 0; k < source->count; k++) {
      record->names[k]  = source->names[k];
      record->values[k] = source->values[k];
    }

    record->names[source->count]      = "tier";
    record->names[source->count + 1]  = "score";
    record->values[source->count]     = assigned[i] == 0 ? ruleforge_value_none() : ruleforge_value_integer(assigned[i]);
    record->values[source->count + 1] = assigned[i] == 0 ? ruleforge_value_none() : ruleforge_value_real(scores[i]);
    result->items[i].kind             = RVK_RECORD;
    result->items[i].is_identifier    = FALSE;
    result->items[i].as.record        = record;
  }

  return ruleforge_value_list(result);
}

/*
Summary: keeps an established tier until the new tier has held for `required` consecutive snapshots.
Description: `history` is a list of records {key, tier, streak} from the previous run (a dataset the host keeps); `current` is the
  output of AssignTiers. A record whose tier is unchanged keeps its streak; a changed one is held at its old tier until its streak
  reaches `required`. Recompiling the same snapshot does not add evidence: the host only advances the history on a new snapshot.
*/
static RuleforgeValue r_economy_persist_tiers(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  uint32          current_count;
  RuleforgeValue *current;
  uint32          history_count;
  RuleforgeValue *history;
  RuleforgeList  *result;
  const char     *key_field;
  int64           required;
  uint32          i;
  uint32          j;

  (void)count;

  if (!r_stdlib_items_get(eval, span, &arguments[0], "economy.PersistTiers", &current_count, &current) ||
      !r_stdlib_items_get(eval, span, &arguments[1], "economy.PersistTiers", &history_count, &history)) {
    return ruleforge_value_none();
  }

  if (arguments[2].kind != RVK_STRING || arguments[3].kind != RVK_INT || arguments[3].as.integer < 1) {
    ruleforge_eval_fail(eval, "RF0514", span, "economy.PersistTiers(current, history, key, required)",
                        "economy.PersistTiers needs a key field and a positive snapshot count");

    return ruleforge_value_none();
  }

  key_field = arguments[2].as.string;
  required  = arguments[3].as.integer;
  result    = ruleforge_value_list_create(eval->compilation, current_count);

  for (i = 0; i < current_count; i++) {
    RuleforgeRecord      *source;
    RuleforgeRecord      *record;
    const RuleforgeValue *key;
    const RuleforgeValue *tier;
    int64                 held;
    int64                 streak;
    uint32                k;

    source = current[i].as.record;
    key    = ruleforge_value_record_field_get(source, key_field);
    tier   = ruleforge_value_record_field_get(source, "tier");

    if (key == NULL || tier == NULL) {
      ruleforge_eval_fail(eval, "RF0514", span, NULL, "economy.PersistTiers: item %lu needs the key field and a tier (run AssignTiers first)",
                          (unsigned long)i);

      return ruleforge_value_none();
    }

    held   = tier->kind == RVK_INT ? tier->as.integer : 0;
    streak = 1;

    for (j = 0; j < history_count; j++) {
      const RuleforgeValue *history_key;
      const RuleforgeValue *history_tier;
      const RuleforgeValue *history_streak;

      history_key = history[j].kind == RVK_RECORD ? ruleforge_value_record_field_get(history[j].as.record, key_field) : NULL;

      if (history_key == NULL || !ruleforge_value_equals(history_key, key)) {
        continue;
      }

      history_tier   = ruleforge_value_record_field_get(history[j].as.record, "tier");
      history_streak = ruleforge_value_record_field_get(history[j].as.record, "streak");

      if (history_tier != NULL && history_tier->kind == RVK_INT && tier->kind == RVK_INT) {
        if (history_tier->as.integer == tier->as.integer) {
          streak = history_streak != NULL && history_streak->kind == RVK_INT ? history_streak->as.integer + 1 : 1;
        } else {
          /* The candidate tier counts consecutive snapshots in `candidate_streak`; the established tier holds until it is enough. */
          const RuleforgeValue *candidate;
          const RuleforgeValue *candidate_streak;

          candidate        = ruleforge_value_record_field_get(history[j].as.record, "candidate");
          candidate_streak = ruleforge_value_record_field_get(history[j].as.record, "candidateStreak");
          streak = candidate != NULL && candidate->kind == RVK_INT && candidate->as.integer == tier->as.integer && candidate_streak != NULL &&
                           candidate_streak->kind == RVK_INT
                       ? candidate_streak->as.integer + 1
                       : 1;
          held   = streak >= required ? tier->as.integer : history_tier->as.integer;
        }
      }

      break;
    }

    record          = ruleforge_value_record_create(eval->compilation, source->count + 1);
    record->dataset = source->dataset;
    record->row     = source->row;

    for (k = 0; k < source->count; k++) {
      record->names[k]  = source->names[k];
      record->values[k] = source->values[k];

      if (nexus_strings_string_equals(source->names[k], "tier") && tier->kind == RVK_INT) {
        record->values[k] = ruleforge_value_integer(held);
      }
    }

    record->names[source->count]   = "streak";
    record->values[source->count]  = ruleforge_value_integer(streak);
    result->items[i].kind          = RVK_RECORD;
    result->items[i].is_identifier = FALSE;
    result->items[i].as.record     = record;
  }

  return ruleforge_value_list(result);
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

  info = ruleforge_manifest_condition_find(arguments[1].as.string);

  if (info == NULL) {
    ruleforge_eval_fail(eval, "RF0357", span, NULL, "`%s` is not a game condition", arguments[1].as.string);

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
    {"std.math", "Abs", "value", 1, r_math_abs},
    {"std.math", "Min", "a b", 2, r_math_min},
    {"std.math", "Max", "a b", 2, r_math_max},
    {"std.math", "Clamp", "value low high", 3, r_math_clamp},
    {"std.math", "Floor", "value", 1, r_math_floor},
    {"std.math", "Ceil", "value", 1, r_math_ceil},
    {"std.math", "Round", "value", 1, r_math_round},
    {"std.math", "Sqrt", "value", 1, r_math_sqrt},
    {"std.math", "Pow", "base exponent", 2, r_math_pow},
    {"std.collections", "Count", "items", 1, r_collections_count},
    {"std.collections", "Take", "items count skip", 2, r_collections_take},
    {"std.collections", "Reverse", "items", 1, r_collections_reverse},
    {"std.collections", "Concat", "left right", 2, r_collections_concat},
    {"std.collections", "Contains", "items value", 2, r_collections_contains},
    {"std.collections", "Range", "from to", 2, r_collections_range},
    {"std.collections", "Pluck", "items field", 2, r_collections_pluck},
    {"std.collections", "Sort", "items field descending", 2, r_collections_sort},
    {"std.stats", "Mean", "items field", 1, r_stats_mean},
    {"std.stats", "Median", "items field", 1, r_stats_median},
    {"std.stats", "Quantile", "items q field", 2, r_stats_quantile},
    {"std.economy", "StackValue", "price quantity", 2, r_economy_stack_value},
    {"std.economy", "Score", "market crafting weight", 3, r_economy_score},
    {"std.economy", "AssignTiers", "data field count strategy thresholds", 3, r_economy_assign_tiers},
    {"std.economy", "PersistTiers", "current history key required", 4, r_economy_persist_tiers},
    {"std.rules", "Concat", "left right", 2, r_rules_concat},
    {"std.rules", "Count", "rules", 1, r_rules_count},
    {"std.rules", "Reverse", "rules", 1, r_rules_reverse},
    {"std.rules", "WithAction", "rules action", 2, r_rules_with_action},
    {"std.rules", "WithStyle", "rules style", 2, r_rules_with_style},
    {"std.rules", "WithScope", "rules condition value", 3, r_rules_with_scope},
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

boolean ruleforge_stdlib_module_exists(const char *module) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(module != NULL, "ruleforge_stdlib_module_exists: NULL module");

  for (i = 0; i < (uint32)(NEXUS_SIZEOF(r_stdlib_natives) / NEXUS_SIZEOF(r_stdlib_natives[0])); i++) {
    if (nexus_strings_string_equals(r_stdlib_natives[i].module, module)) {
      return TRUE;
    }
  }

  return FALSE;
}

void ruleforge_stdlib_json_write(NexusStringBuilder *out) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(out != NULL, "ruleforge_stdlib_json_write: NULL out");

  (void)nexus_strings_string_builder_append(out, "\"functions\":[");

  for (i = 0; i < (uint32)(NEXUS_SIZEOF(r_stdlib_natives) / NEXUS_SIZEOF(r_stdlib_natives[0])); i++) {
    (void)nexus_strings_string_builder_append_formatted(out, "%s{\"module\":\"%s\",\"name\":\"%s\",\"minimum\":%lu,\"parameters\":[",
                                                        i > 0 ? "," : "", r_stdlib_natives[i].module, r_stdlib_natives[i].name,
                                                        (unsigned long)r_stdlib_natives[i].minimum);

    {
      const char *cursor;
      boolean     first;

      cursor = r_stdlib_natives[i].parameters;
      first  = TRUE;

      while (*cursor != '\0') {
        const char *end;

        end = cursor;

        while (*end != '\0' && *end != ' ') {
          end++;
        }

        (void)nexus_strings_string_builder_append_formatted(out, "%s\"%.*s\"", first ? "" : ",", (int)(end - cursor), cursor);
        first  = FALSE;
        cursor = *end == ' ' ? end + 1 : end;
      }
    }

    (void)nexus_strings_string_builder_append(out, "]}");
  }

  (void)nexus_strings_string_builder_append(out, "]");
}
