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
struct_name (init_function_name)(size_t const reserve_count_bytes, size_t const capacity) {\
  fprintf(stderr, "initial arena allocation\n");\
  struct_name res = {0};\
  /*This is a way to round value to nearest multiple of page size, using the fact that page size is multiple of 2.*/\
  /*Ideally, we will have something like invariants and be able to optimize based on that.*/\
  size_t rounded_reserve_count_bytes = (reserve_count_bytes + system_info.memory_page_size_bytes - 1) & ~(system_info.memory_page_size_bytes - 1);\
  fprintf(stderr, "rounded_reserve_count_bytes: %ld\n", rounded_reserve_count_bytes);\
  res.reserve_count_bytes = rounded_reserve_count_bytes;\
  /* Note: it is important that result of mmap is page-aligned and zeroed. */\
  Byte* virtual_alloc_ptr = mmap(NULL, rounded_reserve_count_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);\
  size_t initial_capacity_bytes = (capacity * sizeof(T) + system_info.memory_page_size_bytes - 1) & ~(system_info.memory_page_size_bytes - 1);\
  fprintf(stderr, "initial_capacity_bytes: %ld\n", initial_capacity_bytes);\
  /* Size value of mprotect needs to be a multiple of page size. */\
  mprotect(virtual_alloc_ptr, initial_capacity_bytes, PROT_READ | PROT_WRITE);\
  res.data = (T*)virtual_alloc_ptr;\
  fprintf(stderr, "initial_capacity: %ld\n", initial_capacity_bytes / sizeof(T));\
  res.capacity = initial_capacity_bytes / sizeof(T);\
  res.count = 0;\
  return res;\
};\
\
/* TODO: separate alloc and append_count */\
T* (alloc_function_name)(struct_name* const target, size_t const count) {\
  size_t new_count = target->count + count;\
  if (new_count >= target->capacity) /* TODO: unlikely */ {\
    fprintf(stderr, "arena expansion\n");\
    /* TODO: expanding by factor of 2 is matematically suboptimal. */\
    size_t new_capacity_bytes = MAX((target->capacity * sizeof(T)) * 2, new_count * sizeof(T));\
    new_capacity_bytes = (new_capacity_bytes + system_info.memory_page_size_bytes - 1) & ~(system_info.memory_page_size_bytes - 1);\
    fprintf(stderr, "new_capacity_bytes: %ld\n", new_capacity_bytes);\
    /* TODO: check if new_capacity_bytes is more than reserve, return OUT OF MEMORY error. */\
    mprotect((char*)target->data, new_capacity_bytes, PROT_READ | PROT_WRITE);\
    target->capacity = new_capacity_bytes / sizeof(T);\
    fprintf(stderr, "new_capacity: %ld\n", target->capacity);\
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
bool u64_to_u64_hashmap_insert_unchecked(U64ToU64Hashmap* const hm, U64ToU64HashmapEntry entry) {
  size_t offset = 0;
  uint64_t key = entry.key;
  uint64_t element_under_cursor = hm->data[key % hm->count].key;
  /*
  Note (Nilpo):
  We do not have additional array of sentinel "is_slot_occupied" values.
  The idea is, if key in slot is equal to zero, then slot is empty.
  This works because we usually store enums or indexes, and we have "zero_stub" in pretty much all enums.
  */
  // TODO: robin hood hashing.
  // TODO: simd.
  while (element_under_cursor != 0 && element_under_cursor != key) {
    ++offset;
    element_under_cursor = hm->data[(key + offset) % hm->count].key;
  }
  hm->data[(key + offset) % hm->count].value = entry.value;
  return element_under_cursor == key;
}

typedef struct {
 bool was_in_hashmap;
 size_t position;
} _HashmapGetRes;

_HashmapGetRes u64_to_u64_hashmap_get(const U64ToU64Hashmap* const hm, uint64_t key) {
  size_t offset = 0;
  uint64_t element_under_cursor = hm->data[key % hm->count].key;
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
    ++offset;
    element_under_cursor = hm->data[(key + offset) % hm->count].key;
  }
  return (_HashmapGetRes){element_under_cursor == key, (key + offset) % hm->count};
}

/*
Note (nilpo):
This one is a little complicated.
First of all, we store all identifiers in a string arena. Each unique identifier is stored only once.
Usually, you would use size_t (== uint64_t on 64-bit systems) to index start of each identifier,
and uint16_t to store their length. Effectively, IdentifierReference == std::pair(size_t, uint16_t).
Now, notice that we don't actually need full 64 bits to index start.
On most systems, only 48 bits in pointers are actually used. On very few systems, 57 bits.
TODO: add source.
48 bits are enough to index 2^^48 == 256TB of memory. The limit is practically impossible to reach.
This means, we can use first 16 bits to store the length, meaning IdentifierReference fits into single 64 bit integer:
| length (16 bits) | index of start in arena (48 bits) |.
This is nice, because now we only need to build a hashmap where value stored is single uint64_t.
TODO: pack small strings into 48 bits so we don't need to store them in the arena, saving memory.
*/
typedef uint64_t IdentifierReference;

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

bool identifier_ref_equal(const StringArena* const identifier_arena,
                          IdentifierReference const ref,
                          StringView const key) {
  /*
  Note (Nilpo):
  See explanation on what is happening here at IdentifierReference type description.
  */
  if (key.count != (ref >> 48)) {
    return false;
  }
  return !memcmp(&identifier_arena->data[ref & 0x00FFFFFF], key.data, key.count);
}

MAKE_TYPED_ARENA_DEFINITION(IdentifierReference, IdentifierHashset, identifier_hashset_init, identifier_hashset_alloc, identifier_hashset_append);

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
  uint64_t identifier_ref_under_cursor = hm->data[hash % hm->count];
  // fprintf(stderr, "identifier_ref_under_cursor: %llu\n", identifier_ref_under_cursor);
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
  while (identifier_ref_under_cursor != 0 &&
         !identifier_ref_equal(identifier_arena, identifier_ref_under_cursor, key)) {
    ++offset;
    identifier_ref_under_cursor = hm->data[(hash + offset) % hm->count];
  }
  return (_HashmapGetRes){identifier_ref_under_cursor != 0, (hash + offset) % hm->count};
}

