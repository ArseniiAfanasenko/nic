#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "string_handling.h"

typedef struct {
  size_t tab_size;
} TokenizerConfig;

const TokenizerConfig default_tokenizer_config = (TokenizerConfig){.tab_size = 2};

// https://en.cppreference.com/c/keyword
typedef enum {
  C_KW_void,
  C_KW_bool,
  C_KW_signed,
  C_KW_unsigned,
  C_KW_char,
  C_KW_short,
  C_KW_int,
  C_KW_long,
  C_KW_float,
  C_KW_double,
  C_KW_BitInt,
  C_KW_Complex,
  C_KW_Imaginary,
  C_KW_Decimal32,
  C_KW_Decimal64,
  C_KW_Decimal128,

  C_KW_false,
  C_KW_true,
  C_KW_nullptr,

  C_KW_struct,
  C_KW_union,
  C_KW_enum,
  C_KW_typedef,

  C_KW_auto,
  C_KW_register,
  C_KW_extern,
  C_KW_thread_local,
  C_KW_static,
  C_KW_inline,

  C_KW_const,
  C_KW_constexpr,
  C_KW_volarile,
  C_KW_restrict,
  C_KW_alignas,
  C_KW_Atomic,

  C_KW_sizeof,
  C_KW_typeof,
  C_KW_typeof_unqual,
  C_KW_alignof,

  C_KW_return,
  C_KW_if,
  C_KW_else,
  C_KW_for,
  C_KW_do,
  C_KW_while,
  C_KW_continue,
  C_KW_break,
  C_KW_switch,
  C_KW_case,
  C_KW_default,
  C_KW_goto,

  C_KW_static_assert,

  // C11, deprecated in C23
  C_KW_Noreturn,

  C_KW_Generic,
  _C_KW_count,
} CKeyword;

static const char* c_keyword_cstr[_C_KW_count] = {
  [C_KW_void] = "void",
  [C_KW_bool] = "bool",
  [C_KW_signed] = "signed",
  [C_KW_unsigned] = "unsigned",
  [C_KW_char] = "char",
  [C_KW_short] = "short",
  [C_KW_int] = "int",
  [C_KW_long] = "long",
  [C_KW_float] = "float",
  [C_KW_double] = "double",
  [C_KW_BitInt] = "_BitInt",
  [C_KW_Complex] = "_Complex",
  [C_KW_Imaginary] = "_Imaginary",
  [C_KW_Decimal32] = "_Decimal32",
  [C_KW_Decimal64] = "_Decimal64",
  [C_KW_Decimal128] = "_Decimal128",

  [C_KW_false] = "false",
  [C_KW_true] = "true",
  [C_KW_nullptr] = "nullptr",

  [C_KW_struct] = "struct",
  [C_KW_union] = "union",
  [C_KW_enum] = "enum",
  [C_KW_typedef] = "typedef",

  [C_KW_auto] = "auto",
  [C_KW_register] = "register",
  [C_KW_extern] = "extern",
  [C_KW_thread_local] = "thread_local",
  [C_KW_static] = "static",
  [C_KW_inline] = "inline",

  [C_KW_const] = "const",
  [C_KW_constexpr] = "constexpr",
  [C_KW_volarile] = "volarile",
  [C_KW_restrict] = "restrict",
  [C_KW_alignas] = "alignas",
  [C_KW_Atomic] = "_Atomic",

  [C_KW_sizeof] = "sizeof",
  [C_KW_typeof] = "typeof",
  [C_KW_typeof_unqual] = "typeof_unqual",
  [C_KW_alignof] = "alignof",

  [C_KW_return] = "return",
  [C_KW_if] = "if",
  [C_KW_else] = "else",
  [C_KW_for] = "for",
  [C_KW_do] = "do",
  [C_KW_while] = "while",
  [C_KW_continue] = "continue",
  [C_KW_break] = "break",
  [C_KW_switch] = "switch",
  [C_KW_case] = "case",
  [C_KW_default] = "default",
  [C_KW_goto] = "goto",

  [C_KW_static_assert] = "static_assert",

  // C11, deprecated in C23
  [C_KW_Noreturn] = "_Noreturn",

  [C_KW_Generic] = "_Generic",
};

