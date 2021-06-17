/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <webmaster@flippeh.de> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return. Lukas Niederbremer.
 * -----------------------------------------------------------------------------
 * NBTx modifications by Arnoldo A. Barón.
 * -----------------------------------------------------------------------------
 */
#include "nbtx.h"

#include "buffer.h"
#include "list.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

 /* A special form of memcpy which copies `n' bytes into `dest', then returns
  * `src' + n.
  */
static const void* memscan(void* dest, const void* src, size_t n) {
  memcpy(dest, src, n);
  return (const char*)src + n;
}

#define CHECKED_MALLOC(var, n, on_error) do { \
    if(((var) = malloc(n)) == NULL)             \
    {                                         \
        errno = NBTX_EMEM;                     \
        on_error;                             \
    }                                         \
} while(0)

#define CHECKED_APPEND(b, ptr, len) do { \
    if(buffer_append((b), (ptr), (len))) \
        return NBTX_EMEM;                 \
} while(0)

/* Parses a tag, given a name (may be NULL) and a type. Fills in the payload. */
static nbtx_node* parse_unnamed_tag(nbtx_type type, char* name, const char** memory, size_t* length);

/*
 * Reads some bytes from the memory stream. This macro will read `n'
 * bytes into `dest', call memscan, then fix the length. If anything
 * funky goes down, `on_failure' will be executed.
 */
#define READ_GENERIC(dest, n, on_failure) do { \
    if(*length < (n)) { on_failure; }                   \
    *memory = memscan((dest), *memory, (n));            \
    *length -= (n);                                     \
} while(0)

 /* printfs into the end of a buffer. Note: no null-termination! */
static void bprintf(struct buffer* b, const char* restrict format, ...) {
  va_list args;

  va_start(args, format);
  const int siz = vsnprintf(NULL, 0, format, args);
  va_end(args);

  buffer_reserve(b, b->len + siz + 1);

  va_start(args, format);
  vsnprintf((char*)(b->data + b->len), siz + 1, format, args);
  va_end(args);

  b->len += siz; // remember - no null terminator!
}

/*
 * Reads a string from memory, moving the pointer and updating the length
 * appropriately. Returns NULL on failure.
 */
static char* read_string(const char** memory, size_t* length) {
  int16_t string_length;
  char* ret = NULL;

  READ_GENERIC(&string_length, sizeof string_length, goto parse_error);

  if (string_length < 0)               goto parse_error;
  if (*length < (size_t)string_length) goto parse_error;

  CHECKED_MALLOC(ret, string_length + 1, goto parse_error);

  READ_GENERIC(ret, (size_t)string_length, goto parse_error);

  ret[string_length] = '\0'; /* don't forget to NULL-terminate ;) */
  return ret;

parse_error:
  if (errno == NBTX_OK)
    errno = NBTX_ERR;

  free(ret);
  return NULL;
}

static nbtx_node* parse_named_tag(const char** memory, size_t* length) {
  char* name = NULL;

  uint8_t type;
  READ_GENERIC(&type, sizeof type, goto parse_error);

  name = read_string(memory, length);

  nbtx_node* ret = parse_unnamed_tag((nbtx_type)type, name, memory, length);
  if (ret == NULL) goto parse_error;

  return ret;

parse_error:
  if (errno == NBTX_OK)
    errno = NBTX_ERR;

  free(name);
  return NULL;
}

static struct nbtx_byte_array read_byte_array(const char** memory, size_t* length) {
  struct nbtx_byte_array ret;
  ret.data = NULL;

  READ_GENERIC(&ret.length, sizeof ret.length, goto parse_error);

  if (ret.length < 0) goto parse_error;

  CHECKED_MALLOC(ret.data, ret.length, goto parse_error);

  READ_GENERIC(ret.data, (size_t)ret.length, goto parse_error);

  return ret;

parse_error:
  if (errno == NBTX_OK)
    errno = NBTX_ERR;

  free(ret.data);
  ret.data = NULL;
  return ret;
}

/*
 * Is the list all one type? If yes, return the type. Otherwise, return
 * NBTX_TAG_INVALID
 */
