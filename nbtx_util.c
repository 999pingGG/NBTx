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

#include <assert.h>
#include <string.h>

const char* nbtx_type_to_string(const nbtx_type t) {
  #define DEF_CASE(name) case name: return #name;
  switch (t) {
    case 0: return "TAG_END";
      DEF_CASE(TAG_BYTE)
        DEF_CASE(TAG_SHORT)
        DEF_CASE(TAG_INT)
        DEF_CASE(TAG_LONG)
        DEF_CASE(TAG_FLOAT)
        DEF_CASE(TAG_DOUBLE)
        DEF_CASE(TAG_BYTE_ARRAY)
        DEF_CASE(TAG_STRING)
        DEF_CASE(TAG_LIST)
        DEF_CASE(TAG_COMPOUND)
        DEF_CASE(TAG_INT_ARRAY)
    default:
      return "TAG_UNKNOWN";
  }
  #undef DEF_CASE
}

const char* nbtx_error_to_string(const nbtx_status s) {
  switch (s) {
    case NBTX_OK:
      return "No error.";
    case NBTX_ERR:
      return "NBT tree is corrupt.";
    case NBTX_EMEM:
      return "Out of memory. You should buy some RAM.";
    case NBTX_EIO:
      return "IO Error. Nonexistent/corrupt file?";
    case NBTX_EZ:
      return "Fatal zlib error. Corrupt file?";
    default:
      return "Unknown error.";
  }
}

/* Returns 1 if one is null and the other isn't. */
static int safe_strcmp(const char* a, const char* b) {
  if (a == NULL)
    return b != NULL; /* a is NULL, b is not */

  if (b == NULL) /* b is NULL, a is not */
    return 1;

  return strcmp(a, b);
}

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static bool floats_are_close(const double a, const double b) {
  static const double epsilon = 0.000001;
  return (min(a, b) + epsilon) >= max(a, b);
}

bool nbtx_eq(const nbtx_node* restrict a, const nbtx_node* restrict b) {
  if (a->type != b->type)
    return false;

  if (safe_strcmp(a->name, b->name) != 0)
    return false;

  switch (a->type) {
    case TAG_BYTE:
      return a->payload.tag_byte == b->payload.tag_byte;
    case TAG_SHORT:
      return a->payload.tag_short == b->payload.tag_short;
    case TAG_INT:
      return a->payload.tag_int == b->payload.tag_int;
    case TAG_LONG:
      return a->payload.tag_long == b->payload.tag_long;
    case TAG_FLOAT:
      return floats_are_close((double)a->payload.tag_float, (double)b->payload.tag_float);
    case TAG_DOUBLE:
      return floats_are_close(a->payload.tag_double, b->payload.tag_double);
    case TAG_BYTE_ARRAY:
      if (a->payload.tag_byte_array.length != b->payload.tag_byte_array.length) return false;
      return memcmp(a->payload.tag_byte_array.data,
                    b->payload.tag_byte_array.data,
                    a->payload.tag_byte_array.length) == 0;
    case TAG_INT_ARRAY:
      if (a->payload.tag_int_array.length != b->payload.tag_int_array.length) return false;
      return memcmp(a->payload.tag_int_array.data,
                    b->payload.tag_int_array.data,
                    a->payload.tag_int_array.length) == 0;
    case TAG_STRING:
      return strcmp(a->payload.tag_string, b->payload.tag_string) == 0;
    case TAG_LIST:
    case TAG_COMPOUND:
    {
      struct list_head* ai, * bi;
      struct nbtx_list* alist = a->type == TAG_LIST ? a->payload.tag_list : a->payload.tag_compound;
      struct nbtx_list* blist = b->type == TAG_LIST ? b->payload.tag_list : b->payload.tag_compound;

      for (ai = alist->entry.flink, bi = blist->entry.flink;
           ai != &alist->entry && bi != &blist->entry;
           ai = ai->flink, bi = bi->flink) {
        struct nbtx_list* ae = list_entry(ai, struct nbtx_list, entry);
        struct nbtx_list* be = list_entry(bi, struct nbtx_list, entry);

        if (!nbtx_eq(ae->data, be->data))
          return false;
      }

      /* if there are still elements left in either list... */
      if (ai != &alist->entry || bi != &blist->entry)
        return false;

      return true;
    }

    case TAG_INVALID:
    default: /* wtf invalid type */
      assert(false);
      return false;
  }
}