StringView c_keyword_sv[_C_KW_count];

// https://en.cppreference.com/c/language/punctuators
typedef enum {
  C_TK_newline,
  C_TK_whitespace,
  C_TK_multiline_comment,
  C_TK_singleline_comment,
  // Note that order here matches greedy lexing, e.g. "[[" is before "[" and so forth.
  _C_TK_punct_start,
  C_TK_attr_start,         // aka "[["
  C_TK_attr_scope,         // aka "::"
  C_TK_attr_end,           // aka "]]"
  C_TK_shift_left_assign,  // aka "<<="
  C_TK_shift_right_assign, // aka ">>="
  C_TK_equal,              // aka "=="
  C_TK_not_equal,          // aka "!="
  C_TK_less_equal,         // aka "<="
  C_TK_greater_equal,      // aka ">="
  C_TK_logic_and,          // aka "&&"
  C_TK_logic_or,           // aka "||"
  C_TK_shift_left,         // aka "<<"
  C_TK_shift_right,        // aka ">>"
  C_TK_incr,               // aka "++"
  C_TK_decr,               // aka "--"
  C_TK_open_paren,
  C_TK_close_paren,
  C_TK_open_curly,
  C_TK_close_curly,
  C_TK_open_bracket,
  C_TK_close_bracket,
  C_TK_semicolon,
  C_TK_colon,
  C_TK_ellipsis,
  C_TK_question,
  C_TK_member,        // aka "."
  // Note that arrow should come before "-" and ">" for greedy lexing.
  C_TK_member_ptr,    // aka "->"
  C_TK_tilde,         // aka "~"
  C_TK_exclamation,   // aka "!"
  C_TK_plus_equal,
  C_TK_minus_equal,
  C_TK_mult_equal,    // aka "*="
  C_TK_div_equal,     // aka "/="
  C_TK_mod_equal,     // aka "%="
  C_TK_bit_xor_equal, // aka "^="
  C_TK_bit_or_equal,  // aka "|="
  C_TK_bit_and_equal, // aka "&="
  C_TK_plus,
  C_TK_minus,
  C_TK_div,           // aka "/"
  C_TK_mod,           // aka "%"
  C_TK_bit_xor,       // aka "^"
  C_TK_bit_or,        // aka "|"
  C_TK_ampersand,     // aka "&"
  C_TK_assign,        // aka "="
  C_TK_less,          // aka "<"
  C_TK_greater,       // aka ">"
  C_TK_comma,         // aka ","
  _C_TK_punct_end,

  C_TK_keyword,
  C_TK_identifier,
  C_TK_eof,
  C_TK_error,
} CTokenKind;

static const char* c_punct_cstr[_C_TK_punct_end] = {
  [C_TK_attr_start]         = "[[",
  [C_TK_attr_scope]         = "::",
  [C_TK_attr_end]           = "]]",
  [C_TK_shift_left_assign]  = "<<=",
  [C_TK_shift_right_assign] = ">>=",
  [C_TK_equal]              = "==",
  [C_TK_not_equal]          = "!=",
  [C_TK_less_equal]         = "<=",
  [C_TK_greater_equal]      = ">=",
  [C_TK_logic_and]          = "&&",
  [C_TK_logic_or]           = "||",
  [C_TK_shift_left]         = "<<",
  [C_TK_shift_right]        = ">>",
  [C_TK_incr]               = "++",
  [C_TK_decr]               = "--",
  [C_TK_open_paren]         = "(",
  [C_TK_close_paren]        = ")",
  [C_TK_open_curly]         = "{",
  [C_TK_close_curly]        = "}",
  [C_TK_open_bracket]       = "[",
  [C_TK_close_bracket]      = "]",
  [C_TK_semicolon]          = ";",
  [C_TK_colon]              = ":",
  [C_TK_ellipsis]           = "...",
  [C_TK_question]           = "?",
  [C_TK_member]             = ".",
  [C_TK_member_ptr]         = "->",
  [C_TK_tilde]              = "~",
  [C_TK_exclamation]        = "!",
  [C_TK_plus_equal]         = "+=",
  [C_TK_minus_equal]        = "-=",
  [C_TK_mult_equal]         = "*=",
  [C_TK_div_equal]          = "/=",
  [C_TK_mod_equal]          = "%=",
  [C_TK_bit_xor_equal]      = "^=",
  [C_TK_bit_or_equal]       = "|=",
  [C_TK_bit_and_equal]      = "&=",
  [C_TK_plus]               = "+",
  [C_TK_minus]              = "-",
  [C_TK_div]                = "/",
  [C_TK_mod]                = "%",
  [C_TK_bit_xor]            = "^",
  [C_TK_bit_or]             = "|",
  [C_TK_ampersand]          = "&",
  [C_TK_assign]             = "=",
  [C_TK_less]               = "<",
  [C_TK_greater]            = ">",
  [C_TK_comma]              = ",",
};