static nbtx_type list_is_homogenous(const struct nbtx_list* list) {
  nbtx_type type = NBTX_TAG_INVALID;

  const struct list_head* pos;
  list_for_each(pos, &list->entry) {
    const struct nbtx_list* cur = list_entry(pos, const struct nbtx_list, entry);

    assert(cur->data);
    assert(cur->data->type != NBTX_TAG_INVALID);

    if (cur->data->type == NBTX_TAG_INVALID)
      return NBTX_TAG_INVALID;

    /* if we're the first type, just set it to our current type */
    if (type == NBTX_TAG_INVALID) type = cur->data->type;

    if (type != cur->data->type)
      return NBTX_TAG_INVALID;
  }

  /* if the list was empty, use the sentinel type */
  if (type == NBTX_TAG_INVALID && list->data != NULL)
    type = list->data->type;

  return type;
}

static struct nbtx_list* read_list(const char** memory, size_t* length) {
  uint8_t type;
  int32_t elems;
  struct nbtx_list* ret;

  CHECKED_MALLOC(ret, sizeof * ret, goto parse_error);

  /* we allocate the data pointer to store the type of the list in the first
   * sentinel element */
  CHECKED_MALLOC(ret->data, sizeof * ret->data, goto parse_error);

  INIT_LIST_HEAD(&ret->entry);

  READ_GENERIC(&type, sizeof type, goto parse_error);
  READ_GENERIC(&elems, sizeof elems, goto parse_error);

  ret->data->type = type == NBTX_TAG_INVALID ? NBTX_TAG_COMPOUND : (nbtx_type)type;

  for (int32_t i = 0; i < elems; i++) {
    struct nbtx_list* new;

    CHECKED_MALLOC(new, sizeof * new, goto parse_error);

    new->data = parse_unnamed_tag((nbtx_type)type, NULL, memory, length);

    if (new->data == NULL) {
      free(new);
      goto parse_error;
    }

    list_add_tail(&new->entry, &ret->entry);
  }

  return ret;

parse_error:
  if (errno == NBTX_OK)
    errno = NBTX_ERR;

  nbtx_free_list(ret);
  return NULL;
}

static struct nbtx_list* read_compound(const char** memory, size_t* length) {
  struct nbtx_list* ret;

  CHECKED_MALLOC(ret, sizeof * ret, goto parse_error);

  ret->data = NULL;
  INIT_LIST_HEAD(&ret->entry);

  for (;;) {
    uint8_t type;
    char* name = NULL;
    struct nbtx_list* new_entry;

    READ_GENERIC(&type, sizeof type, goto parse_error);

    if (type == 0) break; /* TAG_END == 0. We've hit the end of the list when type == TAG_END. */

    name = read_string(memory, length);
    if (name == NULL) goto parse_error;

    CHECKED_MALLOC(new_entry, sizeof * new_entry,
                   free(name);
    goto parse_error;
    );

    new_entry->data = parse_unnamed_tag((nbtx_type)type, name, memory, length);

    if (new_entry->data == NULL) {
      free(new_entry);
      free(name);
      goto parse_error;
    }

    list_add_tail(&new_entry->entry, &ret->entry);
  }

  return ret;

parse_error:
  if (errno == NBTX_OK)
    errno = NBTX_ERR;
  nbtx_free_list(ret);

  return NULL;
}

/*
 * Parses a tag, given a name (may be NULL) and a type. Fills in the payload.
 */
