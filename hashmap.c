#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hashmap.h"

// TODO: rewrite to use arenas.

// Initial hash bucket size
#define INIT_SIZE 16

// Rehash if the usage exceeds 70%.
#define HIGH_WATERMARK 70

// We'll keep the usage below 50% after rehashing.
#define LOW_WATERMARK 50

// Represents a deleted hash entry
#define TOMBSTONE ((void *)-1)
#define TOMBSTONE_CASE ((uintptr_t)-1)

static uint64_t fnv_hash(StringView sv) {
  uint64_t hash = 0xcbf29ce484222325;
  for (size_t i = 0; i < sv.count; i++) {
    hash *= 0x100000001b3;
    hash ^= (unsigned char)sv.data[i];
  }
  return hash;
}

// Make room for new entries in a given hashmap by removing
// tombstones and possibly extending the bucket size.
static void rehash(HashMap *map) {
  // Compute the size of the new hashmap.
  size_t nkeys = 0;
  for (size_t i = 0; i < map->capacity; ++i)
    if (map->buckets[i].key.data && map->buckets[i].key.data != TOMBSTONE)
      nkeys++;

  size_t cap = map->capacity;
  while ((nkeys * 100) / cap >= LOW_WATERMARK)
    cap = cap * 2;
  assert(cap > 0);

  // Create a new hashmap and copy all key-values.
  HashMap new_map = {0};
  new_map.buckets = calloc(cap, sizeof(HashEntry));
  new_map.capacity = cap;

  for (size_t i = 0; i < map->capacity; ++i) {
    HashEntry *ent = &map->buckets[i];
    if (ent->key.data && ent->key.data != TOMBSTONE)
      hashmap_put_sv(&new_map, ent->key, ent->val);
  }

  assert(new_map.count == nkeys);
  free(map->buckets);
  *map = new_map;
}

static HashEntry *get_entry_sv(HashMap *map, StringView key) {
  if (!map->buckets)
    return NULL;

  uint64_t hash = fnv_hash(key);
  uint64_t msk = map->capacity - 1;
  HashEntry *buckets = map->buckets;

  for (;;) {
    HashEntry *ent = &buckets[hash++ & msk];

    switch ((uintptr_t)ent->key.data) {
    case 0: {
      return NULL;
    }
    default:
      if (ent->key.count == key.count && !memcmp(ent->key.data, key.data, key.count))
        return ent;
    case TOMBSTONE_CASE: {
      break;
    }
    }
  }
}

HashEntry *hashmap_get_or_insert_sv(HashMap *map, StringView key) {
  if (!map->buckets) {
    map->buckets = calloc(INIT_SIZE, sizeof(HashEntry));
    map->capacity = INIT_SIZE;
  } else if ((map->count * 100) / map->capacity >= HIGH_WATERMARK) {
    rehash(map);
  }

  uint64_t hash = fnv_hash(key);
  uint64_t msk = map->capacity - 1;
  HashEntry *buckets = map->buckets;

  for (;;) {
    HashEntry *ent = &buckets[hash++ & msk];

    switch ((uintptr_t)ent->key.data) {
    case 0:
      ent->key = key;
      map->count++;
      return ent;
    default:
      if (ent->key.count == key.count && !memcmp(ent->key.data, key.data, key.count))
        return ent;
    case TOMBSTONE_CASE: {
      break;
    }
    }
  }
}

void *hashmap_get(HashMap *map, const char *key) {
  return hashmap_get_sv(map, sv_from_cstr(key));
}

void *hashmap_get_sv(HashMap *map, StringView sv) {
  HashEntry *ent = get_entry_sv(map, sv);
  return ent ? ent->val : NULL;
}

void hashmap_put(HashMap *map, const char *key, void *val) {
  hashmap_put_sv(map, sv_from_cstr(key), val);
}

void hashmap_put_sv(HashMap *map, StringView key, void *val) {
  HashEntry *ent = hashmap_get_or_insert_sv(map, key);
  ent->val = val;
}

void hashmap_delete(HashMap *map, const char *key) {
  hashmap_delete_sv(map, sv_from_cstr(key));
}

void hashmap_delete_sv(HashMap *map, StringView key) {
  HashEntry *ent = get_entry_sv(map, key);
  if (ent)
    ent->key.data = TOMBSTONE;
}