StringView c_punct_sv[_C_TK_punct_end];

typedef enum {
  C_TK_error_unclosed_multiline_comment,
  C_TK_error_encountered_null_term,
  C_TK_error_encountered_unexpected_symbol,
  _C_TK_error_count,
} CTokenError;

static const char* c_token_error_cstr[_C_TK_error_count] = {
  [C_TK_error_unclosed_multiline_comment]    = "Unclosed multiline comment.",
  [C_TK_error_encountered_null_term]         = "Encountered null term. Provided string view should not include it.",
  [C_TK_error_encountered_unexpected_symbol] = "Encountered unexpected symbol.",
};

StringView c_token_error_sv[_C_TK_error_count];

typedef struct {
  // TODO: source file
  StringView source_file_name;
  size_t line;
  size_t position_in_line;
  CTokenKind kind;
  union {
    size_t whitespace;
    CKeyword keyword;
    StringArenaRef identifier;
    StringArenaRef comment;
    CTokenError error;
    // TODO: for unexpected symbol, store byte
  };
} CToken;

typedef struct {
  CToken* data;
  size_t count;
  size_t capacity;
} CTokenDA;

#define c_token_da_ptr_last(target) (target)->data[(target)->count - 1]
#define c_token_da_last(target) (target).data[(target).count - 1]

static void c_token_da_extend(CTokenDA* const target, size_t capacity) {
  target->data = realloc(target->data, capacity * sizeof(target->data[0]));
  target->capacity = capacity;
}

static void c_token_da_append(CTokenDA* const target, const CToken* const elem) {
  if (target->count + 1 >= target->capacity) {
    if (target->capacity == 0) {
      c_token_da_extend(target, 4096);
    } else {
      c_token_da_extend(target, target->capacity * 2);
    };
  };
  target->data[target->count] = *elem;
  ++target->count;
}

/*
void c_token_da_concat_no_overlap(CTokenDA* const restrict dest, const CTokenDA* const restrict src) {
  size_t new_count = dest->count + src->count;
  if (new_count >= dest->capacity) {
    c_token_da_extend(dest, MAX(new_count, dest->capacity * 2));
  };
  memcpy(dest->data, src->data, new_count * sizeof(dest->data[0]));
  dest->count = new_count;
}
*/

typedef struct {
  CTokenDA result;
  StringView buffer;
  size_t current_line;
  size_t beginning_of_current_line;
  // Something like StringView remainding_buffer;
  size_t cursor;
} CTokenizerState;

static CToken generic_token_with_current_state(CTokenizerState* const state, const CTokenKind kind) {
  // TODO: file, line and pos, are we expanding a macro, etc
  return (CToken){.kind = kind};
}

static CToken multiline_comment_token_with_current_state(CTokenizerState* const state, const StringArenaRef comment) {
  // TODO: file, line and pos, are we expanding a macro, etc
  return (CToken){.kind = C_TK_multiline_comment, .comment = comment};
}

