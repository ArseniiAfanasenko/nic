#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char* data;
  size_t count;
} StringView;

StringView sv_from_cstr(const char* const cstr) {
  return (StringView){cstr, strlen(cstr)};
}

uint16_t sv_as_u16(StringView sv) {
  if (sv.count > 2) {
    fprintf(stderr, "Integer representation of string view {%ld, \"%.*s\"} contains more than 2 bytes\n", sv.count, (int)sv.count, sv.data);
    abort();
  }
  uint16_t res = 0;
  memcpy(&res, sv.data, sv.count);
  printf("\"%.*s\" as u16: 0x%04X\n", (int)sv.count, sv.data, res);
  return res;
}

uint32_t sv_as_u32(StringView sv) {
  if (sv.count > 4) {
    fprintf(stderr, "Integer representation of string view {%ld, \"%.*s\"} contains more than 4 bytes\n", sv.count, (int)sv.count, sv.data);
    abort();
  }
  uint32_t res = 0;
  memcpy(&res, sv.data, sv.count);
  printf("\"%.*s\" as u32: 0x%08X\n", (int)sv.count, sv.data, res);
  return res;
}

uint64_t sv_as_u64(StringView sv) {
  if (sv.count > 8) {
    fprintf(stderr, "Integer representation of string view {%ld, \"%.*s\"} contains more than 8 bytes\n", sv.count, (int)sv.count, sv.data);
    abort();
  }
  uint64_t res = 0;
  memcpy(&res, sv.data, sv.count);
  printf("\"%.*s\" as u64: 0x%016lX\n", (int)sv.count, sv.data, res);
  return res;
}
int main(int argc, const char* argv[]) {
  if (argc <= 2) {
    fprintf(stderr, "Incorrect usage.\n");
    return 1;
  }
  switch (argv[1][0]) {
    case '2': {
      sv_as_u16(sv_from_cstr(argv[2]));
      return 0;
    }
    case '4': {
      sv_as_u32(sv_from_cstr(argv[2]));
      return 0;
    }
    case '8': {
      sv_as_u64(sv_from_cstr(argv[2]));
      return 0;
    }
  }
  fprintf(stderr, "Incorrect usage.\n");
  return 1;
}
