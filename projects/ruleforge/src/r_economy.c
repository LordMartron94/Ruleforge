#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <nexus.h>
#include "./r_internal.h"

/* History policy is pure compiler work. The host supplies immutable, patch- and
   freshness-filtered buckets and persists returned decisions after publication. */
typedef struct REconomyObservation {
  const char *base;
  const char *name;
  const char *league;
  f_real      price;
  f_real      listings;
  f_real      weight;
  int64       bucket;
  boolean     supply;
  boolean     liquid;
  boolean     reliable;
} REconomyObservation;
typedef struct REconomyPoint {
  f_real  value;
  f_real  weight;
  int64   bucket;
  boolean reliable;
} REconomyPoint;
typedef struct REconomyAggregate {
  const char *base;
  const char *league;
  f_real      price;
  f_real      listings;
  boolean     supply;
} REconomyAggregate;
typedef struct REconomyScore {
  const char *base;
  f_real      score[5];
  f_real      crafting;
  int64       bucket;
  int64       tier[5];
  boolean     reliable;
} REconomyScore;

typedef struct REconomyCache {
  struct REconomyCache *next;
  const void           *arguments[4];
  RuleforgeValue        result;
} REconomyCache;

static const RuleforgeValue *r_e_field(const RuleforgeValue *row, const char *name) {
  return row != NULL && row->kind == RVK_RECORD ? ruleforge_value_record_field_get(row->as.record, name) : NULL;
}
static const char *r_e_text(const RuleforgeValue *row, const char *name) {
  const RuleforgeValue *v;
  v = r_e_field(row, name);
  return v != NULL && v->kind == RVK_STRING ? v->as.string : "";
}
static f_real r_e_number(const RuleforgeValue *row, const char *name, f_real fallback) {
  const RuleforgeValue *v;
  f_real                n;
  v = r_e_field(row, name);
  return v != NULL && ruleforge_value_number_get(v, &n) ? n : fallback;
}
static RuleforgeValue *r_e_items(REval *eval, const RuleforgeValue *value, uint32 *count) {
  RuleforgeValue *out;
  uint32          i;
  *count = 0;
  if (value == NULL) {
    return NULL;
  }
  if (value->kind == RVK_LIST) {
    *count = value->as.list->count;
    return value->as.list->items;
  }
  if (value->kind != RVK_DATASET) {
    return NULL;
  }
  *count = value->as.dataset->count;
  out = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)*count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
  for (i = 0; i < *count; i++) {
    out[i].kind = RVK_RECORD;
    out[i].as.record = value->as.dataset->rows[i];
  }
  return out;
}
static f_real r_e_sample(const RuleforgeValue *row, const char *name, uint32 index, f_real fallback) {
  const RuleforgeValue *value;
  f_real                number;
  value = r_e_field(row, name);
  if (value == NULL) {
    return fallback;
  }
  if (value->kind == RVK_LIST) {
    if (index >= value->as.list->count) {
      return fallback;
    }
    value = &value->as.list->items[index];
  }
  return ruleforge_value_number_get(value, &number) ? number : fallback;
}
static int r_e_base_compare(const void *a, const void *b) { return strcmp(((const REconomyScore *)a)->base, ((const REconomyScore *)b)->base); }
static RuleforgeValue r_e_record(REval *eval, const char **names, RuleforgeValue *values, uint32 count) {
  RuleforgeValue   value;
  RuleforgeRecord *record;
  uint32           i;
  record = ruleforge_value_record_create(eval->compilation, count);
  for (i = 0; i < count; i++) {
    record->names[i] = names[i];
    record->values[i] = values[i];
  }
  value = ruleforge_value_none();
  value.kind = RVK_RECORD;
  value.as.record = record;
  return value;
}
static int r_e_observation_compare(const void *a, const void *b) {
  const REconomyObservation *x;
  const REconomyObservation *y;
  int                        result;
  x = (const REconomyObservation *)a;
  y = (const REconomyObservation *)b;
  result = strcmp(x->league, y->league);
  if (result != 0) {
    return result;
  }
  result = strcmp(x->base, y->base);
  if (result != 0) {
    return result;
  }
  result = strcmp(x->name, y->name);
  if (result != 0) {
    return result;
  }
  return x->bucket < y->bucket ? -1 : x->bucket > y->bucket;
}
static int r_e_point_compare(const void *a, const void *b) {
  const REconomyPoint *x;
  const REconomyPoint *y;
  x = (const REconomyPoint *)a;
  y = (const REconomyPoint *)b;
  return x->value < y->value ? -1 : x->value > y->value;
}
static int r_e_score_compare(const void *a, const void *b) {
  const REconomyScore *x;
  const REconomyScore *y;
  x = (const REconomyScore *)a;
  y = (const REconomyScore *)b;
  if (x->score[0] != y->score[0]) {
    return x->score[0] > y->score[0] ? -1 : 1;
  }
  return strcmp(x->base, y->base);
}
/* Summary: confidence-gated weighted percentile, or weighted median for a thin sample. */
static f_real r_e_robust(REconomyObservation *rows, uint32 count, REconomyPoint *scratch, uint32 minimum) {
  uint32 i;
  uint32 j;
  uint32 buckets;
  uint32 n;
  f_real total;
  f_real cumulative;
  f_real q;
  buckets = 0;
  for (i = 0; i < count; i++) {
    boolean seen;
    if (!rows[i].liquid) {
      continue;
    }
    seen = FALSE;
    for (j = 0; j < i; j++) {
      seen = seen || (rows[j].liquid && rows[j].bucket == rows[i].bucket);
    }
    if (!seen) {
      buckets++;
    }
  }
  q = buckets >= minimum ? 0.95 : 0.5;
  n = 0;
  total = 0;
  for (i = 0; i < count; i++) {
    if (q > 0.5 && !rows[i].liquid) {
      continue;
    }
    scratch[n].value = rows[i].price;
    scratch[n].weight = rows[i].weight;
    total += rows[i].weight;
    n++;
  }
  if (n == 0) {
    return 0;
  }
  qsort(scratch, n, sizeof(*scratch), r_e_point_compare);
  cumulative = 0;
  for (i = 0; i < n; i++) {
    cumulative += scratch[i].weight;
    if (cumulative >= q * total) {
      return scratch[i].value;
    }
  }
  return scratch[n - 1].value;
}
static f_real r_e_supply(REconomyObservation *rows, uint32 count) {
  f_real sum;
  f_real weights;
  uint32 i;
  sum = 0;
  weights = 0;
  for (i = 0; i < count; i++) {
    if (rows[i].supply) {
      sum += rows[i].listings * rows[i].weight;
      weights += rows[i].weight;
    }
  }
  return weights > 0 ? sum / weights : 0;
}
static uint32 r_e_nearest(const f_real *centres, uint32 count, f_real value) {
  uint32 best;
  uint32 i;
  f_real distance;
  best = 0;
  distance = fabs(value - centres[0]);
  for (i = 1; i < count; i++) {
    f_real next;
    next = fabs(value - centres[i]);
    if (next < distance) {
      distance = next;
      best = i;
    }
  }
  return best;
}
static boolean r_e_profile_has(RuleforgeValue *profiles, uint32 count, const char *base) {
  uint32 i;
  for (i = 0; i < count; i++) {
    if (strcmp(r_e_text(&profiles[i], "baseType"), base) == 0) {
      return TRUE;
    }
  }
  return FALSE;
}

