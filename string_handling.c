#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "string_handling.h"

StringView sv_from_cstr(const char* const cstr) {
  return (StringView){cstr, strlen(cstr)};
}

StringView sv_from_cstr_sized(const char* const cstr, const size_t size) {
  return (StringView){cstr, size};
}

bool sv_equal(const StringView sv1, const StringView sv2) {
  if (sv1.count != sv2.count) return false;
  return !strncmp(sv1.data, sv2.data, sv1.count);
}

StringView sv_slice(const StringView src, const size_t i1, const size_t i2) {
  return (StringView){src.data + i1, i2 - i1};
}

StringArenaRef make_string_arena_ref(const StringArena* const arena, const size_t count) {
  return (StringArenaRef){arena, arena->count - count, count};
}

StringView string_arena_ref_to_sv(const StringArenaRef* const ref) {
  return sv_slice(ref->arena->sv, ref->start, ref->start + ref->count);
}

// TODO: actually arena and not malloc
char* string_arena_alloc(StringArena* const target, const size_t count) {
  size_t new_count = target->count + count;
  if (new_count >= target->capacity) {
    size_t new_capacity = MAX(target->capacity * 2, new_count);
    target->data = realloc(target->data, new_capacity * sizeof(target->data[0]));
    target->capacity = new_capacity;
  };
  target->count = new_count;
  return target->data + target->count - count;
}

size_t string_arena_append_sv(StringArena* const target, const StringView elem) {
  string_arena_alloc(target, elem.count);
  memcpy(target->data + target->count - elem.count, elem.data, elem.count);
  return elem.count;
}

size_t string_arena_printf(StringArena* const target, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  // + 1 is for null terminate vsnprintf always writes
  size_t count = vsnprintf(NULL, 0, fmt, ap) + 1;
  va_end(ap);

  va_start(ap, fmt);
  vsnprintf(string_arena_alloc(target, count), count, fmt, ap);
  va_end(ap);
  return count;
}

// TODO: use a scratch arena
// Takes a printf-style format string and returns a formatted string.
/*
const char *format(const char *fmt, ...) {
  char *buf;
  size_t buflen;
  FILE *out = open_memstream(&buf, &buflen);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(out, fmt, ap);
  va_end(ap);
  fclose(out);
  return buf;
}
*/
