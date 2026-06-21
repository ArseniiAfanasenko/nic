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

typedef uint64_t IdentifierUID;

/*
Note (Nilpo):
See note about IdentifierUID type to get what the hell is happening here.
*/
IdentifierUID compress_short_identifier_into_identifier_uid(StringView const identifier_sv) {
  IdentifierUID res = identifier_sv.count << 48;
  // TODO: metaprogramming and co.
  uint8_t lookup_table[256] = {
    ['A'] = 1,
    ['B'] = 2,
    ['C'] = 3,
    ['D'] = 4,
    ['E'] = 5,
    ['F'] = 6,
    ['G'] = 7,
    ['H'] = 8,
    ['I'] = 9,
    ['J'] = 10,
    ['K'] = 11,
    ['L'] = 12,
    ['M'] = 13,
    ['N'] = 14,
    ['O'] = 15,
    ['P'] = 16,
    ['Q'] = 17,
    ['R'] = 18,
    ['S'] = 19,
    ['T'] = 20,
    ['U'] = 21,
    ['V'] = 22,
    ['W'] = 23,
    ['X'] = 24,
    ['Y'] = 25,
    ['Z'] = 26,

    ['a'] = 27,
    ['b'] = 28,
    ['c'] = 29,
    ['d'] = 30,
    ['e'] = 31,
    ['f'] = 32,
    ['g'] = 33,
    ['h'] = 34,
    ['i'] = 35,
    ['j'] = 36,
    ['k'] = 37,
    ['l'] = 38,
    ['m'] = 39,
    ['n'] = 40,
    ['o'] = 41,
    ['p'] = 42,
    ['q'] = 43,
    ['r'] = 44,
    ['s'] = 45,
    ['t'] = 46,
    ['u'] = 47,
    ['v'] = 48,
    ['w'] = 49,
    ['x'] = 50,
    ['y'] = 51,
    ['z'] = 52,

    ['0'] = 53,
    ['1'] = 54,
    ['2'] = 55,
    ['3'] = 56,
    ['4'] = 57,
    ['5'] = 58,
    ['6'] = 59,
    ['7'] = 60,
    ['8'] = 61,
    ['9'] = 62,

    ['_'] = 63,
  };
  uint8_t under_cursor = 0;
  uint64_t compressed_value = 0;
  // Note (Nilpo): I don't think you can reasonably SIMD that, unfortunately.
  for (size_t i = 0; i < identifier_sv.count; i += 1) {
    under_cursor = identifier_sv.data[i];
    compressed_value = lookup_table[under_cursor];
    res |= compressed_value << (6 * i);
  }
  return res;
}

int main(int argc, const char* argv[]) {
  if (argc <= 1) {
    fprintf(stderr, "Incorrect usage.\n");
    return 1;
  }
  StringView identifier_sv = sv_from_cstr(argv[1]);
  fprintf(stderr, "0x%lX\n", compress_short_identifier_into_identifier_uid(identifier_sv));
}