RuleforgeValue ruleforge_economy_tier_history(REval *eval, RuleforgeSpan span, const RuleforgeValue *args, uint32 arg_count) {
  RuleforgeValue       *history;
  RuleforgeValue       *profiles;
  RuleforgeValue       *weights;
  RuleforgeValue       *previous;
  RuleforgeValue       *names;
  REconomyObservation  *observations;
  REconomyAggregate    *aggregates;
  REconomyPoint        *scratch;
  REconomyScore        *scores;
  RuleforgeList        *state;
  RuleforgeList        *groups;
  const RuleforgeValue *request;
  const char           *class_name;
  const char           *group_name;
  uint32                history_count;
  uint_large            observation_capacity;
  uint32                profile_count;
  uint32                weight_count;
  uint32                previous_count;
  uint32                names_count;
  uint32                observation_count;
  uint32                aggregate_count;
  uint32                score_count;
  uint32                tiers;
  uint32                k;
  uint32                i;
  uint32                j;
  uint32                t;
  uint32                start;
  uint32                end;
  uint32                iteration;
  uint32                assignment_count[64];
  f_real                centres[64];
  f_real                sums[64];
  uint32                order[64];
  uint32               *assignment;
  int64                 newest;
  f_real                mean;
  f_real                variance;
  f_real                listing_mean;
  f_real                listing_variance;
  f_real                listing_count;
  f_real                craft_mean;
  f_real                craft_sd;
  f_real                craft_weight;
  f_real                chase_weight;
  f_real                half_life;
  f_real                offset;
  boolean               currency;
  boolean               csv;
  int64                 stacks[5];
  REconomyCache        *cached;
  const void           *identities[4];
  (void)arg_count;
  for (i = 0; i < 4; i++) {
    identities[i] = args[i].kind == RVK_RECORD    ? (const void *)args[i].as.record
                    : args[i].kind == RVK_DATASET ? (const void *)args[i].as.dataset
                                                  : (const void *)args[i].as.list;
  }
  for (cached = (REconomyCache *)eval->economy_cache; cached != NULL; cached = cached->next) {
    if (cached->arguments[0] == identities[0] && cached->arguments[1] == identities[1] && cached->arguments[2] == identities[2] &&
        cached->arguments[3] == identities[3]) {
      return cached->result;
    }
  }
  request = &args[1];
  class_name = r_e_text(request, "class");
  group_name = r_e_text(request, "id");
  tiers = (uint32)r_e_number(request, "count", 0);
  if (tiers < 1 || tiers > 64 || request->kind != RVK_RECORD) {
    goto invalid;
  }
  history = r_e_items(eval, &args[0], &history_count);
  weights = r_e_items(eval, &args[2], &weight_count);
  previous = r_e_items(eval, &args[3], &previous_count);
  profiles = r_e_items(eval, r_e_field(request, "profiles"), &profile_count);
  names = r_e_items(eval, r_e_field(request, "validBases"), &names_count);
  currency = strcmp(class_name, "Currency") == 0;
  csv = strncmp(class_name, "csv:", 4) == 0;
  craft_weight = r_e_number(request, "craftingWeight", currency ? 0.4 : 0);
  chase_weight = r_e_number(request, "chaseWeight", 0.85);
  half_life = r_e_number(request, "halfLifeDays", 14) * 86400;
  offset = currency || csv ? 0.01 : 1;
  if (craft_weight < 0 || craft_weight > 1 || chase_weight < 0 || chase_weight > 1 || half_life <= 0 || weight_count == 0) {
    goto invalid;
  }
  observation_capacity = 0;
  for (i = 0; i < history_count; i++) {
    const RuleforgeValue *prices;
    if (currency || csv) {
      if (!r_e_profile_has(profiles, profile_count, r_e_text(&history[i], "baseType"))) {
        continue;
      }
    } else if (strcmp(r_e_text(&history[i], "class"), class_name) != 0) {
      continue;
    }
    prices = r_e_field(&history[i], "prices");
    observation_capacity += prices != NULL && prices->kind == RVK_LIST ? prices->as.list->count : 1U;
  }
  if (observation_capacity > 4000000U) {
    goto invalid;
  }
  observations =
      (REconomyObservation *)nexus_data_arena_allocate(&eval->compilation->arena, (observation_capacity + 1U) * NEXUS_SIZEOF(*observations));
  scratch = (REconomyPoint *)nexus_data_arena_allocate(&eval->compilation->arena, (observation_capacity + 1U) * NEXUS_SIZEOF(*scratch));
  newest = 0;
  observation_count = 0;
  for (i = 0; i < history_count; i++) {
    REconomyObservation *row;
    const char          *base;
    boolean              valid;
    base = r_e_text(&history[i], "baseType");
    if (currency || csv) {
      valid = r_e_profile_has(profiles, profile_count, base);
    } else {
      valid = strcmp(r_e_text(&history[i], "class"), class_name) == 0;
      if (valid && names_count > 0) {
        valid = FALSE;
        for (j = 0; j < names_count; j++) {
          if (names[j].kind == RVK_STRING && strcmp(names[j].as.string, base) == 0) {
            valid = TRUE;
            break;
          }
        }
      }
    }
    if (!valid) {
      continue;
    }
    {
      const RuleforgeValue *prices;
      uint32                samples;
      uint32                sample;
      prices = r_e_field(&history[i], "prices");
      samples = prices != NULL && prices->kind == RVK_LIST ? prices->as.list->count : 1;
      for (sample = 0; sample < samples; sample++) {
        f_real price;
        price = prices != NULL ? r_e_sample(&history[i], "prices", sample, 0) : r_e_number(&history[i], "chaosValue", 0);
        if (price <= 0) {
          continue;
        }
        row = &observations[observation_count++];
        row->base = base;
        row->name = r_e_text(&history[i], "name");
        row->league = r_e_text(&history[i], "league");
        row->price = price;
        row->listings = r_e_sample(&history[i], "listings", sample, r_e_number(&history[i], "listingCount", 0));
        row->bucket = (int64)r_e_sample(&history[i], "buckets", sample, r_e_number(&history[i], "bucket", 0));
        row->supply = strcmp(r_e_text(&history[i], "source"), "stash") == 0 && row->listings >= 5;
        row->liquid = row->supply && row->listings >= 20;
        row->reliable = strcmp(r_e_text(&history[i], "state"), "reliable") == 0 &&
                        (row->supply ||
                         (strcmp(r_e_text(&history[i], "source"), "exchange") == 0 && r_e_sample(&history[i], "tradedUnits", sample, 0) >= 20));
        if (row->bucket > newest) {
          newest = row->bucket;
        }
      }
    }
  }
  if (csv) {
    boolean *eligible;
    uint32   kept;
    eligible = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)profile_count + 1U) * NEXUS_SIZEOF(boolean));
    for (i = 0; i < profile_count; i++) {
      for (j = 0; j < observation_count; j++) {
        if (observations[j].reliable && strcmp(observations[j].base, r_e_text(&profiles[i], "baseType")) == 0) {
          eligible[i] = TRUE;
          break;
        }
      }
    }
    kept = 0;
    for (j = 0; j < observation_count; j++) {
      for (i = 0; i < profile_count; i++) {
        if (eligible[i] && strcmp(observations[j].base, r_e_text(&profiles[i], "baseType")) == 0) {
          observations[kept++] = observations[j];
          break;
        }
      }
    }
    observation_count = kept;
  }

  eval->steps += observation_count;
  if (eval->settings != NULL && eval->steps > eval->settings->step_limit) {
    goto invalid;
  }
  for (i = 0; i < observation_count; i++) {
    observations[i].weight = pow(0.5, (f_real)(newest - observations[i].bucket) / half_life);
  }
  qsort(observations, observation_count, sizeof(*observations), r_e_observation_compare);
  aggregates = (REconomyAggregate *)nexus_data_arena_allocate(&eval->compilation->arena,
                                                              ((uint_large)observation_count + 1U) * NEXUS_SIZEOF(*aggregates));
  aggregate_count = 0;
  for (start = 0; start < observation_count; start = end) {
    REconomyAggregate *aggregate;
    f_real             general;
    f_real             chase;
    f_real             chase_supply;
    uint32             name_start;
    uint32             name_end;
    end = start + 1;
    while (end < observation_count && strcmp(observations[start].league, observations[end].league) == 0 &&
           strcmp(observations[start].base, observations[end].base) == 0) {
      end++;
    }
    general = r_e_robust(observations + start, end - start, scratch, 20);
    chase = -1;
    chase_supply = 0;
    for (name_start = start; name_start < end; name_start = name_end) {
      f_real price;
      name_end = name_start + 1;
      while (name_end < end && strcmp(observations[name_start].name, observations[name_end].name) == 0) {
        name_end++;
      }
      price = r_e_robust(observations + name_start, name_end - name_start, scratch, 20);
      if (price > chase) {
        chase = price;
        chase_supply = r_e_supply(observations + name_start, name_end - name_start);
      }
    }
    aggregate = &aggregates[aggregate_count++];
    aggregate->base = observations[start].base;
    aggregate->league = observations[start].league;
    aggregate->price = chase * chase_weight + general * (1 - chase_weight);
    aggregate->listings = chase_supply * chase_weight + r_e_supply(observations + start, end - start) * (1 - chase_weight);
    aggregate->supply = aggregate->listings > 0;
  }
  mean = 0;
  listing_mean = 0;
  listing_count = 0;
  for (i = 0; i < aggregate_count; i++) {
    mean += log(aggregates[i].price + offset);
    if (aggregates[i].supply) {
      listing_mean += log(aggregates[i].listings + 1);
      listing_count++;
    }
  }
  if (aggregate_count > 0) {
    mean /= aggregate_count;
  }
  if (listing_count > 0) {
    listing_mean /= listing_count;
  }
  variance = 0;
  listing_variance = 0;
  for (i = 0; i < aggregate_count; i++) {
    f_real d;
    d = log(aggregates[i].price + offset) - mean;
    variance += d * d;
    if (aggregates[i].supply) {
      d = log(aggregates[i].listings + 1) - listing_mean;
      listing_variance += d * d;
    }
  }
  if (aggregate_count > 0) {
    variance = sqrt(variance / aggregate_count);
  }
  if (listing_count > 0) {
    listing_variance = sqrt(listing_variance / listing_count);
  }
  scores = (REconomyScore *)nexus_data_arena_allocate(&eval->compilation->arena,
                                                      ((uint_large)aggregate_count + profile_count + 1U) * NEXUS_SIZEOF(*scores));
  score_count = 0;
  if (currency || csv) {
    for (i = 0; i < profile_count; i++) {
      const char *base;
      boolean     eligible;
      base = r_e_text(&profiles[i], "baseType");
      eligible = !csv;
      for (j = 0; csv && j < observation_count; j++) {
        if (observations[j].reliable && strcmp(observations[j].base, base) == 0) {
          eligible = TRUE;
          break;
        }
      }
      if (eligible) {
        scores[score_count].base = base;
        scores[score_count].crafting = r_e_number(&profiles[i], "crafting", 0);
        score_count++;
      }
    }
    if (csv && score_count < 2) {
      score_count = 0;
    }
  } else {
    for (i = 0; i < aggregate_count; i++) {
      boolean exists;
      exists = FALSE;
      for (j = 0; j < score_count; j++) {
        if (strcmp(scores[j].base, aggregates[i].base) == 0) {
          exists = TRUE;
          break;
        }
      }
      if (!exists) {
        scores[score_count++].base = aggregates[i].base;
      }
    }
  }
  craft_mean = 0;
  craft_sd = 0;
  for (i = 0; i < score_count; i++) {
    craft_mean += scores[i].crafting;
  }
  if (score_count > 0) {
    craft_mean /= score_count;
  }
  for (i = 0; i < score_count; i++) {
    f_real d;
    d = scores[i].crafting - craft_mean;
    craft_sd += d * d;
  }
  if (score_count > 0) {
    craft_sd = sqrt(craft_sd / score_count);
  }
  stacks[0] = 1;
  stacks[1] = 3;
  stacks[2] = 5;
  stacks[3] = 10;
  stacks[4] = 20;
  qsort(scores, score_count, sizeof(*scores), r_e_base_compare);
  for (j = 0; j < observation_count; j++) {
    uint32 low;
    uint32 high;
    if (!observations[j].reliable) {
      continue;
    }
    low = 0;
    high = score_count;
    while (low < high) {
      uint32 mid;
      int    cmp;
      mid = low + (high - low) / 2;
      cmp = strcmp(scores[mid].base, observations[j].base);
      if (cmp < 0) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    if (low < score_count && strcmp(scores[low].base, observations[j].base) == 0) {
      scores[low].reliable = TRUE;
      if (observations[j].bucket > scores[low].bucket) {
        scores[low].bucket = observations[j].bucket;
      }
    }
  }
  for (i = 0; i < score_count; i++) {
    f_real total_weight;
    total_weight = 0;
    for (j = 0; j < aggregate_count; j++) {
      f_real weight;
      f_real rarity;
      if (strcmp(scores[i].base, aggregates[j].base) != 0) {
        continue;
      }
      weight = 0;
      for (t = 0; t < weight_count; t++) {
        if (strcmp(r_e_text(&weights[t], "league"), aggregates[j].league) == 0) {
          weight = r_e_number(&weights[t], "weight", 0);
          break;
        }
      }
      rarity = aggregates[j].supply && listing_variance > 0 ? -(log(aggregates[j].listings + 1) - listing_mean) / listing_variance : 0;
      total_weight += weight;
      for (t = 0; t < 5; t++) {
        f_real value;
        value = variance > 0 ? (log(aggregates[j].price * stacks[t] + offset) - mean) / variance : 0;
        scores[i].score[t] += weight * (0.65 * value + 0.35 * rarity);
      }
    }
    for (t = 0; t < 5; t++) {
      scores[i].score[t] = (1 - craft_weight) * (total_weight > 0 ? scores[i].score[t] / total_weight : 0) +
                           craft_weight * (craft_sd > 0 ? (scores[i].crafting - craft_mean) / craft_sd : 0);
    }
  }
  qsort(scores, score_count, sizeof(*scores), r_e_score_compare);
  k = score_count < tiers ? score_count : tiers;
  assignment = (uint32 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)score_count + 1U) * NEXUS_SIZEOF(uint32));
  for (t = 0; t < k; t++) {
    centres[t] = scores[(2 * t + 1) * score_count / (2 * k)].score[0];
  }
  for (iteration = 0; iteration < 200 && k > 0; iteration++) {
    boolean changed;
    changed = iteration == 0;
    for (t = 0; t < k; t++) {
      sums[t] = 0;
      assignment_count[t] = 0;
    }
    for (i = 0; i < score_count; i++) {
      uint32 nearest;
      nearest = r_e_nearest(centres, k, scores[i].score[0]);
      changed = changed || assignment[i] != nearest;
      assignment[i] = nearest;
      sums[nearest] += scores[i].score[0];
      assignment_count[nearest]++;
    }
    if (!changed) {
      break;
    }
    for (t = 0; t < k; t++) {
      if (assignment_count[t] > 0) {
        centres[t] = sums[t] / assignment_count[t];
      }
    }
  }
  for (t = 0; t < k; t++) {
    order[t] = t;
  }
  for (t = 1; t < k; t++) {
    uint32 at;
    uint32 value;
    at = t;
    value = order[t];
    while (at > 0 && centres[order[at - 1]] < centres[value]) {
      order[at] = order[at - 1];
      at--;
    }
    order[at] = value;
  }
  for (t = 0; t < k; t++) {
    sums[t] = centres[order[t]];
  }
  for (t = 0; t < k; t++) {
    centres[t] = sums[t];
  }
  state = ruleforge_value_list_create(eval->compilation, score_count * ((currency || csv) ? 5U : 1U));
  groups = ruleforge_value_list_create(eval->compilation, 640);
  groups->count = 0;
  start = 0;
  for (i = 0; i < score_count; i++) {
    int64 best;
    best = (int64)tiers;
    for (t = 0; t < ((currency || csv) ? 5U : 1U); t++) {
      const RuleforgeValue *old;
      int64                 proposed;
      int64                 held;
      int64                 candidate;
      int64                 confirmations;
      int64                 bucket;
      const char           *fields[] = {"group", "baseType", "stack", "tier", "candidate", "confirmations", "bucket", "enabled", "vaal"};
      RuleforgeValue        values[9];
      proposed = k == 0 || scores[0].score[0] == scores[score_count - 1].score[0] ? (int64)tiers
                                                                                  : (int64)r_e_nearest(centres, k, scores[i].score[t]) + 1;
      held = proposed;
      candidate = 0;
      confirmations = 0;
      bucket = scores[i].bucket;
      old = NULL;
      for (j = 0; j < previous_count; j++) {
        if (strcmp(r_e_text(&previous[j], "group"), group_name) == 0 && strcmp(r_e_text(&previous[j], "baseType"), scores[i].base) == 0 &&
            (int64)r_e_number(&previous[j], "stack", 1) == stacks[t]) {
          old = &previous[j];
          break;
        }
      }
      if (old != NULL) {
        held = (int64)r_e_number(old, "tier", proposed);
        candidate = (int64)r_e_number(old, "candidate", 0);
        confirmations = (int64)r_e_number(old, "confirmations", 0);
        bucket = (int64)r_e_number(old, "bucket", 0);
        if (held == proposed) {
          candidate = 0;
          confirmations = 0;
          if (scores[i].bucket > bucket) {
            bucket = scores[i].bucket;
          }
        } else if (scores[i].reliable && scores[i].bucket > bucket) {
          if (candidate != proposed) {
            candidate = proposed;
            confirmations = 0;
          }
          confirmations++;
          bucket = scores[i].bucket;
          if (confirmations >= (proposed < held ? 2 : 3)) {
            held = proposed;
            candidate = 0;
            confirmations = 0;
          }
        }
      }
      if (held < 1 || held > (int64)tiers) {
        goto invalid;
      }
      if (t > 0 && held > best) {
        held = best;
      }
      values[0] = ruleforge_value_string(group_name);
      values[1] = ruleforge_value_string(scores[i].base);
      values[2] = ruleforge_value_integer(stacks[t]);
      values[3] = ruleforge_value_integer(held);
      values[4] = ruleforge_value_integer(candidate);
      values[5] = ruleforge_value_integer(confirmations);
      values[6] = ruleforge_value_integer(bucket);
      values[7] = ruleforge_value_flag(t == 0 || held < best);
      values[8] = ruleforge_value_flag(strcmp(class_name, "Gems") == 0 && strncmp(scores[i].base, "Vaal ", 5) == 0);
      state->items[start++] = r_e_record(eval, fields, values, 9);
      if (held < best) {
        best = held;
      }
    }
  }
  /* Coalesce disjoint base names without changing stack or tier precedence. */
  for (t = 5; t > 0; t--) {
    uint32 tier;
    for (tier = 1; tier <= tiers; tier++) {
      uint32 vaal;
      for (vaal = 0; vaal < 2; vaal++) {
        RuleforgeList *bases;
        uint32         found;
        const char    *fields[] = {"group", "bases", "tier", "stack", "vaal"};
        RuleforgeValue values[5];
        found = 0;
        for (i = 0; i < state->count; i++) {
          const RuleforgeValue *v;
          v = &state->items[i];
          if (r_e_number(v, "tier", 0) == tier && r_e_number(v, "stack", 0) == stacks[t - 1] && r_e_field(v, "enabled")->as.flag &&
              r_e_field(v, "vaal")->as.flag == (boolean)vaal) {
            found++;
          }
        }
        if (found == 0) {
          continue;
        }
        bases = ruleforge_value_list_create(eval->compilation, found);
        found = 0;
        for (i = 0; i < state->count; i++) {
          const RuleforgeValue *v;
          v = &state->items[i];
          if (r_e_number(v, "tier", 0) == tier && r_e_number(v, "stack", 0) == stacks[t - 1] && r_e_field(v, "enabled")->as.flag &&
              r_e_field(v, "vaal")->as.flag == (boolean)vaal) {
            bases->items[found++] = ruleforge_value_string(r_e_text(v, "baseType"));
          }
        }
        values[0] = ruleforge_value_string(group_name);
        values[1] = ruleforge_value_list(bases);
        values[2] = ruleforge_value_integer(tier);
        values[3] = ruleforge_value_integer(stacks[t - 1]);
        values[4] = ruleforge_value_flag((boolean)vaal);
        groups->items[groups->count++] = r_e_record(eval, fields, values, 5);
      }
    }
  }
  {
    const char    *fields[] = {"groups", "state"};
    RuleforgeValue values[2];
    values[0] = ruleforge_value_list(groups);
    values[1] = ruleforge_value_list(state);
    cached = (REconomyCache *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*cached));
    cached->next = (REconomyCache *)eval->economy_cache;
    for (i = 0; i < 4; i++) {
      cached->arguments[i] = identities[i];
    }
    cached->result = r_e_record(eval, fields, values, 2);
    eval->economy_cache = cached;
    return cached->result;
  }
invalid:
  ruleforge_eval_fail(eval, "RF0530", span, NULL,
                      "economy.TierHistory needs valid history, league weights and 1..64 tiers with bounded policy weights");
  return ruleforge_value_none();
}
