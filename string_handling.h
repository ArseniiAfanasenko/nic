#include <stddef.h>

typedef struct {
  const char* data;
  size_t count;
} StringView;

StringView sv_from_cstr(const char* cstr);

// Takes a printf-style format string and returns a formatted string.
const char *format(const char *fmt, ...);
