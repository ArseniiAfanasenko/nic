#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "string_handling.h"

StringView sv_from_cstr(const char* cstr) {
  return (StringView){cstr, strlen(cstr)};
}

// TODO: use a scratch arena
// Takes a printf-style format string and returns a formatted string.
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