static nbtx_node* parse_unnamed_tag(nbtx_type type, char* name, const char** memory, size_t* length) {
  nbtx_node* node;

  CHECKED_MALLOC(node, sizeof * node, goto parse_error);

  node->type = type;
  node->name = name;

  #define COPY_INTO_PAYLOAD(payload_name) \
    READ_GENERIC(&node->payload.payload_name, sizeof node->payload.payload_name, goto parse_error)

  switch (type) {
    case NBTX_TAG_BYTE:
      COPY_INTO_PAYLOAD(tag_byte);
      break;
    case NBTX_TAG_UNSIGNED_BYTE:
      COPY_INTO_PAYLOAD(tag_ubyte);
      break;
    case NBTX_TAG_SHORT:
      COPY_INTO_PAYLOAD(tag_short);
      break;
    case NBTX_TAG_UNSIGNED_SHORT:
      COPY_INTO_PAYLOAD(tag_ushort);
      break;
    case NBTX_TAG_INT:
      COPY_INTO_PAYLOAD(tag_int);
      break;
    case NBTX_TAG_UNSIGNED_INT:
      COPY_INTO_PAYLOAD(tag_uint);
      break;
    case NBTX_TAG_LONG:
      COPY_INTO_PAYLOAD(tag_long);
      break;
    case NBTX_TAG_UNSIGNED_LONG:
      COPY_INTO_PAYLOAD(tag_ulong);
      break;
    case NBTX_TAG_FLOAT:
      COPY_INTO_PAYLOAD(tag_float);
      break;
    case NBTX_TAG_DOUBLE:
      COPY_INTO_PAYLOAD(tag_double);
      break;
    case NBTX_TAG_BYTE_ARRAY:
      node->payload.tag_byte_array = read_byte_array(memory, length);
      break;
    case NBTX_TAG_STRING:
      node->payload.tag_string = read_string(memory, length);
      break;
    case NBTX_TAG_LIST:
      node->payload.tag_list = read_list(memory, length);
      break;
    case NBTX_TAG_COMPOUND:
      node->payload.tag_compound = read_compound(memory, length);
      break;

    case NBTX_TAG_INVALID:
    default:
      goto parse_error; /* Unknown node or TAG_END. Either way, we shouldn't be parsing this. */
  }

  #undef COPY_INTO_PAYLOAD

  if (errno != NBTX_OK) goto parse_error;

  return node;

parse_error:
  if (errno == NBTX_OK)
    errno = NBTX_ERR;

  free(node);
  return NULL;
}

nbtx_node* nbtx_parse(const void* mem, size_t len) {
  errno = NBTX_OK;

  const char** memory = (const char**)&mem;
  size_t* length = &len;

  return parse_named_tag(memory, length);
}

/* spaces, not tabs ;) */
static void indent(struct buffer* b, size_t amount) {
  size_t spaces = amount * 2; /* 2 spaces per indent */

  char temp[spaces + 1];

  for (size_t i = 0; i < spaces; ++i)
    temp[i] = ' ';
  temp[spaces] = '\0';

  bprintf(b, "%s", temp);
}

static nbtx_status dump_ascii(const nbtx_node*, struct buffer*, size_t ident);

/* prints the node's name, or (null) if it has none. */
#define SAFE_NAME(node) ((node)->name ? (node)->name : "<null>")

static void dump_byte_array(const struct nbtx_byte_array ba, struct buffer* b) {
  assert(ba.length >= 0);

  bprintf(b, "[ ");
  for (int32_t i = 0; i < ba.length; ++i)
    bprintf(b, "%u ", +ba.data[i]);
  bprintf(b, "]");
}

static nbtx_status dump_list_contents_ascii(const struct nbtx_list* list, struct buffer* b, size_t ident) {
  const struct list_head* pos;

  list_for_each(pos, &list->entry) {
    const struct nbtx_list* entry = list_entry(pos, const struct nbtx_list, entry);
    nbtx_status err;

    if ((err = dump_ascii(entry->data, b, ident)) != NBTX_OK)
      return err;
  }

  return NBTX_OK;
}