static CToken singleline_comment_token_with_current_state(CTokenizerState* const state, const StringArenaRef comment) {
  // TODO: file, line and pos, are we expanding a macro, etc
  return (CToken){.kind = C_TK_singleline_comment, .comment = comment};
}

static CToken whitespace_token_with_current_state(CTokenizerState* const state, const size_t whitespace) {
  return (CToken){.kind = C_TK_whitespace, .whitespace = whitespace};
}

static CToken keyword_token_with_current_state(CTokenizerState* const state, const CKeyword keyword) {
  // TODO: file, line and pos, are we expanding a macro, etc
  return (CToken){.kind = C_TK_keyword, .keyword = keyword};
}


static CToken identifier_token_with_current_state(CTokenizerState* const state, const StringArenaRef identifier) {
  // TODO: file, line and pos, are we expanding a macro, etc
  return (CToken){.kind = C_TK_identifier, .identifier = identifier};
}

static CToken error_token_with_current_state(CTokenizerState* const state, const CTokenError error) {
  // TODO: file, line and pos, are we expanding a macro, etc
  return (CToken){.kind = C_TK_error, .error = error};
}

static void evaluate_preprocessor_directive(CTokenizerState* const state) {
  // TODO: handle directives
}

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
  c_token_da_append(&state->result, &next);

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

static bool try_consuming_singleline_comment(StringArena* const comment_arena, CTokenizerState* const state) {
  if (state->cursor + 1 >= state->buffer.count) {
    return false;
  }
  StringView next2chars = sv_slice(state->buffer, state->cursor, state->cursor + 2);
  if (!sv_equal(next2chars, sv_from_cstr("//"))) {
    return false;
  }
  state->cursor += 2;
  size_t start_cursor = state->cursor;
  bool consumed_newline;
  while (state->cursor < state->buffer.count && !(consumed_newline = try_consuming_newline(state))) {
    ++state->cursor;
  }
  if (comment_arena != NULL) {
    // -(size_t)consumed_newline is so we don't count newline as part of the comment.
    size_t comm_len = string_arena_append_sv(comment_arena, sv_slice(state->buffer, start_cursor, state->cursor - (size_t)consumed_newline));
    StringArenaRef comment = make_string_arena_ref(comment_arena, comm_len);
    CToken next = singleline_comment_token_with_current_state(state, comment);
    c_token_da_append(&state->result, &next);
  } else {
    CToken next = singleline_comment_token_with_current_state(state, (StringArenaRef){0});
    c_token_da_append(&state->result, &next);
  }
  return true;
}
static bool try_consuming_multiline_comment(StringArena* const comment_arena, CTokenizerState* const state) {
  if (state->cursor + 1 >= state->buffer.count) {
    return false;
  }
  StringView next2chars = sv_slice(state->buffer, state->cursor, state->cursor + 2);
  if (!sv_equal(next2chars, sv_from_cstr("/*"))) {
    return false;
  }
  state->cursor += 2;
  size_t start_cursor = state->cursor;
  CToken next = {0};
  while (state->cursor + 1 < state->buffer.count) {
    next2chars = sv_slice(state->buffer, state->cursor, state->cursor + 2);
    if (!sv_equal(next2chars, sv_from_cstr("*/"))) {
      ++state->cursor;
      continue;
    }
    // TODO: this shitshow will be simplified in nilang
    if (comment_arena != NULL) {
      size_t comm_len = string_arena_append_sv(comment_arena, sv_slice(state->buffer, start_cursor, state->cursor));
      StringArenaRef comment = make_string_arena_ref(comment_arena, comm_len);
      next = multiline_comment_token_with_current_state(state, comment);
    } else {
      next = multiline_comment_token_with_current_state(state, (StringArenaRef){0});
    }
    c_token_da_append(&state->result, &next);
    state->cursor += 2;
    return true;
  }
  // Because we have stopped when there was only a single character left.
  ++state->cursor;
  next = error_token_with_current_state(state, C_TK_error_unclosed_multiline_comment);
  c_token_da_append(&state->result, &next);
  // Even though this is an error, we advanced the state, so returning true.
  return true;
}