void identifier_hashset_insert_unchecked(const IdentifierHashset* const hm, size_t position, IdentifierReference ref) {
  hm->data[position] = ref;
}

typedef enum {
  _NI_TK_zero_stub,
  // TODO: just bool, Bool8 looks stupid.
  NI_TK_Bool8,
  _NI_TK_stringifiable_start = NI_TK_Bool8,
  // TODO: reorder stuff here so that when we do perfect hashing we don't need a lookup table.
  /* TYPES: */
  NI_TK_Byte,
  // Distinct integers, neither arithmetic nor bit operations are allowed on them.
  // Useful for e.g. linux file descriptors.
  NI_TK_D8,
  NI_TK_D16,
  NI_TK_D32,
  NI_TK_D64,
  // Bit integers, arithmetic is not allowed on them, only bitwise operations and shifts.
  NI_TK_B8,
  NI_TK_B16,
  NI_TK_B32,
  NI_TK_B64,
  // TODO: Do we do 128 bit types?
  // NI_TK_B128,
  // Unsigned integers.
  NI_TK_U8,
  NI_TK_U16,
  NI_TK_U32,
  NI_TK_U64,
  // NI_TK_U128,
  NI_TK_USize,
  // Signed integers.
  NI_TK_S8,
  NI_TK_S16,
  NI_TK_S32,
  NI_TK_S64,
  // NI_TK_S128,
  NI_TK_SSize,
  // Floating point numbers.
  NI_TK_F32,
  NI_TK_F64,
  // TODO: F80? Does anybody actually use it in current day and age?

  // TODO: automatically suggest adding NoReturn in tidy phase.
  NI_TK_NoReturn,

  NI_TK_false,
  NI_TK_true,
  NI_TK_null,

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
  NI_TK_add,                    // aka "*"
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
  _NI_TK_count,
} NiTokenKind;

