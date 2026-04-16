// Hashmap implementation is taken from slimcc:
// https://github.com/fuhsnn/slimcc
// It is modified a bit to use StringView.

#include "string_handling.h"

typedef struct {
  StringView key;
  void *val;
} HashEntry;

typedef struct {
  HashEntry *buckets;
  size_t count;
  size_t capacity;
} HashMap;

HashEntry *hashmap_get_or_insert_sv(HashMap *map, StringView key);
void *hashmap_get(HashMap *map, const char *key);
void *hashmap_get_sv(HashMap *map, StringView key);
void hashmap_put(HashMap *map, const char *key, void *val);
void hashmap_put_sv(HashMap *map, StringView key, void *val);
void hashmap_delete(HashMap *map, const char *key);
void hashmap_delete_sv(HashMap *map, StringView key);
