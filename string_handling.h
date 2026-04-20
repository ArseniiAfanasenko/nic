#include <stdbool.h>
#include <stddef.h>

typedef struct {
  const char* data;
  size_t count;
} StringView;

StringView sv_from_cstr(const char* cstr);
StringView sv_from_cstr_sized(const char* cstr, size_t size);
StringView sv_slice(StringView src, size_t i1, size_t i2);
bool sv_equal(const StringView s1, const StringView s2);

// TODO: actually arenas and not malloc
typedef struct {
  union {
    StringView sv;
    struct {
      char* data;
      size_t count;
    };
  };
  size_t capacity;
} StringArena;

// TODO: in nilang, we would use a range_usize instead and not hold pointer to the arena.
// Since an Arena is stored on the stack, the holder knows already where to look to.
// If it was deallocated from the stack, then the pointer would be invalid anyway.
typedef struct {
  const StringArena* arena;
  size_t start;
  size_t count;
} StringArenaRef;

StringArenaRef make_string_arena_ref(const StringArena* const arena, const size_t count);
StringView string_arena_ref_to_sv(const StringArenaRef* const ref);

char* string_arena_alloc(StringArena* const target, const size_t count);
size_t string_arena_append_sv(StringArena* const target, const StringView elem);

size_t string_arena_printf(StringArena* const target, const char *fmt, ...);

// Takes a printf-style format string and returns a formatted string.
const char *format(const char *fmt, ...);