// TODO: something like @exhaustive
// TODO: something like @don't this do identifier
// TODO: align
static const char* ni_token_cstr[_NI_TK_count] = {
  [NI_TK_Bool8] = "Bool8",
  [NI_TK_Byte] = "Byte",
  [NI_TK_D8] = "D8",
  [NI_TK_D16] = "D16",
  [NI_TK_D32] = "D32",
  [NI_TK_D64] = "D64",
  [NI_TK_B8] = "B8",
  [NI_TK_B16] = "B16",
  [NI_TK_B32] = "B32",
  [NI_TK_B64] = "B64",
  [NI_TK_U8] = "U8",
  [NI_TK_U16] = "U16",
  [NI_TK_U32] = "U32",
  [NI_TK_U64] = "U64",
  [NI_TK_USize] = "USize",
  [NI_TK_S8] = "S8",
  [NI_TK_S16] = "S16",
  [NI_TK_S32] = "S32",
  [NI_TK_S64] = "S64",
  [NI_TK_SSize] = "SSize",
  [NI_TK_F32] = "F32",
  [NI_TK_F64] = "F64",
  [NI_TK_NoReturn] = "NoReturn",
  [NI_TK_false] = "false",
  [NI_TK_true] = "true",
  [NI_TK_null] = "null",
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

/*
// Since each unique identifier is only stored once, we can use this integer as a unique hash for types.
typedef IdentifierReference TypeUID;
*/

typedef struct {
  // TODO: struct of arrays
  size_t position_in_file;
  NiTokenKind kind;
  union {
    uint16_t identifier_length;
    IdentifierReference identifier_ref;
    size_t literal_length;
  };
} NiToken;

MAKE_TYPED_ARENA_DEFINITION(NiToken, NiTokenArena, ni_token_arena_init, ni_token_arena_alloc, ni_token_arena_append);

#if 0
static void consume_newline(CTokenizerState* const state, bool prev_was_cr) {
  ++state->cursor;
  // Dealing with microslop's and apple's bullshit:
  // https://en.wikipedia.org/wiki/Newline#Representation
  if (prev_was_cr && state->cursor < state->buffer.count && state->buffer.data[state->cursor] == '\n') {
    ++state->cursor;
  }
  ++state->current_line;
  state->beginning_of_current_line = state->cursor;
  
  CToken next = generic_token_with_current_state(state, C_TK_newline);
  // c_token_da_append(&state->result, &next);
  c_token_arena_append(&state->result, &next);

  return;
}

static bool try_consuming_newline(CTokenizerState* const state) {
  const char head = state->buffer.data[state->cursor];
  if (head == '\n' || head == '\r') {
    consume_newline(state, head == '\r');
    return true;
  }
  return false;
}
#endif

#if 0
  [NI_TK_bitwise_negate] = "~",
  [NI_TK_bool_negate] = "!",
  [NI_TK_plus] = "+",
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
  [NI_TK_saturated_plus] = "++",
  [NI_TK_saturated_minus] = "--",
  [NI_TK_mult] = "*",
  [NI_TK_saturated_mult] = "**",
  [NI_TK_div] = "/",
  [NI_TK_pymod] = "%",
  // TODO: redo
  [NI_TK_cmod] = "%%",
  [NI_TK_assign] = "=",
  [NI_TK_shift_left_assign] = "<<=",      !!
  [NI_TK_shift_right_assign] = ">>=",
  [NI_TK_bitwise_xor_assign] = "^=",
  [NI_TK_bitwise_or_assign] = "|=",
  [NI_TK_bitwise_and_assign] = "&=",
  [NI_TK_plus_assign] = "+=",
  [NI_TK_saturated_plus_assign] = "++=",
  [NI_TK_minus_assign] = "-=",
  [NI_TK_saturated_minus_assign] = "--=", !!
  [NI_TK_mult_assign] = "*=",
  [NI_TK_saturated_mult_assign] = "**=",
  [NI_TK_div_assign] = "/=",
  [NI_TK_pymod_assign] = "%=",
  // TODO: redo
  [NI_TK_cmod_assign] = "%%=",
  [NI_TK_dot] = ".",
  [NI_TK_comma] = ",",
  [NI_TK_semicolon] = ";",
  [NI_TK_open_parenthesis] = "(",
  [NI_TK_close_parenthesis] = ")",
  [NI_TK_open_curly] = "{",
  [NI_TK_close_curly] = "}",
  [NI_TK_open_bracket] = "[",
  [NI_TK_close_bracket] = "]",
#endif

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
        case 0x3855: return NI_TK_U8;
        default:     return NI_TK_identifier;
      }
    }
    case 3: {
      uint32_t as_U32 = 0;
      memcpy(&as_U32, identifier_sv.data, 3);
      switch (as_U32) {
        case 0x00343655: return NI_TK_U64;
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
    default: return NI_TK_identifier;
  }
}