static nbtx_status dump_ascii(const nbtx_node* tree, struct buffer* b, const size_t ident) {
  if (tree == NULL) return NBTX_OK;

  indent(b, ident);

  if (tree->type == NBTX_TAG_BYTE)
    bprintf(b, "TAG_Byte(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_byte);
  else if (tree->type == NBTX_TAG_UNSIGNED_BYTE)
    bprintf(b, "TAG_UnsignedByte(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_ubyte);
  else if (tree->type == NBTX_TAG_SHORT)
    bprintf(b, "TAG_Short(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_short);
  else if (tree->type == NBTX_TAG_UNSIGNED_SHORT)
    bprintf(b, "TAG_UnsignedShort(\"%s\"): %i\n", SAFE_NAME(tree), (int)tree->payload.tag_ushort);
  else if (tree->type == NBTX_TAG_INT)
    bprintf(b, "TAG_Int(\"%s\"): %i\n", SAFE_NAME(tree), tree->payload.tag_int);
  else if (tree->type == NBTX_TAG_UNSIGNED_INT)
    bprintf(b, "TAG_UnsignedInt(\"%s\"): %u\n", SAFE_NAME(tree), tree->payload.tag_uint);
  else if (tree->type == NBTX_TAG_LONG)
    bprintf(b, "TAG_Long(\"%s\"): %" PRIi64 "\n", SAFE_NAME(tree), tree->payload.tag_long);
  else if (tree->type == NBTX_TAG_UNSIGNED_LONG)
    bprintf(b, "TAG_UnsignedLong(\"%s\"): %" PRIu64 "\n", SAFE_NAME(tree), tree->payload.tag_ulong);
  else if (tree->type == NBTX_TAG_FLOAT)
    bprintf(b, "TAG_Float(\"%s\"): %f\n", SAFE_NAME(tree), (double)tree->payload.tag_float);
  else if (tree->type == NBTX_TAG_DOUBLE)
    bprintf(b, "TAG_Double(\"%s\"): %f\n", SAFE_NAME(tree), tree->payload.tag_double);
  else if (tree->type == NBTX_TAG_BYTE_ARRAY) {
    bprintf(b, "TAG_ByteArray(\"%s\"): ", SAFE_NAME(tree));
    dump_byte_array(tree->payload.tag_byte_array, b);
    bprintf(b, "\n");
  } if (tree->type == NBTX_TAG_STRING) {
    if (tree->payload.tag_string == NULL)
      return NBTX_ERR;

    bprintf(b, "TAG_String(\"%s\"): %s\n", SAFE_NAME(tree), tree->payload.tag_string);
  } else if (tree->type == NBTX_TAG_LIST) {
    bprintf(b, "TAG_List(\"%s\") [%s]\n", SAFE_NAME(tree), nbtx_type_to_string(tree->payload.tag_list->data->type));
    indent(b, ident);
    bprintf(b, "{\n");

    const nbtx_status err = dump_list_contents_ascii(tree->payload.tag_list, b, ident + 1);

    indent(b, ident);
    bprintf(b, "}\n");

    if (err != NBTX_OK)
      return err;
  } else if (tree->type == NBTX_TAG_COMPOUND) {
    bprintf(b, "TAG_Compound(\"%s\")\n", SAFE_NAME(tree));
    indent(b, ident);
    bprintf(b, "{\n");

    const nbtx_status err = dump_list_contents_ascii(tree->payload.tag_compound, b, ident + 1);

    indent(b, ident);
    bprintf(b, "}\n");

    if (err != NBTX_OK)
      return err;
  } else
    return NBTX_ERR;

  return NBTX_OK;
}

char* nbtx_dump_ascii(const nbtx_node* tree) {
  errno = NBTX_OK;

  assert(tree);

  struct buffer b = NBTX_BUFFER_INIT;

  if ((errno = dump_ascii(tree, &b, 0)) != NBTX_OK) goto OOM;
  if (buffer_reserve(&b, b.len + 1))            goto OOM;

  b.data[b.len] = '\0'; /* null-terminate that biatch, since bprintf doesn't
                           do that for us. */

  return (char*)b.data;

OOM:
  if (errno != NBTX_OK)
    errno = NBTX_EMEM;

  buffer_free(&b);
  return NULL;
}

static nbtx_status dump_byte_array_binary(const struct nbtx_byte_array ba, struct buffer* b) {
  int32_t dumped_length = ba.length;

  CHECKED_APPEND(b, &dumped_length, sizeof dumped_length);

  if (ba.length) assert(ba.data);

  CHECKED_APPEND(b, ba.data, ba.length);

  return NBTX_OK;
}

static nbtx_status dump_string_binary(const char* name, struct buffer* b) {
  assert(name);

  const size_t len = strlen(name);

  if (len > 32767 /* SHORT_MAX */)
    return NBTX_ERR;

  { /* dump the length */
    int16_t dumped_len = (int16_t)len;

    CHECKED_APPEND(b, &dumped_len, sizeof dumped_len);
  }

  CHECKED_APPEND(b, name, len);

  return NBTX_OK;
}

static nbtx_status dump_binary_(const nbtx_node*, bool, struct buffer*);

static nbtx_status dump_list_binary(const struct nbtx_list* list, struct buffer* b) {
  const nbtx_type type = list_is_homogenous(list);

  const size_t len = list_length(&list->entry);

  if (len > 2147483647 /* INT_MAX */)
    return NBTX_ERR;

  assert(type != NBTX_TAG_INVALID);

  if (type == NBTX_TAG_INVALID)
    return NBTX_ERR;

  {
    int8_t _type = (int8_t)type;
    CHECKED_APPEND(b, &_type, sizeof _type);
  }

  {
    int32_t dumped_len = (int32_t)len;
    CHECKED_APPEND(b, &dumped_len, sizeof dumped_len);
  }

  const struct list_head* pos;
  list_for_each(pos, &list->entry) {
    const struct nbtx_list* entry = list_entry(pos, const struct nbtx_list, entry);
    nbtx_status ret;

    if ((ret = dump_binary_(entry->data, false, b)) != NBTX_OK)
      return ret;
  }

  return NBTX_OK;
}

static nbtx_status dump_compound_binary(const struct nbtx_list* list, struct buffer* b) {
  const struct list_head* pos;
  list_for_each(pos, &list->entry) {
    const struct nbtx_list* entry = list_entry(pos, const struct nbtx_list, entry);
    nbtx_status ret;

    if ((ret = dump_binary_(entry->data, true, b)) != NBTX_OK)
      return ret;
  }

  /* write out TAG_End */
  uint8_t zero = 0;
  CHECKED_APPEND(b, &zero, sizeof zero);

  return NBTX_OK;
}

/*
 * @param dump_type   Should we dump the type, or just skip it? We need to skip
 *                    when dumping lists, because the list header already says
 *                    the type.
 */
static nbtx_status dump_binary_(const nbtx_node* tree, bool dump_type, struct buffer* b) {
  if (dump_type) { /* write out the type */
    int8_t type = (int8_t)tree->type;

    CHECKED_APPEND(b, &type, sizeof type);
  }

  if (tree->name) {
    nbtx_status err;

    if ((err = dump_string_binary(tree->name, b)) != NBTX_OK)
      return err;
  }

  #define DUMP_NUM(type, x) do {             \
    type temp = x;                           \
    CHECKED_APPEND(b, &temp, sizeof temp);   \
} while(0)

  if (tree->type == NBTX_TAG_BYTE)
    DUMP_NUM(int8_t, tree->payload.tag_byte);
  if (tree->type == NBTX_TAG_UNSIGNED_BYTE)
    DUMP_NUM(uint8_t, tree->payload.tag_ubyte);
  else if (tree->type == NBTX_TAG_SHORT)
    DUMP_NUM(int16_t, tree->payload.tag_short);
  else if (tree->type == NBTX_TAG_UNSIGNED_SHORT)
    DUMP_NUM(uint16_t, tree->payload.tag_ushort);
  else if (tree->type == NBTX_TAG_INT)
    DUMP_NUM(int32_t, tree->payload.tag_int);
  else if (tree->type == NBTX_TAG_UNSIGNED_INT)
    DUMP_NUM(uint32_t, tree->payload.tag_uint);
  else if (tree->type == NBTX_TAG_LONG)
    DUMP_NUM(int64_t, tree->payload.tag_long);
  else if (tree->type == NBTX_TAG_UNSIGNED_LONG)
    DUMP_NUM(uint64_t, tree->payload.tag_ulong);
  else if (tree->type == NBTX_TAG_FLOAT)
    DUMP_NUM(float, tree->payload.tag_float);
  else if (tree->type == NBTX_TAG_DOUBLE)
    DUMP_NUM(double, tree->payload.tag_double);
  else if (tree->type == NBTX_TAG_BYTE_ARRAY)
    return dump_byte_array_binary(tree->payload.tag_byte_array, b);
  else if (tree->type == NBTX_TAG_STRING)
    return dump_string_binary(tree->payload.tag_string, b);
  else if (tree->type == NBTX_TAG_LIST)
    return dump_list_binary(tree->payload.tag_list, b);
  else if (tree->type == NBTX_TAG_COMPOUND)
    return dump_compound_binary(tree->payload.tag_compound, b);

  else
    return NBTX_ERR;

  return NBTX_OK;

  #undef DUMP_NUM
}

struct buffer nbtx_dump_binary(const nbtx_node* tree) {
  errno = NBTX_OK;

  if (tree == NULL) return NBTX_BUFFER_INIT;

  struct buffer ret = NBTX_BUFFER_INIT;

  errno = dump_binary_(tree, true, &ret);

  return ret;
}
