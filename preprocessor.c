#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

// TODO: this is one giant file for now because porting it to nilang that way will be way easier later.

// TODO: will be builtin operators in nilang
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

typedef char Byte;

// TODO: should probably be global constant as part of nilib.
typedef struct {
  size_t memory_page_size_bytes;
  // TODO: actually do this
  // TODO: do we actually need it? with loop_stride and stuff?
  size_t vector_register_size_bytes;
} SystemInfo;

SystemInfo system_info;

typedef struct {
  const char* data;
  size_t count;
} StringView;

// TODO: in nilang, there would be more robust mechanism for generic types.
// Here, the count and capacity are for amount of elements, not for bytes.

// TODO: for regular arenas (e.g. not struct of arrays and co) it probably makes sense to just do one implementation.
// TODO: return errors in allocations as values.
// TODO: make type-generic arenas in nilang.
// TODO: fix this writeup
/*
Note: There are two main ways to approach allocating contiguous (in virtual memory) buffer.
You can either:
- Do initial call to mmap, and then do mremap when need to expand.
- Initially reserve a huge contiguous (in virtual memory) buffer via mmap, and
then commit additional memory via mprotect when needed.
The second approach is nicer because pointer stability.
TODO: do we actually need pointer stability? If we use arena references instead of pointers?
Note that both approaches require a syscall every time you expand, which is expensive time-wise.
Ideal approach to memory allocation would be to actually use some heuristic to determine the size of needed memory
on first allocation, e.g. if you want to read file contents you can use "stat", and then never expand.
Sometimes the heuristic is not obvious at first glance, for example if you are tokenizing a file you can't really
easily tell how many tokens you will end up with from theory.
In such cases, I recommend to first observe from practice what is the average amount you need from previous stage of
the program using expanding arenas, add some percent in the formula for worst cases, and change this stage of program 
to use static/non-expanding arena to reduce syscall count.
This approach also allows to handle failure to allocate memory cleanly.
*/
// TODO: think of interface, what do we return??
// Note: probably slice.