typedef struct {
  NiTokenArena token_arena;
  NiTokenizationErrorArena error_arena;
  USizeArena newline_indexes_arena;
  /* Note (Nilpo):
  identifier_arena actually stores the strings, identifer_indexes_hashset stores references
  */
  StringArena identifier_arena;
} NiTokenizationResult;

// TODO: do identifier storage with hashmap to save memory and make everybody's life easier
// TODO: store non-semantic tokens (newlines, comments) separately.
NiTokenizationResult nic_tokenize(StringView const buffer) {
  // TODO: use var := ... in nilang;
  // TODO: init error arena
  // TODO: better heuristics for sizes of arenas
  NiTokenArena token_arena = ni_token_arena_init(buffer.count * sizeof(NiToken), buffer.count);
  USizeArena newline_indexes_arena = usize_arena_init(buffer.count * sizeof(size_t), buffer.count);
  StringArena identifier_arena = string_arena_init(buffer.count * sizeof(size_t), buffer.count);
  /* Note (Nilpo):
  This one are for internal needs.
  We first collect the indexes so we can avoid constantly rehashing the identifier hashtable.
  */
  USizeArena identifier_indexes_arena = usize_arena_init(buffer.count * sizeof(size_t), buffer.count);
  size_t cursor = 0;

  NiToken next = {0};
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
          next.kind = classify_identifier(sv_slice(buffer, cursor, cursor + length));
	  if (next.kind == NI_TK_identifier) {
            next.identifier_length = length;
            usize_arena_append(&identifier_indexes_arena, &token_arena.count);
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
  
  // Add each identifier to arena only once and update tokens with "identifier" kind with reference to that arena storage.
  {
    IdentifierHashset identifier_references_hashset = identifier_hashset_init_with_enough_memory_for_reasonably_low_collision_rate(identifier_indexes_arena.count);
    size_t index_in_token_arena = 0;
    NiToken token = {0};
    StringView key = {0};
    size_t index_in_arena = 0;
    IdentifierReference ref = 0;
    for (size_t i = 0; i < identifier_indexes_arena.count; ++i) {
      index_in_token_arena = identifier_indexes_arena.data[i];
      token = token_arena.data[index_in_token_arena];
      key = sv_slice(buffer, token.position_in_file, token.position_in_file + (size_t)token.identifier_length);
      auto _get_res = identifier_hashset_check(&identifier_references_hashset, &identifier_arena, key);
      if (_get_res.was_in_hashmap) {
        token_arena.data[index_in_token_arena].identifier_ref = identifier_references_hashset.data[_get_res.position];
	continue;
      }
      index_in_arena = identifier_arena.count;
      // fprintf(stderr, "inserting %.*s, length: %ld, index_in_arena: %ld\n", (int)key.count, key.data, (uint64_t)token.identifier_length, index_in_arena);
      string_arena_append_sv(&identifier_arena, key);
      ref = (((uint64_t)token.identifier_length) << 48) | index_in_arena;
      // fprintf(stderr, "ref, extracted - length: %ld, start: %ld\n", ref >> 48, ref & 0x00FFFFFF);
      identifier_hashset_insert_unchecked(&identifier_references_hashset, _get_res.position, ref);
      token_arena.data[index_in_token_arena].identifier_ref = ref;
    }
  }

  return (NiTokenizationResult){token_arena, {0}, newline_indexes_arena, identifier_arena};
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
      IdentifierReference ref = token->identifier_ref;
      size_t start = ref & 0x00FFFFFF;
      size_t length = ref >> 48;
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

void init_tokenizer_library(void) {
  for (NiTokenKind tk_index = _NI_TK_stringifiable_start; tk_index < _NI_TK_stringifiable_end; ++tk_index) {
    ni_token_sv[tk_index] = sv_from_cstr(ni_token_cstr[tk_index]);
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
  NiTokenizationResult res = nic_tokenize(*(StringView*)&arena);
  fprintf(stderr, "Tokenized. res.token_arena.count: %ld\n", res.token_arena.count);
  StringArena debug_dump_arena = string_arena_init(16*1024*1024, 1024*1024);
  for (size_t i = 0; i < res.token_arena.count; ++i) {
    StringView elem_debug_sv = ni_token_to_debug_sv(&res.identifier_arena, *(StringView*)&arena, &debug_dump_arena, &res.token_arena.data[i]);
    fprintf(stderr, "%.*s\n", (int)elem_debug_sv.count, elem_debug_sv.data);
  }
  return 0;
}