static void consume_whitespace(CTokenizerState* const state, size_t whitespace) {
  if (state->result.count == 0 || c_token_da_last(state->result).kind != C_TK_whitespace) {
    CToken next = whitespace_token_with_current_state(state, whitespace);
    c_token_da_append(&state->result, &next);
  } else {
    c_token_da_last(state->result).whitespace += whitespace;
  }
  ++state->cursor;
  return;
}

static bool try_consuming_whitespace(const TokenizerConfig* const config, CTokenizerState* const state) {
  if (state->buffer.data[state->cursor] == '\t') {
    consume_whitespace(state, config->tab_size);
    return true;
  } else if (isspace(state->buffer.data[state->cursor])) {
    consume_whitespace(state, 1);
    return true;
  }
  return false;
}

static bool try_consuming_punct(CTokenizerState* const state) {
  StringView punct_sv = {0};
  for (CTokenKind punct_index = _C_TK_punct_start + 1; punct_index < _C_TK_punct_end; ++punct_index) {
    punct_sv = c_punct_sv[punct_index];
    if (state->buffer.count < state->cursor + punct_sv.count) {
      continue;
    } 
    if (sv_equal(sv_slice(state->buffer, state->cursor, state->cursor + punct_sv.count), punct_sv)) {
      CToken next = generic_token_with_current_state(state, punct_index);
      c_token_da_append(&state->result, &next);
      state->cursor += punct_sv.count;
      return true;
    }
  }
  return false;
}

static bool try_consuming_keyword(CTokenizerState* const state) {
  StringView kw_sv = {0};
  for (CKeyword kw_index = 0; kw_index < _C_KW_count; ++kw_index) {
    kw_sv = c_keyword_sv[kw_index];
    if (state->buffer.count < state->cursor + kw_sv.count) {
      continue;
    } 
    if (sv_equal(sv_slice(state->buffer, state->cursor, state->cursor + kw_sv.count), kw_sv)) {
      CToken next = keyword_token_with_current_state(state, kw_index);
      c_token_da_append(&state->result, &next);
      state->cursor += kw_sv.count;
      return true;
    }
    // TODO: check additional spellings like __restrict__
  }
  return false;
}

static bool is_identifier_continuation(const char c) {
  return isalnum(c) || c == '_';
}

// TODO: fully standard-compliant reading of identifiers
// https://en.cppreference.com/c/language/identifier
// TODO: this can be simdified
static bool try_consuming_identifier(StringArena* const identifier_arena, CTokenizerState* const state) {
  char head = state->buffer.data[state->cursor];
  if (!isalpha(head) && head != '_') {
    return false;
  }
  size_t start_cursor = state->cursor;
  while (state->cursor < state->buffer.count && is_identifier_continuation(state->buffer.data[state->cursor])) {
    ++state->cursor;
  }
  size_t id_len = string_arena_append_sv(identifier_arena, sv_slice(state->buffer, start_cursor, state->cursor));
  StringArenaRef identifier = make_string_arena_ref(identifier_arena, id_len);
  CToken next = identifier_token_with_current_state(state, identifier);
  c_token_da_append(&state->result, &next);
  return true;
}