// TODO: test this.
// TODO: in nilang, with default parameters we can use default funtion names
#define MAKE_TYPED_ARENA_DEFINITION(T, struct_name, init_function_name, alloc_function_name, append_function_name) \
typedef struct {\
  T* data;\
  size_t count;\
  size_t capacity;\
  size_t reserve_count_bytes;\
} struct_name;\
\
/* TODO: in nilang we would also return an error. */\
/* TODO: change interface -> reserve_count, capacity, count. */\
struct_name (init_function_name)(size_t const reserve_count_bytes, size_t const capacity) {\
  /*fprintf(stderr, "initial arena allocation\n");*/\
  struct_name res = {0};\
  /*This is a way to round value to nearest multiple of page size, using the fact that page size is multiple of 2.*/\
  /*Ideally, we will have something like invariants and be able to optimize based on that.*/\
  size_t rounded_reserve_count_bytes = (reserve_count_bytes + system_info.memory_page_size_bytes - 1) & ~(system_info.memory_page_size_bytes - 1);\
  /* fprintf(stderr, "rounded_reserve_count_bytes: %ld\n", rounded_reserve_count_bytes); */\
  res.reserve_count_bytes = rounded_reserve_count_bytes;\
  /* Note: it is important that result of mmap is page-aligned and zeroed. */\
  Byte* virtual_alloc_ptr = mmap(NULL, rounded_reserve_count_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\
  size_t initial_capacity_bytes = (capacity * sizeof(T) + system_info.memory_page_size_bytes - 1) & ~(system_info.memory_page_size_bytes - 1);\
  /* fprintf(stderr, "initial_capacity_bytes: %ld\n", initial_capacity_bytes); */\
  /* Size value of mprotect needs to be a multiple of page size. */\
  mprotect(virtual_alloc_ptr, initial_capacity_bytes, PROT_READ | PROT_WRITE);\
  res.data = (T*)virtual_alloc_ptr;\
  /* fprintf(stderr, "initial_capacity: %ld\n", initial_capacity_bytes / sizeof(T)); */\
  res.capacity = initial_capacity_bytes / sizeof(T);\
  res.count = 0;\
  return res;\
};\
\
/* TODO: separate alloc and append_count */\
T* (alloc_function_name)(struct_name* const target, size_t const count) {\
  size_t new_count = target->count + count;\
  if (new_count >= target->capacity) /* TODO: unlikely */ {\
    /* fprintf(stderr, "arena expansion\n"); */\
    /* TODO: expanding by factor of 2 is matematically suboptimal. */\
    size_t new_capacity_bytes = MAX((target->capacity * sizeof(T)) * 2, new_count * sizeof(T));\
    new_capacity_bytes = (new_capacity_bytes + system_info.memory_page_size_bytes - 1) & ~(system_info.memory_page_size_bytes - 1);\
    /* fprintf(stderr, "new_capacity_bytes: %ld\n", new_capacity_bytes); */\
    /* TODO: check if new_capacity_bytes is more than reserve, return OUT OF MEMORY error. */\
    mprotect((char*)target->data, new_capacity_bytes, PROT_READ | PROT_WRITE);\
    target->capacity = new_capacity_bytes / sizeof(T);\
    /* fprintf(stderr, "new_capacity: %ld\n", target->capacity); */\
  };\
  target->count = new_count;\
  return target->data + target->count - count;\
};\
\
/* TODO: error if alloc failed*/\
void (append_function_name)(struct_name* const target, const T* elem) {\
  *(alloc_function_name(target, 1)) = *elem;\
};

#define typed_arena_last(target) (target).data[(target).count - 1]

MAKE_TYPED_ARENA_DEFINITION(char, StringArena, string_arena_init, string_arena_alloc, string_arena_append)

// TODO: small string optimization. Requires endianness.
// Since an Arena is stored on the stack, the holder knows already where to look to.
// If it was deallocated from the stack, then the pointer would be invalid anyway.
typedef struct {
  size_t start;
  size_t count;
} StringArenaRef;

StringView sv_from_cstr(const char* const cstr) {
  return (StringView){cstr, strlen(cstr)};
}

bool sv_equal(const StringView sv1, const StringView sv2) {
  if (sv1.count != sv2.count) return false;
  return !strncmp(sv1.data, sv2.data, sv1.count);
}

StringView sv_slice(const StringView src, const size_t i1, const size_t i2) {
  return (StringView){src.data + i1, i2 - i1};
}

StringArenaRef make_string_arena_ref(const StringArena* const arena, const size_t count) {
  return (StringArenaRef){arena->count - count, count};
}

StringView string_arena_ref_to_sv(const StringArena* const arena, const StringArenaRef* const ref) {
  return sv_slice(*(const StringView*)arena, ref->start, ref->start + ref->count);
}

size_t string_arena_append_sv(StringArena* const target, const StringView elem) {
  string_arena_alloc(target, elem.count);
  memcpy(target->data + target->count - elem.count, elem.data, elem.count);
  return elem.count;
}

typedef enum {
  OpenOpStatus_success,
  OpenOpStatus_component_of_path_does_not_exist,
  OpenOpStatus_component_of_path_has_permissions_prohibiting_requested_access,
  OpenOpStatus_component_of_path_is_not_a_directory,
  _OpenOpStatus_unaccounted_error,
  _OpenOpStatus_count,
} OpenOperationStatus;

// TODO: in nilang, we will have cross-platform "file handle" abstraction.
typedef int LinuxFileDescriptor;

typedef struct {
  LinuxFileDescriptor fd;
  OpenOperationStatus status;
} _OpenOperationRes;

// TODO: in nilang, we will have scratch arenas so path would be string view or something like that.
// TODO: better mode enum
// TODO: in nilang, we will have native multiple return values.
_OpenOperationRes open_file(const char* path, int oflag) {
  _OpenOperationRes res = {0};

  int fd = open(path, oflag);
  if (fd == -1) {
    switch (errno) {
      case EACCES:
        res.status = OpenOpStatus_component_of_path_has_permissions_prohibiting_requested_access;
	break;
      case ENOENT:
	res.status = OpenOpStatus_component_of_path_does_not_exist;
	break;
      case ENOTDIR:
	res.status = OpenOpStatus_component_of_path_is_not_a_directory;
	break;
      default:
	res.status = _OpenOpStatus_unaccounted_error;
	break;
    }
    return res;
  }
  res = (_OpenOperationRes){fd, OpenOpStatus_success};
  return res;
}

typedef struct stat LinuxStat;

typedef enum {
  StatOpStatus_success,
  StatOpStatus_file_no_longer_exists,
  StatOpStatus_low_level_io_error,
  // TODO: internal filesystem limit
  _StatOpStatus_unaccounted_error,
  _StatOpStatus_count,
} StatOperationStatus;

typedef struct {
  LinuxStat buf;
  StatOperationStatus status;
} _StatOperationRes;

// TODO: implement
_StatOperationRes stat_file_descriptor(LinuxFileDescriptor fd) {
  _StatOperationRes res = {0};
  if (fstat(fd, &res.buf) == -1) {
    switch (errno) {
      case EBADF:
        res.status = StatOpStatus_file_no_longer_exists;
	break;
      case EIO:
        res.status = StatOpStatus_low_level_io_error;
	break;
      default:
	res.status = _StatOpStatus_unaccounted_error;
	break;
    }
    return res;
  }
  res.status = StatOpStatus_success;
  return res;
}

typedef enum {
  IOOpStatus_success,
  IOOpStatus_file_no_longer_exists,
  IOOpStatus_expected_file_got_directory,
  IOOpStatus_file_was_deleted_while_performing_io, // aka "somebody yanked the HDD out of the computer"
  IOOpStatus_low_level_io_error,
  _IOOpStatus_unaccounted_error,
  _IOOpStatus_count,
} IOOperationStatus;

typedef enum {
  ReadOpStatus_success                              = IOOpStatus_success,
  ReadOpStatus_file_no_longer_exists                = IOOpStatus_file_no_longer_exists,
  ReadOpStatus_expected_file_got_directory          = IOOpStatus_expected_file_got_directory,
  ReadOpStatus_file_was_deleted_while_performing_io = IOOpStatus_file_was_deleted_while_performing_io,
  ReadOpStatus_low_level_io_error                   = IOOpStatus_low_level_io_error,
  _ReadOpStatus_unaccounted_error                   = _IOOpStatus_unaccounted_error,
  // Stuff specific to read.
  ReadOpStatus_not_enough_reserved_memory,
  ReadOpStatus_memory_allocation_error,
  ReadOpStatus_file_changed,
  _ReadOpStatus_count,
} ReadOperationStatus;

// TODO: in nilang, it would take a byte arena, not string arena.
// TODO: test this in all scenarios.
ReadOperationStatus read_entire_file_descriptor_into_string_arena(LinuxFileDescriptor fd, StringArena* const target) {
  ReadOperationStatus res = 0;
  // TODO: in nilang, we will have "defer"

  auto stat_res = stat_file_descriptor(fd);
  if (stat_res.status != StatOpStatus_success) {
    switch (stat_res.status) {
      case StatOpStatus_file_no_longer_exists:
        res = ReadOpStatus_file_no_longer_exists;
	break;
      case StatOpStatus_low_level_io_error:
        res = ReadOpStatus_low_level_io_error;
	break;
      default:
	res = _ReadOpStatus_unaccounted_error;
	break;
    }
    return res;
  }

  if (S_ISDIR(stat_res.buf.st_mode)) {
    res = ReadOpStatus_expected_file_got_directory;
    return res;
  }

  if (target->reserve_count_bytes - target->count * sizeof(char) < (size_t)stat_res.buf.st_size) {
    res = ReadOpStatus_not_enough_reserved_memory;
    return res;
  }

  // TODO: check
  string_arena_alloc(target, stat_res.buf.st_size);
  // TODO: fix when changing arena API
  target->count -= (size_t)stat_res.buf.st_size;

  size_t total_read_bytes_count = 0;
  for (ssize_t read_bytes_count = 0; total_read_bytes_count < (size_t)stat_res.buf.st_size;) {
    read_bytes_count = read(fd, &target->data[target->count + total_read_bytes_count], (size_t)stat_res.buf.st_size - total_read_bytes_count);

    if (read_bytes_count < 0) {
      switch (errno) {
        case EINTR: // Interrupted by signal. Trying again.
          continue;
	case EBADF:
	  res = ReadOpStatus_file_was_deleted_while_performing_io;
	  break;
	case EIO:
	  res = ReadOpStatus_low_level_io_error;
	  break;
	default:
	  res = _ReadOpStatus_unaccounted_error;
	  break;
      }
      return res;
    }

    if (read_bytes_count == 0) /* EOF */ {
      break;
    }

    total_read_bytes_count += (size_t)read_bytes_count;
  }

  target->count += total_read_bytes_count;

  // TODO: check if original stat matches current one, also say that file changed
  if (total_read_bytes_count != (size_t)stat_res.buf.st_size) {
    res = ReadOpStatus_file_changed;
    return res;
  }

  res = ReadOpStatus_success;
  return res;
}

typedef enum {
  WriteOpStatus_success                              = IOOpStatus_success,
  WriteOpStatus_file_no_longer_exists                = IOOpStatus_file_no_longer_exists,
  WriteOpStatus_expected_file_got_directory          = IOOpStatus_expected_file_got_directory,
  WriteOpStatus_file_was_deleted_while_performing_io = IOOpStatus_file_was_deleted_while_performing_io,
  WriteOpStatus_low_level_io_error                   = IOOpStatus_low_level_io_error,
  _WriteOpStatus_unaccounted_error                   = _IOOpStatus_unaccounted_error,
  // Stuff specific to write.
  WriteOpStatus_trying_to_write_too_much,
  WriteOpStatus_no_space_remaining_on_the_device,
  // TODO: handle more errno values.
  _WriteOpStatus_count,
} WriteOperationStatus;

// TODO: in nilang, it would take a sized array of Byte readonly, not StringView.
// TODO: test this in all scenarios.
WriteOperationStatus write_entire_file_descriptor(LinuxFileDescriptor fd, StringView buffer) {
  WriteOperationStatus res = 0;

  auto stat_res = stat_file_descriptor(fd);
  if (stat_res.status != StatOpStatus_success) {
    switch (stat_res.status) {
      case StatOpStatus_file_no_longer_exists:
        res = WriteOpStatus_file_no_longer_exists;
	break;
      case StatOpStatus_low_level_io_error:
        res = WriteOpStatus_low_level_io_error;
	break;
      default:
	res = _WriteOpStatus_unaccounted_error;
	break;
    }
    return res;
  }
  fprintf(stderr, "At least could stat.\n");

  if (S_ISDIR(stat_res.buf.st_mode)) {
    res = WriteOpStatus_expected_file_got_directory;
    return res;
  }

  fprintf(stderr, "At least regular file.\n");

  size_t total_written_bytes_count = 0;
  for (ssize_t written_bytes_count = 0; total_written_bytes_count < buffer.count;) {
    written_bytes_count = write(fd, &buffer.data[total_written_bytes_count], buffer.count - total_written_bytes_count);
    fprintf(stderr, "Written bytes count: %ld.\n", written_bytes_count);

    if (written_bytes_count < 0) {
      // TODO: handle everything
      switch (errno) {
        case EINTR: // Interrupted by signal. Trying again.
          continue;
	case EBADF:
	  res = WriteOpStatus_file_was_deleted_while_performing_io;
	  break;
	case EIO:
	  res = WriteOpStatus_low_level_io_error;
	  break;
	default:
	  res = _WriteOpStatus_unaccounted_error;
	  break;
      }
      return res;
    }

    // TODO: what is the case here?
    #if 0
    if (read_bytes_count == 0) /* EOF */ {
      break;
    }
    #endif

    total_written_bytes_count += (size_t)written_bytes_count;
  }

  res = WriteOpStatus_success;
  return res;
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

typedef struct {
  uint64_t key;
  uint64_t value;
} U64ToU64HashmapEntry;

// TODO: in nilang, we should probably add something like size_t slots_occupied,
// and check whether or not putting a thing would put stuff over threshold.
MAKE_TYPED_ARENA_DEFINITION(U64ToU64HashmapEntry, U64ToU64Hashmap, u64_to_u64_hashmap_init, u64_to_u64_hashmap_alloc, u64_to_u64_hashmap_append);

// Bool is for whether entry with the same key was already in the hashmap.
// TODO: less horrible name
bool u64_to_u64_hashmap_insert_unchecked(U64ToU64Hashmap* const hm, U64ToU64HashmapEntry entry) {
  uint64_t key = entry.key;
  uint64_t cursor = key;
  uint64_t element_under_cursor = hm->data[cursor % hm->count].key;
  /*
  Note (Nilpo):
  We do not have additional array of sentinel "is_slot_occupied" values.
  The idea is, if key in slot is equal to zero, then slot is empty.
  This works because we usually store enums or indexes, and we have "zero_stub" in pretty much all enums.
  */
  // TODO: robin hood hashing.
  // TODO: simd.
  while (element_under_cursor != 0 && element_under_cursor != key) {
    cursor += 1;
    element_under_cursor = hm->data[cursor % hm->count].key;
  }
  hm->data[cursor % hm->count].key   = entry.key;
  hm->data[cursor % hm->count].value = entry.value;
  return element_under_cursor == key;
}

void u64_to_u64_hashmap_insert_assume_no_such_key(U64ToU64Hashmap* hm, U64ToU64HashmapEntry entry) {
  uint64_t cursor = entry.key;
  uint64_t element_under_cursor = hm->data[cursor % hm->count].key;
  /*
  Note (Nilpo):
  We do not have additional array of sentinel "is_slot_occupied" values.
  The idea is, if key in slot is equal to zero, then slot is empty.
  This works because we usually store enums or indexes, and we have "zero_stub" in pretty much all enums.
  */
  // TODO: robin hood hashing.
  // TODO: simd.
  while (element_under_cursor != 0) {
    cursor += 1;
    element_under_cursor = hm->data[cursor % hm->count].key;
  }
  hm->data[cursor % hm->count].key   = entry.key;
  hm->data[cursor % hm->count].value = entry.value;
}

typedef struct {
 bool was_in_hashmap;
 size_t position;
} _HashmapGetRes;

_HashmapGetRes u64_to_u64_hashmap_get(const U64ToU64Hashmap* const hm, uint64_t key) {
  uint64_t cursor = key;
  uint64_t element_under_cursor = hm->data[cursor % hm->count].key;
  /*
  Note (Nilpo):
  We do not have additional array of sentinel "is_slot_occupied" values.
  The idea is, if key in slot is equal to zero, then slot is empty.
  This works because we usually store enums or indexes, and we have "zero_stub" in pretty much all enums.

  Note (Nilpo):
  There is invariant that there are enough empty slots so that search stops quickly.
  It is maintained by "insert" function.
  */
  while (element_under_cursor != 0 && element_under_cursor != key) {
    cursor += 1;
    element_under_cursor = hm->data[cursor % hm->count].key;
  }
  return (_HashmapGetRes){element_under_cursor == key, cursor % hm->count};
}

/*
Note (nilpo):
This one is a little complicated.
First of all, we store all identifiers in a string arena. Each unique identifier is stored only once.
Usually, you would use size_t (== uint64_t on 64-bit systems) to index start of each identifier,
and uint16_t to store their length. Effectively, IdentifierUID == std::pair(size_t, uint16_t).
Now, notice that we don't actually need full 64 bits to index start.
On most systems, only 48 bits in pointers are actually used. On very few systems, 57 bits.
TODO: add source.
48 bits are enough to index 2^^48 == 256TB of memory. The limit is practically impossible to reach.
This means, we can use first 16 bits to store the length, meaning IdentifierUID fits into single 64 bit integer:
| length (16 bits) | index of start in arena (48 bits) |.
This is nice, because now we only need to build a hashmap where value stored is single uint64_t.
TODO: pack small strings into 48 bits so we don't need to store them in the arena, saving memory.
TODO: it is done, make a writeup
*/
typedef uint64_t IdentifierUID;

// TODO: better hash, simd, use the fact that identifiers have restricted character set.
// TODO: are we sure this hash is never zero?
uint64_t fnv_hash(StringView const sv) {
  uint64_t hash = 0xcbf29ce484222325;
  for (size_t i = 0; i < sv.count; i++) {
    hash *= 0x100000001b3;
    hash ^= (unsigned char)sv.data[i];
  }
  return hash;
}

bool identifier_uid_equal(const StringArena* const identifier_arena,
                          IdentifierUID const ref,
                          StringView const key) {
  /*
  Note (Nilpo):
  See explanation on what is happening here at IdentifierUID type description.
  */
  if (key.count != (ref >> 48)) {
    return false;
  }
  return !memcmp(&identifier_arena->data[ref & 0x00FFFFFF], key.data, key.count);
}

MAKE_TYPED_ARENA_DEFINITION(IdentifierUID, IdentifierHashset, identifier_hashset_init, identifier_hashset_alloc, identifier_hashset_append);

IdentifierHashset identifier_hashset_init_with_enough_memory_for_reasonably_low_collision_rate(size_t count) {
  // TODO: Round to power of two to optimize modulo?
  // TODO: better heuristic? benchmark
  size_t slot_count = count * 2;
  IdentifierHashset res = identifier_hashset_init(slot_count * sizeof(U64ToU64HashmapEntry), slot_count);
  identifier_hashset_alloc(&res, slot_count);
  return res;
}

_HashmapGetRes identifier_hashset_check(const IdentifierHashset* const hm,
                                        const StringArena* const identifier_arena,
			                                  StringView const key) {
  size_t offset = 0;
  uint64_t hash = fnv_hash(key);
  // fprintf(stderr, "hash: %llu, hm_count: %llu\n", hash, hm->count);
  uint64_t identifier_uid_under_cursor = hm->data[hash % hm->count];
  // fprintf(stderr, "identifier_uid_under_cursor: %llu\n", identifier_uid_under_cursor);
  /*
  Note (Nilpo):
  We do not have additional array of sentinel "is_slot_occupied" values.
  The idea is, if key in slot is equal to zero, then slot is empty.
  In case of identifier references, length of the identifier is never equal to zero, so it's always true.
  TODO: update description for small string optimization.

  Note (Nilpo):
  TODO: explain collisions.
  There is invariant that there are enough empty slots so that search stops quickly.
  It is maintained by "insert" function.
  */
  while (identifier_uid_under_cursor != 0 &&
         !identifier_uid_equal(identifier_arena, identifier_uid_under_cursor, key)) {
    ++offset;
    identifier_uid_under_cursor = hm->data[(hash + offset) % hm->count];
  }
  return (_HashmapGetRes){identifier_uid_under_cursor != 0, (hash + offset) % hm->count};
}

void identifier_hashset_insert_unchecked(const IdentifierHashset* const hm, size_t position, IdentifierUID ref) {
  hm->data[position] = ref;
}

typedef enum {
  _NI_TK_zero_stub,
  _NI_TK_stringifiable_start,
  // TODO: automatically suggest adding NoReturn in tidy phase.
  NI_TK_NoReturn = _NI_TK_stringifiable_start,

  NI_TK_false,
  NI_TK_true,
  NI_TK_null,

  NI_TK_intrinsic,

  NI_TK_type,
  NI_TK_subtype,
  NI_TK_alias,

  NI_TK_struct,
  NI_TK_union,
  NI_TK_enum,
  NI_TK_proc,

  NI_TK_global,
  NI_TK_thread_local,

  NI_TK_readonly,
  NI_TK_volatile,
  NI_TK_exclusive,

  NI_TK_if,
  NI_TK_else,
  NI_TK_ifx,
  // TODO: selectx?
  NI_TK_select,
  NI_TK_then,
  NI_TK_loop,
  NI_TK_loop_stride,
  NI_TK_switch,
  // TODO: do we really need it?
  // NI_TK_switchx,
  NI_TK_case,
  NI_TK_fall,
  NI_TK_continue,
  NI_TK_break,
  NI_TK_return,

  /* OPERATORS: */
  // Unary:
  NI_TK_bitwise_negate,         // aka "~"
  NI_TK_bool_negate,            // aka "!"
  // Either unary or binary depending on the context:
  NI_TK_minus,
  NI_TK_ampersand,              // aka "&"
  // Binary:
  // - Comparison:
  NI_TK_equal,                  // aka "=="
  NI_TK_not_equal,              // aka "!="
  NI_TK_less,                   // aka "<"
  NI_TK_less_equal,             // aka "<="
  NI_TK_greater,                // aka ">"
  NI_TK_greater_equal,          // aka ">="
  // - Logical:
  NI_TK_logic_or,               // aka "||"
  NI_TK_logic_and,              // aka "&&"
  // - Bit manipulation:
  // TODO: arithmetic and logical shifts?
  NI_TK_shift_left,             // aka "<<"
  NI_TK_shift_right,            // aka ">>"
  NI_TK_most_significant_bit,   // aka ">|"
  NI_TK_count_trailing_zeros,   // aka "|<"
  NI_TK_bitwise_xor,            // aka "^"
  NI_TK_bitwise_or,             // aka "|"
  // - Arithmetic:
  NI_TK_add,                    // aka "+"
  NI_TK_saturated_add,          // aka "++"
  NI_TK_saturated_sub,          // aka "--"
  NI_TK_mult,                   // aka "*"
  NI_TK_saturated_mult,         // aka "**"
  NI_TK_div,                    // aka "/"
  NI_TK_pymod,                  // aka "%"
  // TODO: kinda conflicts with saturated arithmetic, redo
  NI_TK_cmod,                   // aka "%%"
  // Binary, terminating (assignment):
  // - Assign/bitwise copy:
  NI_TK_assign,                 // aka "="
  // - Bit manipulation:
  // TODO: arithmetic and logical shifts
  NI_TK_shift_left_assign,      // aka "<<="
  NI_TK_shift_right_assign,     // aka ">>="
  NI_TK_bitwise_xor_assign,     // aka "^="
  NI_TK_bitwise_or_assign,      // aka "|="
  NI_TK_bitwise_and_assign,     // aka "&="
  // - Arithmetic:
  NI_TK_add_assign,             // aka "+="
  NI_TK_saturated_add_assign,   // aka "++="
  NI_TK_sub_assign,             // aka "-="
  NI_TK_saturated_sub_assign,   // aka "--="
  NI_TK_mult_assign,            // aka "*="
  NI_TK_saturated_mult_assign,  // aka "**="
  NI_TK_div_assign,             // aka "/="
  NI_TK_pymod_assign,           // aka "%="
  // TODO: redo
  NI_TK_cmod_assign,            // aka "%%="
  /* PUNCTUATORS: */
  NI_TK_dot,                    // aka ".", either constant, cast, or aggregate type member access.
  NI_TK_comma,                  // aka ","
  NI_TK_semicolon,              // aka ";"
  NI_TK_colon,                  // aka ":"
  NI_TK_open_rounded,           // aka "("
  NI_TK_close_rounded,          // aka ")"
  NI_TK_open_curly,             // aka "{"
  NI_TK_close_curly,            // aka "}"
  NI_TK_open_bracket,           // aka "["
  NI_TK_close_bracket,          // aka "]"
  _NI_TK_stringifiable_end = NI_TK_close_bracket,

  /* Things which don't have static string representation */
  NI_TK_base2_literal,
  NI_TK_base8_literal,
  NI_TK_base10_literal,
  NI_TK_base16_literal,
  NI_TK_identifier,
  NI_TK_before_beginning_of_file,
  NI_TK_after_end_of_file,
  _NI_TK_count,
} NiTokenKind;

// TODO: something like @exhaustive
// TODO: something like @don't this do identifier
// TODO: align
static const char* ni_token_cstr[_NI_TK_count] = {
  [NI_TK_NoReturn] = "NoReturn",
  [NI_TK_false] = "false",
  [NI_TK_true] = "true",
  [NI_TK_null] = "null",
  [NI_TK_intrinsic] = "intrinsic",
  [NI_TK_type] = "type",
  [NI_TK_subtype] = "subtype",
  [NI_TK_alias] = "alias",
  [NI_TK_struct] = "struct",
  [NI_TK_union] = "union",
  [NI_TK_enum] = "enum",
  [NI_TK_proc] = "proc",
  [NI_TK_global] = "global",
  [NI_TK_thread_local] = "thread_local",
  [NI_TK_readonly] = "readonly",
  [NI_TK_volatile] = "volatile",
  [NI_TK_exclusive] = "exclusive",
  [NI_TK_if] = "if",
  [NI_TK_else] = "else",
  [NI_TK_ifx] = "ifx",
  [NI_TK_select] = "select",
  [NI_TK_then] = "then",
  [NI_TK_loop] = "loop",
  [NI_TK_loop_stride] = "loop_stride",
  [NI_TK_switch] = "switch",
  [NI_TK_case] = "case",
  [NI_TK_fall] = "fall",
  [NI_TK_continue] = "continue",
  [NI_TK_break] = "break",
  [NI_TK_return] = "return",
  [NI_TK_bitwise_negate] = "~",
  [NI_TK_bool_negate] = "!",
  [NI_TK_add] = "+",
  [NI_TK_minus] = "-",
  [NI_TK_ampersand] = "&",
  [NI_TK_equal] = "==",
  [NI_TK_not_equal] = "!=",
  [NI_TK_less] = "<",
  [NI_TK_less_equal] = "<=",
  [NI_TK_greater] = ">",
  [NI_TK_greater_equal] = ">=",
  [NI_TK_logic_or] = "||",
  [NI_TK_logic_and] = "&&",
  [NI_TK_shift_left] = "<<",
  [NI_TK_shift_right] = ">>",
  [NI_TK_most_significant_bit] = ">|",
  [NI_TK_count_trailing_zeros] = "|<",
  [NI_TK_bitwise_xor] = "^",
  [NI_TK_bitwise_or] = "|",
  [NI_TK_saturated_add] = "++",
  [NI_TK_saturated_sub] = "--",
  [NI_TK_mult] = "*",
  [NI_TK_saturated_mult] = "**",
  [NI_TK_div] = "/",
  [NI_TK_pymod] = "%",
  // TODO: redo
  [NI_TK_cmod] = "%%",
  [NI_TK_assign] = "=",
  [NI_TK_shift_left_assign] = "<<=",
  [NI_TK_shift_right_assign] = ">>=",
  [NI_TK_bitwise_xor_assign] = "^=",
  [NI_TK_bitwise_or_assign] = "|=",
  [NI_TK_bitwise_and_assign] = "&=",
  [NI_TK_add_assign] = "+=",
  [NI_TK_saturated_add_assign] = "++=",
  [NI_TK_sub_assign] = "-=",
  [NI_TK_saturated_sub_assign] = "--=",
  [NI_TK_mult_assign] = "*=",
  [NI_TK_saturated_mult_assign] = "**=",
  [NI_TK_div_assign] = "/=",
  [NI_TK_pymod_assign] = "%=",
  // TODO: redo
  [NI_TK_cmod_assign] = "%%=",
  [NI_TK_dot] = ".",
  [NI_TK_comma] = ",",
  [NI_TK_semicolon] = ";",
  [NI_TK_colon] = ":",
  [NI_TK_open_rounded] = "(",
  [NI_TK_close_rounded] = ")",
  [NI_TK_open_curly] = "{",
  [NI_TK_close_curly] = "}",
  [NI_TK_open_bracket] = "[",
  [NI_TK_close_bracket] = "]",
};

StringView ni_token_sv[_NI_TK_count];

typedef struct {
  // TODO: struct of arrays
  size_t position_in_file;
  NiTokenKind kind;
  union {
    uint16_t identifier_length;
    IdentifierUID identifier_uid;
    size_t literal_length;
  };
} NiToken;

MAKE_TYPED_ARENA_DEFINITION(NiToken, NiTokenArena, ni_token_arena_init, ni_token_arena_alloc, ni_token_arena_append);

// TODO: add ascii to function name
size_t from_start_length_of_sequence_of_alphanumeric_or_underscore(StringView const buffer) {
  // TODO: simd, shuffle && nibble trick
  size_t i = 0;
  uint8_t cur_char = 0;
  for (; i < buffer.count; i += 1) {
    cur_char = buffer.data[i];
    // TODO: lookup table
    if (!(('A' <= cur_char && cur_char <= 'Z') ||
	  ('a' <= cur_char && cur_char <= 'z') ||
	  ('0' <= cur_char && cur_char <= '9') ||
	  cur_char == '_')) {
      break;
    }
  }
  return i;
}

typedef enum {
  _NI_TK_ERR_zero_stub,
  NI_TK_ERR_base10_literal_base16_lowercase,
  NI_TK_ERR_base10_literal_base16_uppercase,
  NI_TK_ERR_base10_literal_alpha_or_underscore,
  _NI_TK_ERR_count,
} NiTokenizationError;

MAKE_TYPED_ARENA_DEFINITION(NiTokenizationError, NiTokenizationErrorArena, ni_tokenization_error_arena_init, ni_tokenization_error_arena_alloc, ni_tokenization_error_arena_append);

typedef struct {
  size_t length;
  NiTokenizationError status;
} _Base10ParseRes;

_Base10ParseRes from_start_length_of_sequence_of_numeric_base10(StringView const buffer) {
  // TODO: simd, shuffle && nibble trick
  size_t i = 0;
  uint8_t cur_char = 0;
  NiTokenizationError status = _NI_TK_ERR_zero_stub;
  for (; i < buffer.count; i += 1) {
    cur_char = buffer.data[i];
    // TODO: lookup table
    if ('0' <= cur_char && cur_char <= '9') {
      continue;
    }
    if ('A' <= cur_char && cur_char <= 'Z') {
      switch (status) {
        case _NI_TK_ERR_zero_stub:
          status = NI_TK_ERR_base10_literal_base16_uppercase;
	  break;
	case NI_TK_ERR_base10_literal_base16_lowercase:
	  status = NI_TK_ERR_base10_literal_alpha_or_underscore;
	  break;
	default: break;
      }
      continue;
    }
    if ('a' <= cur_char && cur_char <= 'z') {
      switch (status) {
        case _NI_TK_ERR_zero_stub:
          status = NI_TK_ERR_base10_literal_base16_lowercase;
	  break;
	case NI_TK_ERR_base10_literal_base16_uppercase:
	  status = NI_TK_ERR_base10_literal_alpha_or_underscore;
	  break;
	default: break;
      }
      continue;
    }
    if (cur_char == '_') {
      status = NI_TK_ERR_base10_literal_alpha_or_underscore;
      continue;
    }
    if ('0' <= cur_char && cur_char <= '9') {
      continue;
    }
    break;
  }

  return (_Base10ParseRes){i, status};
}

MAKE_TYPED_ARENA_DEFINITION(size_t, USizeArena, usize_arena_init, usize_arena_alloc, usize_arena_append);

void skip_whitespaces_and_mark_newline_indexes(size_t* cursor, StringView const buffer, USizeArena* const newline_indexes_arena) {
  // TODO: simd
  // Note: I know this all is extremely awkward code, it will be way less awkward after being transformed to simd.
  // So there is no point in making it be good for now.
  uint16_t next_two_chars = 0;
  uint8_t cur_char = 0;
  while (*cursor + 1 < buffer.count) {
    memcpy(&next_two_chars, &buffer.data[*cursor], 2);
    if (next_two_chars == 0x0A0D) { // "\r\n"
      usize_arena_append(newline_indexes_arena, cursor);
      *cursor += 2;
      continue;
    }
    cur_char = buffer.data[*cursor];
    // TODO: LUT
    if (cur_char == '\r' || cur_char == '\n') {
      usize_arena_append(newline_indexes_arena, cursor);
      *cursor += 1;
      continue;
    }
    if (cur_char == ' ' || cur_char == '\f' || cur_char == '\t' || cur_char == '\v') {
      *cursor += 1;
      continue;
    }
    break;
  }
  if (*cursor < buffer.count) {
    cur_char = buffer.data[*cursor];
    if (cur_char == '\r' || cur_char == '\n') {
      usize_arena_append(newline_indexes_arena, cursor);
      *cursor += 1;
    }
    if (cur_char == ' ' || cur_char == '\f' || cur_char == '\t' || cur_char == '\v') {
      *cursor += 1;
    }
  }
}

NiTokenKind classify_identifier(StringView const identifier_sv) {
  // TODO: everything else.
  switch (identifier_sv.count) {
    case 2: {
      uint16_t as_U16 = 0;
      memcpy(&as_U16, identifier_sv.data, 2);
      switch (as_U16) {
        case 0x6669: return NI_TK_if;
        default:     return NI_TK_identifier;
      }
    }
    case 3: {
      uint32_t as_U32 = 0;
      memcpy(&as_U32, identifier_sv.data, 3);
      switch (as_U32) {
        default:         return NI_TK_identifier;
      }
    }
    case 4: {
      uint32_t as_U32 = 0;
      memcpy(&as_U32, identifier_sv.data, 4);
      switch (as_U32) {
        case 0x65707974: return NI_TK_type;
        default:         return NI_TK_identifier;
      }
    }
    case 5: {
      uint64_t as_U64 = 0;
      memcpy(&as_U64, identifier_sv.data, 5);
      switch (as_U64) {
        case 0x00000065736C6166: return NI_TK_false;
        default:                 return NI_TK_identifier;
      }
    }
    case 9: {
      if (memcmp("intrinsic", identifier_sv.data, 9) == 0) {
        return NI_TK_intrinsic;
      }
      return NI_TK_identifier;
    }
    default: return NI_TK_identifier;
  }
}


/*
Note (Nilpo):
See note about IdentifierUID type to get what the hell is happening here.
*/
IdentifierUID compress_short_identifier_into_identifier_uid(StringView const identifier_sv) {
  IdentifierUID res = identifier_sv.count << 48;
  // TODO: metaprogramming and co.
  // TODO: funnily enough, perfect hashing??
  static const uint8_t lookup_table[256] = {
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

typedef struct {
  NiTokenArena token_arena;
  // TODO: make error arena last one.
  NiTokenizationErrorArena error_arena;
  USizeArena newline_indexes_arena;
  USizeArena typedef_marks_arena;
  StringArena identifier_arena;
  // TODO: bool unrecoverable_errors;
} NiTokenizationResult;

typedef struct {
  NiTokenArena token_arena;
  USizeArena newline_indexes_arena;
  USizeArena typedef_marks_arena;
} NiMinimalSuccessfulTokenizationResult;

// TODO: store non-semantic tokens (newlines, comments) separately.
NiTokenizationResult nic_tokenize(StringView const buffer) {
  // TODO: use var := ... in nilang;
  // TODO: init error arena
  // TODO: better heuristics for sizes of arenas
  /*
  Note (Nilpo):
  We append 8 "before_beginning_of_file" tokens at the beginning of arena and 8 "after_eof" tokens at the end.
  This is so we can easily lookahead 8 tokens at any point during parsing.
  */
  size_t total_possible_tokens = buffer.count + 16;
  // TODO: "res" variable
  NiTokenArena token_arena = ni_token_arena_init(total_possible_tokens * sizeof(NiToken), total_possible_tokens);
  USizeArena newline_indexes_arena = usize_arena_init(buffer.count * sizeof(size_t), buffer.count);
  USizeArena typedef_marks_arena = usize_arena_init(buffer.count * sizeof(size_t), buffer.count);
  StringArena identifier_arena = string_arena_init(buffer.count * sizeof(size_t), buffer.count);
  /* Note (Nilpo):
  This one is for internal needs.
  We first collect the indexes so we can avoid constantly rehashing the identifier hashtable.
  */
  USizeArena long_identifier_indexes_arena = usize_arena_init(buffer.count * sizeof(size_t), buffer.count);
  size_t cursor = 0;

  NiToken next = {0};

  next = (NiToken){.kind = NI_TK_before_beginning_of_file};
  for (size_t i = 0; i < 8; i += 1) {
    ni_token_arena_append(&token_arena, &next);
  }

  next = (NiToken){0};

  // TODO: while true
  for (;; ni_token_arena_append(&token_arena, &next)) {
    // TODO: skip whitespaces and newlines
    skip_whitespaces_and_mark_newline_indexes(&cursor, buffer, &newline_indexes_arena);
    if (cursor >= buffer.count) {
      break;
    }
    next.position_in_file = cursor;
    // Check on how many characters are remaining, clamped to 3, since it is the number of lookahead characters.
    switch (MIN((buffer.count - cursor), 3)) {
      case 3: {
        // TODO: video about strict aliasing
        uint32_t next_characters = 0;
        memcpy(&next_characters, &buffer.data[cursor], 3);
        // Values here were obtained via helper executable "string_to_int".
        // This will be a builtin feature of nilang.
        switch (next_characters) {
          case 0x003D3C3C: // "<<="
            next.kind = NI_TK_shift_left_assign;
            break;
          case 0x003D2D2D: // "--="
            next.kind = NI_TK_saturated_sub_assign;
            break;
          default:
            next.kind = _NI_TK_zero_stub;
            break;
        }
        if (next.kind != _NI_TK_zero_stub) {
          cursor += 3;
          continue;
        }
      }
      [[fallthrough]];
      case 2: {
        uint16_t next_characters = 0;
        memcpy(&next_characters, &buffer.data[cursor], 2);
        // TODO: sort, perfect hashing
        switch (next_characters) {
          case 0x3D3C: // "<="
            next.kind = NI_TK_less_equal;
            break;
          case 0x3D2B: // "+="
            next.kind = NI_TK_add_assign;
            break;
          case 0x2B2B: // "++"
            next.kind = NI_TK_saturated_add;
            break;
          case 0x2626: // "&&"
            next.kind = NI_TK_logic_and;
            break;
          default:
            next.kind = _NI_TK_zero_stub;
            break;
        }
        if (next.kind != _NI_TK_zero_stub) {
          cursor += 2;
          continue;
        }
      }
      [[fallthrough]];
      case 1: {
        uint8_t next_character = 0;
        memcpy(&next_character, &buffer.data[cursor], 1);
        // TODO: sort, perfect hashing
        switch (next_character) {
          case ',':
            next.kind = NI_TK_comma;
            break;
          case ':':
            next.kind = NI_TK_colon;
            break;
          case ';':
            next.kind = NI_TK_semicolon;
            break;
          case '.':
            next.kind = NI_TK_dot;
            break;
          case '=':
            next.kind = NI_TK_assign;
            break;
          case '~':
            next.kind = NI_TK_bitwise_negate;
            break;
          case '!':
            next.kind = NI_TK_bool_negate;
            break;
          case '+':
            next.kind = NI_TK_add;
            break;
          case '-':
            next.kind = NI_TK_minus;
            break;
          case '*':
            next.kind = NI_TK_mult;
            break;
          case '/':
            next.kind = NI_TK_div;
            break;
          case '{':
            next.kind = NI_TK_open_curly;
            break;
          case '}':
            next.kind = NI_TK_close_curly;
            break;
          case '(':
            next.kind = NI_TK_open_rounded;
            break;
          case ')':
            next.kind = NI_TK_close_rounded;
            break;
          default:
            next.kind = _NI_TK_zero_stub;
            break;
        }
        if (next.kind != _NI_TK_zero_stub) {
          cursor += 1;
          continue;
        }
        // TODO: in nilang we will have switches on ranges instead
        if (('A' <= next_character && next_character <= 'Z') ||
            ('a' <= next_character && next_character <= 'z') ||
            next_character == '_') {
          // We add one to start, because identifiers can contain numbers, but should not begin with one.
          size_t length_after_first = from_start_length_of_sequence_of_alphanumeric_or_underscore(sv_slice(buffer, cursor + 1, buffer.count));
          size_t length = length_after_first + 1;
          // TODO: check if length is >= 2^^16 and report error if so.
          StringView identifier_sv = sv_slice(buffer, cursor, cursor + length);
          next.kind = classify_identifier(identifier_sv);
          switch (next.kind) {
            case NI_TK_identifier: {
              if (length <= 8) {
                next.identifier_uid = compress_short_identifier_into_identifier_uid(identifier_sv);
              } else {
                next.identifier_length = length;
                usize_arena_append(&long_identifier_indexes_arena, &token_arena.count);
              }
              break;
            }
            case NI_TK_type: {
              usize_arena_append(&typedef_marks_arena, &token_arena.count);
              break;
            }
            default:
              break;
          }
          cursor += length;
          continue;
        }
        if ('0' <= next_character && next_character <= '9') {
	      // TODO: detect suffixes like "ULL" and co
          auto _base10_res = from_start_length_of_sequence_of_numeric_base10(sv_slice(buffer, cursor, buffer.count));
          next.kind = NI_TK_base10_literal;
          // TODO: push error
          next.literal_length = _base10_res.length;
          cursor += _base10_res.length;
          continue;
        }
        // TODO: handle this properly.
        fprintf(stderr, "UNEXPECTED CHARACTER: %c, hex: 0x%02X\n", buffer.data[cursor], buffer.data[cursor]);
        exit(1);
      }
    }
  }

  next = (NiToken){.kind = NI_TK_after_end_of_file, .position_in_file = cursor};
  for (size_t i = 0; i < 8; i += 1) {
    ni_token_arena_append(&token_arena, &next);
  }

  // TODO: we have to actually keep it cause modules?
  // Store each identifier in arena only once and update tokens of "identifier" kind with reference to that storage.
  {
    IdentifierHashset identifier_uid_hashset = identifier_hashset_init_with_enough_memory_for_reasonably_low_collision_rate(long_identifier_indexes_arena.count);
    for (size_t i = 0; i < long_identifier_indexes_arena.count; ++i) {
      size_t index_in_token_arena = long_identifier_indexes_arena.data[i];
      NiToken token = token_arena.data[index_in_token_arena];
      StringView key = sv_slice(buffer, token.position_in_file, token.position_in_file + (size_t)token.identifier_length);
      auto _get_res = identifier_hashset_check(&identifier_uid_hashset, &identifier_arena, key);
      if (_get_res.was_in_hashmap) {
        token_arena.data[index_in_token_arena].identifier_uid = identifier_uid_hashset.data[_get_res.position];
	continue;
      }
      size_t index_in_arena = identifier_arena.count;
      // fprintf(stderr, "inserting %.*s, length: %ld, index_in_arena: %ld\n", (int)key.count, key.data, (uint64_t)token.identifier_length, index_in_arena);
      string_arena_append_sv(&identifier_arena, key);
      IdentifierUID identifier_uid = (((uint64_t)token.identifier_length) << 48) | index_in_arena;
      // fprintf(stderr, "identifier_uid, extracted - length: %ld, start: %ld\n", ref >> 48, ref & 0x00FFFFFF);
      identifier_hashset_insert_unchecked(&identifier_uid_hashset, _get_res.position, identifier_uid);
      token_arena.data[index_in_token_arena].identifier_uid = identifier_uid;
    }
  }

  return (NiTokenizationResult){token_arena, {0}, newline_indexes_arena, typedef_marks_arena, identifier_arena};
}

static StringView ni_token_to_debug_sv(const StringArena* const identifier_arena,
                                       StringView const buffer,
                                       StringArena* const target,
				                               const NiToken* const token) {
  StringView res = {0};
  // TODO: in nilang, we will have switches on ranges
  if (_NI_TK_stringifiable_start <= token->kind && token->kind < _NI_TK_stringifiable_end) {
    res.count += string_arena_append_sv(target, ni_token_sv[token->kind]);
    res.data = target->data + target->count - res.count;
    return res;
  }
  switch (token->kind) {
    case _NI_TK_zero_stub:
      res.count += string_arena_append_sv(target, sv_from_cstr("zero stub"));
      break;
    case NI_TK_identifier: {
      res.count += string_arena_append_sv(target, sv_from_cstr("identifier"));
      IdentifierUID identifier_uid = token->identifier_uid;
      size_t start = identifier_uid & 0x00FFFFFF;
      size_t length = identifier_uid >> 48;
      StringView id = sv_slice(*(StringView*)identifier_arena, start, start + length);
      res.count += string_arena_printf(target, ": %.*s", (int)id.count, id.data);
      break;
    }
    case NI_TK_base10_literal: {
      res.count += string_arena_append_sv(target, sv_from_cstr("base10 numeric literal"));
      StringView id = sv_slice(buffer, token->position_in_file, token->position_in_file + (size_t)token->literal_length);
      res.count += string_arena_printf(target, ": %.*s", (int)id.count, id.data);
      break;
    }
    default:
      break;
  }
  res.data = target->data + target->count - res.count;
  return res;
}

typedef enum {
  _NI_SYM_zero_stub,
  NI_SYM_type,
  NI_SYM_procedure,
  NI_SYM_global_variable,
  // TODO: meta-versions of the same things.
  _NI_SYM_count,
} NiSymbolKind;

/*
Note (Nilpo):
| 8 bits - symbol kind | 56 bits - index in respective arena |.
*/
typedef uint64_t NiSymbolUID;

typedef U64ToU64Hashmap GlobalSymbolHashmap;

typedef uint64_t NiTypeUID;

typedef enum {
  _NI_TYPE_PROP_zero_stub,
  NI_TYPE_PROP_supports_comparison               = 1 << 1, // "==", "!="
  NI_TYPE_PROP_supports_implicit_bool_conversion = 1 << 2, // Bool, Bitmask integers (B8-B64)
  NI_TYPE_PROP_supports_ordered_comparison       = 1 << 3, // "<", "<=", ">", ">=", "<=>"
  NI_TYPE_PROP_supports_bitwise                  = 1 << 4, // "<<", ">>", "&", "|", "^", ">|", "|<",
  NI_TYPE_PROP_supports_arithmetic               = 1 << 5, // "+", "-" (binary), "/", "*"
  NI_TYPE_PROP_supports_modulo                   = 1 << 6, // "%"
  NI_TYPE_PROP_supports_signed_arithmetic        = 1 << 7, // "<<<", ">>>", "-" (unary), "%%" (TODO: placeholder name)
  NI_TYPE_PROP_supports_saturated_arithmetic     = 1 << 8, // "++", "--", "**"
  NI_TYPE_PROP_supports_sloppy_arithmetic        = 1 << 9, // "~+", "~-", "~*", "~/"
  NI_TYPE_PROP_supports_ascii_string_literals    = 1 << 10, // Ascii integers (A8-A64)
  NI_TYPE_PROP_supports_utf8_string_literals     = 1 << 11, // UTF-8 integers (C8-C64)
  NI_TYPE_PROP_storage_8bits                     = 0b00 << 30,
  NI_TYPE_PROP_storage_16bits                    = 0b01 << 30,
  NI_TYPE_PROP_storage_32bits                    = 0b10 << 30,
  NI_TYPE_PROP_storage_64bits                    = 0b11 << 30,
  NI_TYPE_PROP_storage_bits_mask                 = 0b11 << 30,
  // Property masks for different types and type classes.
  NI_TYPE_PROP_Bool =
    NI_TYPE_PROP_supports_comparison |
    NI_TYPE_PROP_supports_implicit_bool_conversion |
    NI_TYPE_PROP_storage_8bits,

  NI_TYPE_PROP_Byte = NI_TYPE_PROP_supports_comparison | NI_TYPE_PROP_storage_8bits,

  NI_TYPE_PROP_distinct_integer_mask =
    NI_TYPE_PROP_supports_comparison,

  NI_TYPE_PROP_D8  = NI_TYPE_PROP_distinct_integer_mask | NI_TYPE_PROP_storage_8bits,
  NI_TYPE_PROP_D16 = NI_TYPE_PROP_distinct_integer_mask | NI_TYPE_PROP_storage_16bits,
  NI_TYPE_PROP_D32 = NI_TYPE_PROP_distinct_integer_mask | NI_TYPE_PROP_storage_32bits,
  NI_TYPE_PROP_D64 = NI_TYPE_PROP_distinct_integer_mask | NI_TYPE_PROP_storage_64bits,

  NI_TYPE_PROP_bitmask_integer_mask =
    NI_TYPE_PROP_supports_comparison |
    NI_TYPE_PROP_supports_implicit_bool_conversion |
    NI_TYPE_PROP_supports_bitwise,

  NI_TYPE_PROP_B8  = NI_TYPE_PROP_bitmask_integer_mask | NI_TYPE_PROP_storage_8bits,
  NI_TYPE_PROP_B16 = NI_TYPE_PROP_bitmask_integer_mask | NI_TYPE_PROP_storage_16bits,
  NI_TYPE_PROP_B32 = NI_TYPE_PROP_bitmask_integer_mask | NI_TYPE_PROP_storage_32bits,
  NI_TYPE_PROP_B64 = NI_TYPE_PROP_bitmask_integer_mask | NI_TYPE_PROP_storage_64bits,

  NI_TYPE_PROP_unsigned_integer_mask =
    NI_TYPE_PROP_supports_comparison |
    NI_TYPE_PROP_supports_ordered_comparison |
    NI_TYPE_PROP_supports_bitwise |
    NI_TYPE_PROP_supports_arithmetic |
    NI_TYPE_PROP_supports_modulo |
    NI_TYPE_PROP_supports_saturated_arithmetic,

  NI_TYPE_PROP_U8  = NI_TYPE_PROP_unsigned_integer_mask | NI_TYPE_PROP_storage_8bits,
  NI_TYPE_PROP_U16 = NI_TYPE_PROP_unsigned_integer_mask | NI_TYPE_PROP_storage_16bits,
  NI_TYPE_PROP_U32 = NI_TYPE_PROP_unsigned_integer_mask | NI_TYPE_PROP_storage_32bits,
  NI_TYPE_PROP_U64 = NI_TYPE_PROP_unsigned_integer_mask | NI_TYPE_PROP_storage_64bits,

  NI_TYPE_PROP_signed_integer_mask =
    NI_TYPE_PROP_supports_comparison |
    NI_TYPE_PROP_supports_ordered_comparison |
    NI_TYPE_PROP_supports_bitwise |
    NI_TYPE_PROP_supports_arithmetic |
    NI_TYPE_PROP_supports_modulo |
    NI_TYPE_PROP_supports_signed_arithmetic |
    NI_TYPE_PROP_supports_saturated_arithmetic,

  NI_TYPE_PROP_S8  = NI_TYPE_PROP_signed_integer_mask | NI_TYPE_PROP_storage_8bits,
  NI_TYPE_PROP_S16 = NI_TYPE_PROP_signed_integer_mask | NI_TYPE_PROP_storage_16bits,
  NI_TYPE_PROP_S32 = NI_TYPE_PROP_signed_integer_mask | NI_TYPE_PROP_storage_32bits,
  NI_TYPE_PROP_S64 = NI_TYPE_PROP_signed_integer_mask | NI_TYPE_PROP_storage_64bits,

  // TODO: floats
} NiIntrinsicTypeProperties;

typedef enum {
  _NI_TYPE_zero_stub,
  NI_TYPE_intrinsic,
  NI_TYPE_type,    // aka "hard alias"/"distinct"
  NI_TYPE_subtype, // aka "one-way alias"
  // TODO: soft alias/typedef?
  NI_TYPE_array,
  NI_TYPE_slice,
  _NI_TYPE_count,
} NiTypeKind;

typedef struct {
  NiTypeKind kind;
  /*
  Note (Nilpo):
  We can't remove identifier_uid from this struct, sadly.
  You may ask, what if we make some kind of supplementary NiTypeUID -> NiIdentifierUID hashmap,
  and then embed NiTypeKind into first bits of NiTypeUID?
  Consider this situation:
  Biba : type = U8;
  Boba : type = U8;
  Of course, Biba should not be implicitly convertable to Boba (and wise-versa),
  but with proposed implementation they would share the same NiTypeUID.
  Funnily enough, this would actually be fine for C typedefs, since they are "soft" aliases.
  Hovewer, it should be noted that when we do hard alias of a hard alias:
  Zhoka : type = U8;
  Boka  : type = Zhoka;
  We actually store the underlying type as U8 for both Zhoka and Boka.
  In fact, "tidy" pass actually replaces all hard aliases like Boka:
  Boka : type = Zhoka; => Boka : type = U8;
  With structs, it replaces it with canonical struct definition:
  TheGood : type = struct {...};
  TheBad  : type = TheGood;
  TheUgly : type = TheBad;       => TheUgly : type = TheGood.
  This means that querying underlying type for hard aliases is always only one non-local memory lookup.
  Of course, we can't guarantee the same with subtypes, but let's be real,
  subtype of a subtype (of a subtype)* of a type is extremely rare anyways.
  */
  IdentifierUID identifier_uid;
  union {
    NiIntrinsicTypeProperties properties_mask; // for "intrinsic"
    NiTypeUID underlying_type;                 // for "type" and "subtype"
    NiTypeUID indexed_by_type;                 // for "array" and "slice"
    size_t member_count;                       // for "struct", "union", and "enum"
  };
} NiTypeInfo;

MAKE_TYPED_ARENA_DEFINITION(NiTypeInfo, NiTypeInfoArena, ni_typeinfo_arena_init, ni_typeinfo_arena_alloc, ni_typeinfo_arena_append);

typedef enum {
  _NI_AT_ERR_zero_stub,
  NI_AT_ERR_expected_identifier_and_colon_before_typedef_mark,
  NI_AT_ERR_expected_assign_after_typedef_mark,
  NI_AT_ERR_expected_type_identifier_or_aggregate_type_mark,
  NI_AT_ERR_no_such_intrinsic_type,
  NI_AT_ERR_forgot_semicolon,
  NI_AT_ERR_global_symbol_collision_type_type,
  NI_AT_ERR_unclosed_rounded,
  NI_AT_ERR_empty_expression,
  NI_AT_ERR_incomplete_expression,
  NI_AT_ERR_c_style_assign_usage,
  NI_AT_ERR_c_style_comma_usage,
  _NI_AT_ERR_count,
} NiParsingErrorKind;

typedef struct {
  size_t position_in_file;
  NiParsingErrorKind kind;
  // This looks kinda cursed lol.
  union {
    IdentifierUID collided_identifier_uid;
  };
} NiParsingError;

MAKE_TYPED_ARENA_DEFINITION(NiParsingError, NiParsingErrorArena, ni_parsing_error_arena_init, ni_parsing_error_arena_alloc, ni_parsing_error_arena_append);

bool nic_parse_types(
  const NiTokenArena* token_arena,
  const USizeArena* typedef_marks_arena,
  GlobalSymbolHashmap* global_symbol_hashmap,
  NiTypeInfoArena* typeinfo_arena,
  NiParsingErrorArena* error_arena
) {
  size_t index_in_token_arena = 0;
  NiParsingError error = {0};
  IdentifierUID identifier_uid = 0;
  NiTypeInfo next = {0};

  // Because 0 should not be valid NiTypeUID, because hashmaps.
  typeinfo_arena->count += 1;

  for (size_t cursor = 0; cursor < typedef_marks_arena->count; cursor += 1) {
    index_in_token_arena = typedef_marks_arena->data[cursor];
    // Note (Nilpo): Unchecked access is fine because we pad token_arena with non-existent tokens on both sides.
    // TODO: cmov/csel/select
    if (token_arena->data[index_in_token_arena - 1].kind != NI_TK_colon ||
        token_arena->data[index_in_token_arena - 2].kind != NI_TK_identifier) {
      error.kind = NI_AT_ERR_expected_identifier_and_colon_before_typedef_mark;
    }
    if (token_arena->data[index_in_token_arena + 1].kind != NI_TK_assign) {
      error.kind = NI_AT_ERR_expected_assign_after_typedef_mark;
    }
    if (error.kind != _NI_AT_ERR_zero_stub) {
      error.position_in_file = token_arena->data[index_in_token_arena].position_in_file;
      ni_parsing_error_arena_append(error_arena, &error);
      return false;
    }
    identifier_uid = token_arena->data[index_in_token_arena - 2].identifier_uid;
    next = (NiTypeInfo){.identifier_uid = identifier_uid};

    auto _hashmap_get_res = u64_to_u64_hashmap_get(global_symbol_hashmap, identifier_uid);
    if (_hashmap_get_res.was_in_hashmap) {
      error.position_in_file = token_arena->data[index_in_token_arena - 2].position_in_file;
      error.kind = NI_AT_ERR_global_symbol_collision_type_type;
      error.collided_identifier_uid = identifier_uid;
      ni_parsing_error_arena_append(error_arena, &error);
      return false;
    }

    // TODO: check collisions
    switch (token_arena->data[index_in_token_arena + 2].kind) {
      case NI_TK_intrinsic: {
        // TODO: select
        // TODO: compile-time execution
        switch (identifier_uid) {
          case 0x40000009A9A42: // Bool
            next.properties_mask = NI_TYPE_PROP_Bool;
            break;
          case 0x2000000000F55: // U8
            next.properties_mask = NI_TYPE_PROP_U8;
            break;
          case 0x3000000039ED5: // U64
            next.properties_mask = NI_TYPE_PROP_U64;
            break;
          default:
            next.properties_mask = _NI_TYPE_PROP_zero_stub;
            break;
        }
        if (next.properties_mask == _NI_TYPE_PROP_zero_stub) {
          error.position_in_file = token_arena->data[index_in_token_arena - 2].position_in_file;
          error.kind = NI_AT_ERR_no_such_intrinsic_type;
          ni_parsing_error_arena_append(error_arena, &error);
          return false;
        }
        next.kind = NI_TYPE_intrinsic;
        u64_to_u64_hashmap_insert_assume_no_such_key(global_symbol_hashmap, (U64ToU64HashmapEntry){identifier_uid, typeinfo_arena->count});
        ni_typeinfo_arena_append(typeinfo_arena, &next);
        continue;
      }
      default: {
        error.position_in_file = token_arena->data[index_in_token_arena + 2].position_in_file;
        error.kind = NI_AT_ERR_expected_type_identifier_or_aggregate_type_mark;
        ni_parsing_error_arena_append(error_arena, &error);
        return false;
      }
    }
  }

  return true;
}

typedef enum {
  _NI_AT_zero_stub,
  NI_AT_numeric_constant,
  NI_AT_variable,
  /*
  Note (Nilpo):
  Whilst cast can be treated as unary operator, it is more convenient to consider it a separate kind,
  so we can store identifier_uid the same way as for variable or function.
  */
  NI_AT_cast,
  NI_AT_unary_operator,
  NI_AT_binary_operator,
  NI_AT_ternary_operator,
  NI_AT_procedure_call,
  NI_AT_expression_end,
  NI_AT_declarator,
  NI_AT_assignment,
  NI_AT_statement,
  NI_AT_statement_end,
  NI_AT_block, // aka "compound statement"/"scope"/multiple statements enclosed in {...}
  NI_AT_statement_if,
  NI_AT_type_named,
  _NI_AT_count,
} NiAtomKind;

/*
Note (Nilpo):
TODO: yeah
We have following precedence levels:
- Default. This is when we start parsing from blank slate.
- Logical or.
- Logical and. It has higher precedence than logical or,
  so "a || b && c" will be parsed as "a || (b && c)", which is intuitive.
*/
typedef enum {
  NI_PREC_default,
  NI_PREC_add_sub,      // +, -
  NI_PREC_mult_div_mod, // *, /, %
  NI_PREC_unary,
  _NI_PREC_count,
} NiOperatorPrecedence;

typedef enum {
  _NI_UnOp_zero_stub,
  NI_UnOp_bitwise_negate,
  NI_UnOp_bool_negate,
  NI_UnOp_negate,
  NI_UnOp_addressof,
  _NI_UnOp_count,
} NiUnaryOperatorKind;

typedef enum {
  _NI_BinOp_zero_stub,
  NI_BinOp_add,
  NI_BinOp_sub,
  NI_BinOp_mult,
  NI_BinOp_div,
  _NI_BinOp_count,
} NiBinaryOperatorKind;

typedef enum {
  _NI_TernOp_zero_stub,
  _NI_TernOp_count,
} NiTernaryOperatorKind;

NiOperatorPrecedence ni_binop_precedence[_NI_BinOp_count] = {
  [NI_BinOp_add] = NI_PREC_add_sub,
  [NI_BinOp_sub] = NI_PREC_add_sub,
  [NI_BinOp_mult] = NI_PREC_mult_div_mod,
  [NI_BinOp_div] = NI_PREC_mult_div_mod,
};

const char* ni_unop_cstr[_NI_UnOp_count] = {
  [NI_UnOp_bitwise_negate] = "~",
  [NI_UnOp_bool_negate] = "!",
  [NI_UnOp_negate] = "un-",
  [NI_UnOp_addressof] = "&",
};

StringView ni_unop_sv[_NI_BinOp_count] = {0};

const char* ni_binop_cstr[_NI_BinOp_count] = {
  [NI_BinOp_add] = "+",
  [NI_BinOp_sub] = "-",
  [NI_BinOp_mult] = "*",
  [NI_BinOp_div] = "/",
};

StringView ni_binop_sv[_NI_BinOp_count] = {0};

// TODO: rename NiAtom => NiCode
typedef struct {
  // TODO: struct of arrays
  size_t position_in_file;
  NiAtomKind kind;
  /*
  Note (Nilpo):
  We do not store ammount of arguments for procedure call, since we check correctness immediately during parsing.
  */
  union {
    NiUnaryOperatorKind   unary_op_kind;
    NiBinaryOperatorKind  binary_op_kind;
    NiTernaryOperatorKind ternary_op_kind;
    IdentifierUID         identifier_uid;   // for procedures, variables, declarators, and type_named.
    size_t                expression_count; // for assign.
    size_t                statement_count;  // for blocks, if/elif/else, cases.
    size_t                case_count;       // for switch (if var == {case; ...}), perfect_hash.
    size_t                literal_length;   // TODO: better way to handle?
  };
} NiAtom;

/*
Note (Nilpo):
Unlike most compilers, we do not have an AST (abstract syntax tree).
The reason is, trees are non-linear in memory and bad for cache effects, and also hard to deal with.
Instead, we have a linear "NiAtomArena", where everything is stored in reverse polish notation.
TODO: better and accurate examples
Examples:
a := U64.69;           => |a|69|cast(U64)|:=|stmt_end|
a = b + c + 1;         => |a|b |c        |+ |1       |+|=|stmt_end|
d = func(1, a, 3) + 1; => |d|1 |a        |3 |func    |1|+|=       |stmt_end
Simple conditionals:
if a && b || c { d; } else { e; f; } => |if - 1 statement|a|b|&&|c|"||"|d|else - 2 statements|e|f|
if cond { a; } else if cond2 { b; c; } else { d; } => |cond|if - 1|a|cond2|elif - 2|b|c|else - 1|d|
This might seem a little counter-intutitive, but it makes type-checking quite easy.
*/
MAKE_TYPED_ARENA_DEFINITION(NiAtom, NiAtomArena, ni_atom_arena_init, ni_atom_arena_alloc, ni_atom_arena_append);

// GOD THANK YOU DENIS RITCHIE AND BRIAN KERNIGAN FOR A WONDERFUL LANGUAGE WITHOUT MULTIPLE RETURN VALUES
// AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
// AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
// AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
typedef struct {
  size_t first_index_after_expression;
  NiParsingErrorKind error_kind;
} _ExprParseRes;

// TODO: reorder parameters
_ExprParseRes parse_expression_into_atom_arena(NiAtomArena* const atom_arena,
                                               const NiTokenArena* const token_arena,
                                               size_t cursor,
					                                     NiOperatorPrecedence passed_precedence);

_ExprParseRes parse_subexpression_into_atom_arena(NiAtomArena* const atom_arena,
                                                  const NiTokenArena* const token_arena,
                                                  size_t cursor) {
  size_t new_cursor = cursor;
  NiToken under_cursor = token_arena->data[cursor];
  NiAtom next = {.position_in_file=under_cursor.position_in_file};
  // TODO: do an outer switch?
  // Handling constants.
  {
    switch (under_cursor.kind) {
      case NI_TK_base2_literal:
        [[fallthrough]];
      case NI_TK_base8_literal:
        [[fallthrough]];
      case NI_TK_base10_literal:
        [[fallthrough]];
      case NI_TK_base16_literal:
        next.kind = NI_AT_numeric_constant;
        break;
      default:
        next.kind = _NI_AT_zero_stub;
        break;
    }
    if (next.kind != _NI_AT_zero_stub) {
      next.literal_length = under_cursor.literal_length;
      ni_atom_arena_append(atom_arena, &next);
      new_cursor += 1;
      return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
    }
  }

  // Handling unary operators.
  {
    switch (under_cursor.kind) {
      case NI_TK_bitwise_negate:
        next.unary_op_kind = NI_UnOp_bitwise_negate;
        break;
      case NI_TK_bool_negate:
        next.unary_op_kind = NI_UnOp_bool_negate;
        break;
      case NI_TK_minus:
        next.unary_op_kind = NI_UnOp_negate;
        break;
      case NI_TK_ampersand:
        next.unary_op_kind = NI_UnOp_addressof;
        break;
      default:
        next.unary_op_kind = _NI_UnOp_zero_stub;
        break;
    }
    if (next.unary_op_kind != _NI_UnOp_zero_stub) {
      next.kind = NI_AT_unary_operator;
      new_cursor += 1;
      auto _res = parse_expression_into_atom_arena(atom_arena, token_arena, new_cursor, NI_PREC_unary);
      new_cursor = _res.first_index_after_expression;
      if (_res.error_kind != _NI_AT_ERR_zero_stub) {
	return (_ExprParseRes){new_cursor, _res.error_kind};
      }
      ni_atom_arena_append(atom_arena, &next);
      return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
    }
  }

  // Handling identifiers (type casts, variables, procedures).
  {
    if (under_cursor.kind == NI_TK_identifier) {
      // TODO: right now we assume all identifiers to be variable names
      next.kind = NI_AT_variable;
      next.identifier_uid = under_cursor.identifier_uid;
      ni_atom_arena_append(atom_arena, &next);
      new_cursor += 1;
      return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
    }
  }

  // Handling rounded parenthesis.
  {
    if (under_cursor.kind == NI_TK_open_rounded) {
      new_cursor += 1;
      auto _res = parse_expression_into_atom_arena(atom_arena, token_arena, new_cursor, NI_PREC_default);
      new_cursor = _res.first_index_after_expression;
      if (_res.error_kind != _NI_AT_ERR_zero_stub) {
	return (_ExprParseRes){new_cursor, _res.error_kind};
      }
      if (token_arena->data[new_cursor].kind != NI_TK_close_rounded) {
        return (_ExprParseRes){new_cursor, NI_AT_ERR_unclosed_rounded};
      }
      new_cursor += 1;
      return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
    }
  }

  return (_ExprParseRes){new_cursor, NI_AT_ERR_empty_expression};
}

/*
Note (Nilpo):
The idea for this function is that it returns when encountering something which is not a subexpression or operator.
It also stops when we encounter binary operator with precedence lower than passed.
Thus, if we pass the lowest precedence available, we will parse the whole expression.
Note (Nilpo):
A little bit of a tough thing is multiple value assignment and commas.
There is a lot of weird stuff people may write, and we somehow have to adequately report errors in all of them:
Examples:
- Comma is only for regular lvalue assignment returned from functions:
a: _, b = func_returning_two_values(args);
Not for this:
a, b = b, a;
And also not for this:
a, b += func_returning_two_values(args);
There is also a possibility of assignment in the middle of the expression:
a = b = c;
It gets especially hairy with commas:
a = b, c = d; <- valid C!
So, here's what we do:
TODO: actually write this part.
*/
_ExprParseRes parse_expression_into_atom_arena(NiAtomArena* const atom_arena,
                                               const NiTokenArena* const token_arena,
                                               size_t cursor,
					                                     NiOperatorPrecedence passed_precedence) {
  size_t new_cursor = cursor;
  auto _res = parse_subexpression_into_atom_arena(atom_arena, token_arena, new_cursor);
  new_cursor = _res.first_index_after_expression;
  if (_res.error_kind != _NI_AT_ERR_zero_stub) {
    NiParsingErrorKind error_kind = cursor != new_cursor ? NI_AT_ERR_incomplete_expression : NI_AT_ERR_empty_expression;
    return (_ExprParseRes){new_cursor, error_kind};
  }

  NiToken under_cursor = {0};
  NiAtom next = {0};
  while (true) {
    under_cursor = token_arena->data[new_cursor];
    next = (NiAtom){.position_in_file=under_cursor.position_in_file};

    switch (under_cursor.kind) {
      case NI_TK_add:
        next.binary_op_kind = NI_BinOp_add;
        break;
      case NI_TK_minus:
        next.binary_op_kind = NI_BinOp_sub;
        break;
      case NI_TK_mult:
        next.binary_op_kind = NI_BinOp_mult;
        break;
      case NI_TK_div:
        next.binary_op_kind = NI_BinOp_div;
        break;
      default:
        next.binary_op_kind = _NI_BinOp_zero_stub;
        break;
    }

    if (next.binary_op_kind == _NI_BinOp_zero_stub) {
      // Our job here is done. The thing under cursor is not a binary operator.
      return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
    }

    next.kind = NI_AT_binary_operator;
    NiOperatorPrecedence cursor_precedence = ni_binop_precedence[next.binary_op_kind];

    // Here's the part where operator precedence and associativity are handled.
    // TODO: operator <=> in nilang
    /* Note (Nilpo):
    If we do strict greater here, the operator will be right associative, meaning:
    a + b + c => a + (b + c)
    However, all operators in nilang are left-associative:
    a + b + c => (a + b) + c
    Some are not associative at all, e.g. comparison:
    a == b == c => error, there is no situation where this would be reasonable thing to write without parenthesis.
    */
    if (passed_precedence >= cursor_precedence) {
      return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
    }

    new_cursor += 1;
    // TODO: hazardous precedence
    if (passed_precedence == cursor_precedence) {
    }
    /* if (passed_precedence < cursor_precedence) */ {
      auto _res = parse_expression_into_atom_arena(atom_arena, token_arena, new_cursor, cursor_precedence);
      new_cursor = _res.first_index_after_expression;
      if (_res.error_kind != _NI_AT_ERR_zero_stub) {
        return (_ExprParseRes){new_cursor, _res.error_kind};
      }
      ni_atom_arena_append(atom_arena, &next);
      continue;
    }
  }
}

_ExprParseRes parse_assign_or_expression_statement_into_atom_arena(
  NiAtomArena* const atom_arena,
  const NiTokenArena* const token_arena,
  size_t cursor)
{
  size_t prev_cursor = cursor;
  size_t new_cursor = cursor;
  NiToken under_cursor = token_arena->data[new_cursor];
  NiAtom next = (NiAtom){.position_in_file = under_cursor.position_in_file};

  size_t lhs_expressions_count = 0;
  // Parse left hand side of statement. This loop will only continue if we expect to actually get the expression.
  while (true) {
    // If declarator is under cursor, parse it.
    if (token_arena->data[new_cursor].kind == NI_TK_identifier &&
        token_arena->data[new_cursor + 1].kind == NI_TK_colon) {
      under_cursor = token_arena->data[new_cursor];

      next.position_in_file = under_cursor.position_in_file;
      next.kind = NI_AT_declarator;
      next.identifier_uid = under_cursor.identifier_uid;
      ni_atom_arena_append(atom_arena, &next);
      // TODO: append type uid
      // TODO: if qualifiers are applied to type, issue an error.
      // TODO: actually parse the type lol
      new_cursor += 2;
      if (token_arena->data[new_cursor].kind != NI_TK_comma) {
        break;
      }
      // Skip comma and continue the grind.
      new_cursor += 1;
      prev_cursor = new_cursor;
      continue;
    }
    auto _res = parse_expression_into_atom_arena(atom_arena, token_arena, prev_cursor, NI_PREC_default);
    new_cursor = _res.first_index_after_expression;
    if (_res.error_kind != _NI_AT_ERR_zero_stub) {
      return (_ExprParseRes){new_cursor, _res.error_kind};
    }

    under_cursor = token_arena->data[new_cursor];
    lhs_expressions_count += 1;
    next = (NiAtom){.position_in_file = under_cursor.position_in_file, .kind = NI_AT_expression_end};
    ni_atom_arena_append(atom_arena, &next);

    if (under_cursor.kind != NI_TK_comma) {
      break;
    }
    // Skip comma and continue the grind.
    new_cursor += 1;
    prev_cursor = new_cursor;
  }
  
  under_cursor = token_arena->data[new_cursor];
  switch (under_cursor.kind) {
    case NI_TK_assign:
      // TODO: do kind properly for all assigns.
      next = (NiAtom){.position_in_file = under_cursor.position_in_file, .kind = NI_AT_assignment};
      ni_atom_arena_append(atom_arena, &next);

      // Skip assign operator.
      new_cursor += 1;
      prev_cursor = new_cursor;

      // Parse right hand side.
      auto _res = parse_expression_into_atom_arena(atom_arena, token_arena, prev_cursor, NI_PREC_default);
      new_cursor = _res.first_index_after_expression;
      if (_res.error_kind != _NI_AT_ERR_zero_stub) {
        return (_ExprParseRes){new_cursor, _res.error_kind};
      }

      under_cursor = token_arena->data[new_cursor];
      switch (under_cursor.kind) {
        case NI_TK_assign:
          return (_ExprParseRes){new_cursor, NI_AT_ERR_c_style_assign_usage};
        case NI_TK_comma:
          return (_ExprParseRes){new_cursor, NI_AT_ERR_c_style_comma_usage};
        case NI_TK_semicolon:
          new_cursor += 1;
          break;
        default:
          // TODO: user forgot semicolon
          break;
      }
      break;
    case NI_TK_semicolon:
      // Our job here is done. The statement was actually an expression statement.
      // Consume semicolon.
      new_cursor += 1;
      break;
    default:
      // The statement was actually an expression statement, the user just forgot the semicolon.

      // TODO: error
      // next = (NiAtom){.position_in_file=under_cursor.position_in_file, .kind=NI_AT_ERR_forgot_semicolon};
      // ni_atom_arena_append(atom_arena, &next);
      break;
  }
  next.position_in_file = under_cursor.position_in_file;
  next.kind = NI_AT_statement_end;
  ni_atom_arena_append(atom_arena, &next);
  return (_ExprParseRes){new_cursor, _NI_AT_ERR_zero_stub};
}

_ExprParseRes parse_statement_into_atom_arena(NiAtomArena* const atom_arena,
                                              const NiTokenArena* const token_arena,
                                              size_t cursor) {
  size_t prev_cursor = cursor;
  size_t new_cursor = cursor;
  NiToken under_cursor = token_arena->data[new_cursor];
  NiAtom next = (NiAtom){.position_in_file=under_cursor.position_in_file};
  switch (under_cursor.kind) {
    // Parse assign (including declaration) or expression statement.
    default:
      return parse_assign_or_expression_statement_into_atom_arena(atom_arena, token_arena, cursor);
      break;
  }
}

typedef struct {
  GlobalSymbolHashmap global_symbol_hashmap;
  NiTypeInfoArena     typeinfo_arena;
  NiAtomArena         atom_arena;
  NiParsingErrorArena error_arena;
  bool                unrecoverable_errors;
} NiParsingResult;

NiParsingResult nic_parse(const NiMinimalSuccessfulTokenizationResult* tokenization_result) {
  // TODO: "use" keyword in nilang
  // TODO: better heuristics for sizes of arenas
  size_t token_count = tokenization_result->token_arena.count;
  auto res = (NiParsingResult){
    u64_to_u64_hashmap_init(token_count*sizeof(NiAtom), token_count),
    ni_typeinfo_arena_init(token_count*sizeof(NiAtom), token_count),
    ni_atom_arena_init(token_count*sizeof(NiAtom), token_count),
    ni_parsing_error_arena_init(token_count*sizeof(NiAtom), token_count),
    false,
   };
  // TODO: make real hashmap init please for the love of god
  u64_to_u64_hashmap_alloc(&res.global_symbol_hashmap, token_count);

  if (!nic_parse_types(
    &tokenization_result->token_arena,
    &tokenization_result->typedef_marks_arena,
    &res.global_symbol_hashmap,
    &res.typeinfo_arena,
    &res.error_arena
  )) {
    res.unrecoverable_errors = true;
    return res;
  }
  fprintf(stderr, "Parsed types!\n");

  size_t cursor = 0;
  _ExprParseRes _res = {0};
  while (true) {
    _res = parse_statement_into_atom_arena(&res.atom_arena, &tokenization_result->token_arena, cursor);
    if (_res.error_kind != _NI_AT_ERR_zero_stub) {
      fprintf(stderr, "Error encountered: %d\n", _res.error_kind);
      break;
    }
    cursor = _res.first_index_after_expression;
  }

  return res;
}

static StringView ni_atom_to_debug_sv(const StringArena* const identifier_arena,
                                      StringView const buffer,
                                      StringArena* const target,
				                              const NiAtom* const atom) {
  StringView res = {0};
  // TODO: in nilang, we will have switches on ranges
  switch (atom->kind) {
    case _NI_AT_zero_stub:
      res.count += string_arena_append_sv(target, sv_from_cstr("zero_stub"));
      break;
    case NI_AT_variable: {
      // res.count += string_arena_append_sv(target, sv_from_cstr("variable"));
      IdentifierUID identifier_uid = atom->identifier_uid;
      size_t start = identifier_uid & 0x0000FFFFFFFFFFFF;
      size_t length = identifier_uid >> 48;
      StringView id = sv_slice(*(StringView*)identifier_arena, start, start + length);
      res.count += string_arena_printf(target, "%.*s", (int)id.count, id.data);
      break;
    }
    case NI_AT_numeric_constant: {
      // res.count += string_arena_append_sv(target, sv_from_cstr("numeric_constant"));
      StringView id = sv_slice(buffer, atom->position_in_file, atom->position_in_file + (size_t)atom->literal_length);
      res.count += string_arena_printf(target, "%.*s", (int)id.count, id.data);
      break;
    }
    case NI_AT_unary_operator: {
      res.count += string_arena_append_sv(target, ni_unop_sv[atom->unary_op_kind]);
      break;
    }
    case NI_AT_binary_operator: {
      res.count += string_arena_append_sv(target, ni_binop_sv[atom->binary_op_kind]);
      break;
    }
    case NI_AT_expression_end: {
      res.count += string_arena_append_sv(target, sv_from_cstr("expr_end"));
      break;
    }
    case NI_AT_statement_end: {
      res.count += string_arena_append_sv(target, sv_from_cstr("stmt_end\n"));
      break;
    }
    case NI_AT_declarator: {
      IdentifierUID identifier_uid = atom->identifier_uid;
      size_t start = identifier_uid & 0x0000FFFFFFFFFFFF;
      size_t length = identifier_uid >> 48;
      StringView id = sv_slice(*(StringView*)identifier_arena, start, start + length);
      res.count += string_arena_printf(target, "%.*s:_", (int)id.count, id.data);
      break;
    }
    case NI_AT_assignment: {
      res.count += string_arena_append_sv(target, sv_from_cstr("="));
      break;
    }
    default: {
      res.count += string_arena_append_sv(target, sv_from_cstr("TODO"));
      break;
    }
  }
  res.data = target->data + target->count - res.count;
  return res;
}

void init_tokenizer_library(void) {
  for (NiTokenKind tk_index = _NI_TK_stringifiable_start; tk_index < _NI_TK_stringifiable_end; tk_index += 1) {
    ni_token_sv[tk_index] = sv_from_cstr(ni_token_cstr[tk_index]);
  }
  for (NiUnaryOperatorKind unop_index = _NI_UnOp_zero_stub + 1; unop_index < _NI_UnOp_count; unop_index += 1) {
    ni_unop_sv[unop_index] = sv_from_cstr(ni_unop_cstr[unop_index]);
  }
  for (NiBinaryOperatorKind binop_index = _NI_BinOp_zero_stub + 1; binop_index < _NI_BinOp_count; binop_index += 1) {
    ni_binop_sv[binop_index] = sv_from_cstr(ni_binop_cstr[binop_index]);
  }
}

void init_system_info(void) {
  system_info.memory_page_size_bytes = sysconf(_SC_PAGESIZE);
};

typedef struct {
  uint64_t i;
  uint32_t j;
  uint8_t k;
  uint32_t l;
} WeirdSizeofTest;

/* Arena tests. In nilang, each module will have "executable" section,
 * which library modules like arenas could use for testing.*/
MAKE_TYPED_ARENA_DEFINITION(WeirdSizeofTest, WeirdSizeofTestArena, weird_sizeof_test_arena_init, weird_sizeof_test_arena_alloc, weird_sizeof_test_arena_append);

void weird_sizeof_arena_test() {
  size_t iteration_count = 100000;
  fprintf(stderr, "Weird sizeof: %ld\n", sizeof(WeirdSizeofTest));
  WeirdSizeofTestArena weird_sizeof_arena = weird_sizeof_test_arena_init(16*1024*1024, 3200);
  size_t res_theoretical = 0;
  WeirdSizeofTest elem = {0};
  for (uint64_t i = 0; i < iteration_count; ++i) {
    res_theoretical += (uint64_t)i * 8 + (uint32_t)i * 4 + (uint8_t)i + (uint32_t)i * 4;
    elem = (WeirdSizeofTest){i * 8, i * 4, i, i * 4};
    weird_sizeof_test_arena_append(&weird_sizeof_arena, &elem);
  }
  fprintf(stderr, "Weird sizeof count after appending: %ld\n", weird_sizeof_arena.count);

  size_t res = 0;
  for (size_t i = 0; i < (size_t)weird_sizeof_arena.count; ++i) {
    elem = weird_sizeof_arena.data[i];
    res += elem.i + elem.j + elem.k + elem.l;
  }
  if (res == res_theoretical) {
    fprintf(stderr, "Weird sizeof test passed: res == res_theoretical == %ld\n", res);
  } else {
    fprintf(stderr, "Weird sizeof test failed: res_theoretical == %ld, res == %ld\n", res_theoretical, res);
  }
}

typedef struct {
  uint8_t data[5];
} OddSizeofTest;

MAKE_TYPED_ARENA_DEFINITION(OddSizeofTest, OddSizeofTestArena, odd_sizeof_test_arena_init, odd_sizeof_test_arena_alloc, odd_sizeof_test_arena_append)

void odd_sizeof_arena_test() {
  size_t iteration_count = 100000;
  fprintf(stderr, "Odd sizeof: %ld\n", sizeof(OddSizeofTest));
  OddSizeofTestArena odd_sizeof_arena = odd_sizeof_test_arena_init(16*1024*1024, 3200);
  size_t res_theoretical = 0;
  OddSizeofTest elem = {0};
  for (uint64_t i = 0; i < iteration_count; ++i) {
    res_theoretical += (uint8_t)i * 5;
    elem = (OddSizeofTest){{i, i, i, i, i}};
    odd_sizeof_test_arena_append(&odd_sizeof_arena, &elem);
  }
  fprintf(stderr, "Odd sizeof count after appending: %ld\n", odd_sizeof_arena.count);

  size_t res = 0;
  for (size_t i = 0; i < (size_t)odd_sizeof_arena.count; ++i) {
    elem = odd_sizeof_arena.data[i];
    res += elem.data[0] + elem.data[1] + elem.data[2] + elem.data[3] + elem.data[4];
  }
  if (res == res_theoretical) {
    fprintf(stderr, "Odd sizeof test passed: res == res_theoretical == %ld\n", res);
  } else {
    fprintf(stderr, "Odd sizeof test failed: res_theoretical == %ld, res == %ld\n", res_theoretical, res);
  }
}

/* End of tests. */

int main(void) {
  init_system_info();
  if (false) {
  weird_sizeof_arena_test();
  odd_sizeof_arena_test();
  }
  auto open_res = open_file("tests/some_arithmetic_operators.ni", O_RDONLY);
  if (open_res.status != OpenOpStatus_success) {
    // TODO: string representations of all errors.
    fprintf(stdout, "Something went wrong while trying to open file.\n");
    return open_res.status;
  }
  StringArena arena = string_arena_init(16*1024*1024, 0);
  // TODO: function which does stat automatically
  auto read_res = read_entire_file_descriptor_into_string_arena(open_res.fd, &arena);
  if (read_res != ReadOpStatus_success) {
    fprintf(stdout, "Something went wrong while trying to read file.\n");
    fprintf(stdout, "res: %d.\n", read_res);
    return read_res;
  }
  // TODO: defer
  close(open_res.fd);

  if (false) {
  auto write_res = write_entire_file_descriptor(STDOUT_FILENO, *(StringView*)&arena);
  if (write_res != WriteOpStatus_success) {
    fprintf(stdout, "Something went wrong while trying to write file.\n");
    fprintf(stdout, "res: %d.\n", write_res);
    return write_res;
  }
  }
  fprintf(stderr, "At least read file, arena.count: %ld\n", arena.count);
  // printf("%.*s", (int)arena.count, arena.data);
  init_tokenizer_library();
  fprintf(stderr, "Initialized tokenizer library.\n");
  NiTokenizationResult tokenization_result = nic_tokenize(*(StringView*)&arena);
  fprintf(stderr, "Tokenized. tokenization_result.token_arena.count: %ld\n", tokenization_result.token_arena.count);
  StringArena debug_dump_arena = string_arena_init(16*1024*1024, 1024*1024);
  /*
  for (size_t i = 0; i < res.token_arena.count; ++i) {
    StringView elem_debug_sv = ni_token_to_debug_sv(&res.identifier_arena, *(StringView*)&arena, &debug_dump_arena, &res.token_arena.data[i]);
    fprintf(stderr, "%.*s\n", (int)elem_debug_sv.count, elem_debug_sv.data);
  }
  */

  auto successful_tokenization_result = (NiMinimalSuccessfulTokenizationResult){
    tokenization_result.token_arena,
    tokenization_result.newline_indexes_arena,
    tokenization_result.typedef_marks_arena,
  };

  NiParsingResult parse_result = nic_parse(&successful_tokenization_result);
  // TODO: check stuff
  for (size_t i = 0; i < parse_result.atom_arena.count; ++i) {
    StringView elem_debug_sv = ni_atom_to_debug_sv(&tokenization_result.identifier_arena, *(StringView*)&arena, &debug_dump_arena, &parse_result.atom_arena.data[i]);
    fprintf(stderr, "%.*s ", (int)elem_debug_sv.count, elem_debug_sv.data);
  }
  fprintf(stderr, "\n");
  return 0;
}