CTokenDA preprocess_and_tokenize(const TokenizerConfig* const config,
		StringArena* const macro_identifier_arena,
		StringArena* const comment_arena,
		StringArena* const identifier_arena,
		StringView buffer) {
  CTokenizerState state = {.buffer = buffer};

  CToken next = {0};
  do {
    // Invariant: there is always at least one symbol under the cursor.
    // If you need more, check the length.
    if (state.cursor >= state.buffer.count) {
      next = generic_token_with_current_state(&state, C_TK_eof);
      c_token_da_append(&state.result, &next);
      continue;
    } else if (state.buffer.data[state.cursor] == '\0') {
      next = error_token_with_current_state(&state, C_TK_error_encountered_null_term);
      c_token_da_append(&state.result, &next);
      continue;
    } else if (state.buffer.data[state.cursor] == '#') {
      // TODO: update all state
      ++state.cursor;
      // TODO: noop for now
      evaluate_preprocessor_directive(&state);
      continue;
    } else if (try_consuming_singleline_comment(comment_arena, &state)) {
      continue;
    } else if (try_consuming_multiline_comment(comment_arena, &state)) {
      continue;
    } else if (try_consuming_newline(&state)) {
      continue;
    } else if (try_consuming_whitespace(config, &state)) {
      continue;
    } else if (try_consuming_punct(&state)) {
      continue;
    // We try to consume keywords first so they do not accidentally get counted as identifiers.
    } else if (try_consuming_keyword(&state)) {
      continue;
    } else if (try_consuming_identifier(identifier_arena, &state)) {
      // TODO: expand identifiers which are preprocessor tokens
      continue;
    };
    next = error_token_with_current_state(&state, C_TK_error_encountered_unexpected_symbol);
    c_token_da_append(&state.result, &next);
    fprintf(stderr, "could not consume anything\n");
    break;
  } while (c_token_da_last(state.result).kind != C_TK_eof && c_token_da_last(state.result).kind != C_TK_error);

  return state.result;
}

static StringView c_token_to_debug_sv(StringArena* const a, const CToken* const token) {
  StringView res = {0};
  switch (token->kind) {
    case C_TK_newline:
      res.count += string_arena_append_sv(a, sv_from_cstr("newline"));
      break;
    case C_TK_multiline_comment:
      res.count += string_arena_append_sv(a, sv_from_cstr("multiline_comment - "));
      if (token->comment.arena != NULL) {
        res.count += string_arena_append_sv(a, string_arena_ref_to_sv(&token->comment));
      }
      break;
    case C_TK_singleline_comment:
      res.count += string_arena_append_sv(a, sv_from_cstr("singleline_comment - "));
      if (token->comment.arena != NULL) {
        res.count += string_arena_append_sv(a, string_arena_ref_to_sv(&token->comment));
      }
      break;
    case C_TK_whitespace:
      res.count += string_arena_printf(a, "whitespace - %ld", token->whitespace);
      break;
    case C_TK_keyword:
      res.count += string_arena_append_sv(a, sv_from_cstr("keyword - "));
      res.count += string_arena_append_sv(a, c_keyword_sv[token->keyword]);
      break;
    case _C_TK_punct_start:
      res.count += string_arena_append_sv(a, sv_from_cstr("error - use of internal punct_start"));
      break;
    case _C_TK_punct_end:
      res.count += string_arena_append_sv(a, sv_from_cstr("error - use of internal punct_end"));
      break;
    case C_TK_attr_start:         [[fallthrough]];
    case C_TK_attr_scope:         [[fallthrough]];
    case C_TK_attr_end:           [[fallthrough]];
    case C_TK_shift_left_assign:  [[fallthrough]];
    case C_TK_shift_right_assign: [[fallthrough]];
    case C_TK_equal:              [[fallthrough]];
    case C_TK_not_equal:          [[fallthrough]];
    case C_TK_less_equal:         [[fallthrough]];
    case C_TK_greater_equal:      [[fallthrough]];
    case C_TK_logic_and:          [[fallthrough]];
    case C_TK_logic_or:           [[fallthrough]];
    case C_TK_shift_left:         [[fallthrough]];
    case C_TK_shift_right:        [[fallthrough]];
    case C_TK_incr:               [[fallthrough]];
    case C_TK_decr:               [[fallthrough]];
    case C_TK_open_paren:         [[fallthrough]];
    case C_TK_close_paren:        [[fallthrough]];
    case C_TK_open_curly:         [[fallthrough]];
    case C_TK_close_curly:        [[fallthrough]];
    case C_TK_open_bracket:       [[fallthrough]];
    case C_TK_close_bracket:      [[fallthrough]];
    case C_TK_semicolon:          [[fallthrough]];
    case C_TK_colon:              [[fallthrough]];
    case C_TK_ellipsis:           [[fallthrough]];
    case C_TK_question:           [[fallthrough]];
    case C_TK_member:             [[fallthrough]];
    case C_TK_member_ptr:         [[fallthrough]];
    case C_TK_tilde:              [[fallthrough]];
    case C_TK_exclamation:        [[fallthrough]];
    case C_TK_plus_equal:         [[fallthrough]];
    case C_TK_minus_equal:        [[fallthrough]];
    case C_TK_mult_equal:         [[fallthrough]];
    case C_TK_div_equal:          [[fallthrough]];
    case C_TK_mod_equal:          [[fallthrough]];
    case C_TK_bit_xor_equal:      [[fallthrough]];
    case C_TK_bit_or_equal:       [[fallthrough]];
    case C_TK_bit_and_equal:      [[fallthrough]];
    case C_TK_plus:               [[fallthrough]];
    case C_TK_minus:              [[fallthrough]];
    case C_TK_div:                [[fallthrough]];
    case C_TK_mod:                [[fallthrough]];
    case C_TK_bit_xor:            [[fallthrough]];
    case C_TK_bit_or:             [[fallthrough]];
    case C_TK_ampersand:          [[fallthrough]];
    case C_TK_assign:             [[fallthrough]];
    case C_TK_less:               [[fallthrough]];
    case C_TK_greater:            [[fallthrough]];
    case C_TK_comma:
      res.count += string_arena_append_sv(a, sv_from_cstr("punctuator - "));
      res.count += string_arena_append_sv(a, c_punct_sv[token->kind]);
      break;
    case C_TK_identifier:
      res.count += string_arena_append_sv(a, sv_from_cstr("identifier - "));
      res.count += string_arena_append_sv(a, string_arena_ref_to_sv(&token->identifier));
      break;
    case C_TK_eof:
      res.count += string_arena_append_sv(a, sv_from_cstr("eof"));
      break;
    case C_TK_error:
      res.count += string_arena_append_sv(a, sv_from_cstr("error - "));
      res.count += string_arena_append_sv(a, c_token_error_sv[token->error]);
      break;
  }
  res.data = a->data + a->count - res.count;
  return res;
}

/*
StringView tokens_to_sv(StringArena* const a, const CTokenDA* const tokens) {
  for (size_t i = 0; i < tokens->count; ++i) {
    string_arena_alloc(a, tokens->data[i]);
    // TODO: take a string view
  };
};
*/

void init_tokenizer_library(void) {
  for (CKeyword kw_index = 0; kw_index < _C_KW_count; ++kw_index) {
    c_keyword_sv[kw_index] = sv_from_cstr(c_keyword_cstr[kw_index]);
  };
  for (CTokenKind punct_index = _C_TK_punct_start + 1; punct_index < _C_TK_punct_end; ++punct_index) {
    c_punct_sv[punct_index] = sv_from_cstr(c_punct_cstr[punct_index]);
  }
  for (CTokenError error_index = 0; error_index < _C_TK_error_count; ++error_index) {
    c_token_error_sv[error_index] = sv_from_cstr(c_token_error_cstr[error_index]);
  }
}

#define TESTTEXT "int restrict ->aboba\n \ngoto b1bab0ba /* ...\n.. , \n{a;  // };*/ }; l33t_h4ker"

int main(void) {
  init_tokenizer_library();
  StringArena macro_identifier_arena = {0};
  StringArena comment_arena          = {0};
  StringArena identifier_arena       = {0};
  StringArena debug_dump_arena       = {0};
  CTokenDA res = preprocess_and_tokenize(&default_tokenizer_config,
                                         &macro_identifier_arena,
                                         &comment_arena,
					 &identifier_arena,
					 sv_from_cstr(TESTTEXT));
  for (size_t i = 0; i < res.count; ++i) {
    StringView elem_debug_sv = c_token_to_debug_sv(&debug_dump_arena, &res.data[i]);
    fprintf(stderr, "%.*s\n", (int)elem_debug_sv.count, elem_debug_sv.data);
  }
  return 0;
}
