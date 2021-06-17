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
#include <errno.h>
#include <stdlib.h>
#include <string.h>

 /* strdup isn't standard. GNU extension. */
static inline char* nbtx_strdup(const char* s) {
  char* r = malloc(strlen(s) + 1);
  if (r == NULL) return NULL;

  strcpy(r, s);
  return r;
}

#define CHECKED_MALLOC(var, n, on_error) do { \
    if((var = malloc(n)) == NULL)             \
    {                                         \
        errno = NBTX_EMEM;                     \
        on_error;                             \
    }                                         \
} while(0)

void nbtx_free_list(struct nbtx_list* list) {
  if (!list)
    return;

  struct list_head* current;
  struct list_head* temp;
  list_for_each_safe(current, temp, &list->entry) {
    struct nbtx_list* entry = list_entry(current, struct nbtx_list, entry);

    nbtx_free(entry->data);
    free(entry);
  }

  free(list->data);
  free(list);
}

void nbtx_free(nbtx_node* tree) {
  if (tree == NULL) return;

  if (tree->type == TAG_LIST)
    nbtx_free_list(tree->payload.tag_list);

  else if (tree->type == TAG_COMPOUND)
    nbtx_free_list(tree->payload.tag_compound);

  else if (tree->type == TAG_BYTE_ARRAY)
    free(tree->payload.tag_byte_array.data);

  else if (tree->type == TAG_INT_ARRAY)
    free(tree->payload.tag_int_array.data);

  else if (tree->type == TAG_STRING)
    free(tree->payload.tag_string);

  free(tree->name);
  free(tree);
}

static struct nbtx_list* clone_list(struct nbtx_list* list) {
  /* even empty lists are valid pointers! */
  assert(list);

  struct nbtx_list* ret;
  CHECKED_MALLOC(ret, sizeof * ret, goto clone_error);

  INIT_LIST_HEAD(&ret->entry);

  ret->data = NULL;

  if (list->data != NULL) {
    CHECKED_MALLOC(ret->data, sizeof * ret->data, goto clone_error);
    ret->data->type = list->data->type;
  }

  struct list_head* pos;
  list_for_each(pos, &list->entry) {
    struct nbtx_list* current = list_entry(pos, struct nbtx_list, entry);
    struct nbtx_list* new;

    CHECKED_MALLOC(new, sizeof * new, goto clone_error);

    new->data = nbtx_clone(current->data);

    if (new->data == NULL) {
      free(new);
      goto clone_error;
    }

    list_add_tail(&new->entry, &ret->entry);
  }

  return ret;

clone_error:
  nbtx_free_list(ret);
  return NULL;
}

/* same as strdup, but handles NULL gracefully */
static inline char* safe_strdup(const char* s) {
  return s ? nbtx_strdup(s) : NULL;
}

nbtx_node* nbtx_clone(nbtx_node* tree) {
  if (tree == NULL) return NULL;
  assert(tree->type != TAG_INVALID);

  nbtx_node* ret = NULL;
  CHECKED_MALLOC(ret, sizeof * ret, return NULL);

  ret->type = tree->type;
  ret->name = safe_strdup(tree->name);

  if (tree->name && ret->name == NULL) goto clone_error;

  if (tree->type == TAG_STRING) {
    ret->payload.tag_string = nbtx_strdup(tree->payload.tag_string);
    if (ret->payload.tag_string == NULL) goto clone_error;
  }

  else if (tree->type == TAG_BYTE_ARRAY) {
    unsigned char* newbuf;
    CHECKED_MALLOC(newbuf, tree->payload.tag_byte_array.length, goto clone_error);

    memcpy(newbuf,
           tree->payload.tag_byte_array.data,
           tree->payload.tag_byte_array.length);

    ret->payload.tag_byte_array.data = newbuf;
    ret->payload.tag_byte_array.length = tree->payload.tag_byte_array.length;
  }

  else if (tree->type == TAG_INT_ARRAY) {
    int32_t* newbuf;
    CHECKED_MALLOC(newbuf, tree->payload.tag_int_array.length * sizeof(int32_t), goto clone_error);

    memcpy(newbuf,
           tree->payload.tag_int_array.data,
           tree->payload.tag_int_array.length);

    ret->payload.tag_int_array.data = newbuf;
    ret->payload.tag_int_array.length = tree->payload.tag_int_array.length;
  }

  else if (tree->type == TAG_LIST) {
    ret->payload.tag_list = clone_list(tree->payload.tag_list);
    if (ret->payload.tag_list == NULL) goto clone_error;
  } else if (tree->type == TAG_COMPOUND) {
    ret->payload.tag_compound = clone_list(tree->payload.tag_compound);
    if (ret->payload.tag_compound == NULL) goto clone_error;
  } else {
    ret->payload = tree->payload;
  }

  return ret;

clone_error:
  if (ret) free(ret->name);

  free(ret);
  return NULL;
}

bool nbtx_map(nbtx_node* tree, nbtx_visitor_t v, void* aux) {
  assert(v);

  if (tree == NULL)  return true;
  if (!v(tree, aux)) return false;

  /* And if the item is a list or compound, recurse through each of their elements. */
  if (tree->type == TAG_COMPOUND) {
    struct list_head* pos;

    list_for_each(pos, &tree->payload.tag_compound->entry)
      if (!nbtx_map(list_entry(pos, struct nbtx_list, entry)->data, v, aux))
        return false;
  }

  if (tree->type == TAG_LIST) {
    struct list_head* pos;

    list_for_each(pos, &tree->payload.tag_list->entry)
      if (!nbtx_map(list_entry(pos, struct nbtx_list, entry)->data, v, aux))
        return false;
  }

  return true;
}

/* Only returns NULL on error. An empty list is still a valid pointer */
static struct nbtx_list* filter_list(const struct nbtx_list* list, const nbtx_predicate_t predicate, void* aux) {
  assert(list);

  struct nbtx_list* ret = NULL;
  CHECKED_MALLOC(ret, sizeof * ret, goto filter_error);

  ret->data = NULL;
  INIT_LIST_HEAD(&ret->entry);

  const struct list_head* pos;
  list_for_each(pos, &list->entry) {
    const struct nbtx_list* p = list_entry(pos, struct nbtx_list, entry);

    nbtx_node* new_node = nbtx_filter(p->data, predicate, aux);

    if (errno != NBTX_OK)  goto filter_error;
    if (new_node == NULL) continue;

    struct nbtx_list* new_entry;
    CHECKED_MALLOC(new_entry, sizeof * new_entry, goto filter_error);

    new_entry->data = new_node;
    list_add_tail(&new_entry->entry, &ret->entry);
  }

  return ret;

filter_error:
  if (errno == NBTX_OK)
    errno = NBTX_EMEM;

  nbtx_free_list(ret);
  return NULL;
}

nbtx_node* nbtx_filter(const nbtx_node* tree, nbtx_predicate_t filter, void* aux) {
  assert(filter);

  errno = NBTX_OK;

  if (tree == NULL)       return NULL;
  if (!filter(tree, aux)) return NULL;

  nbtx_node* ret = NULL;
  CHECKED_MALLOC(ret, sizeof * ret, goto filter_error);

  ret->type = tree->type;
  ret->name = safe_strdup(tree->name);

  if (tree->name && ret->name == NULL) goto filter_error;

  if (tree->type == TAG_STRING) {
    ret->payload.tag_string = nbtx_strdup(tree->payload.tag_string);
    if (ret->payload.tag_string == NULL) goto filter_error;
  }

  else if (tree->type == TAG_BYTE_ARRAY) {
    CHECKED_MALLOC(ret->payload.tag_byte_array.data,
                   tree->payload.tag_byte_array.length,
                   goto filter_error);

    memcpy(ret->payload.tag_byte_array.data,
           tree->payload.tag_byte_array.data,
           tree->payload.tag_byte_array.length);

    ret->payload.tag_byte_array.length = tree->payload.tag_byte_array.length;
  }

  else if (tree->type == TAG_INT_ARRAY) {
    CHECKED_MALLOC(ret->payload.tag_int_array.data,
                   tree->payload.tag_int_array.length * sizeof(int32_t),
                   goto filter_error);

    memcpy(ret->payload.tag_int_array.data,
           tree->payload.tag_int_array.data,
           tree->payload.tag_int_array.length);

    ret->payload.tag_int_array.length = tree->payload.tag_int_array.length;
  }

  /* Okay, we want to keep this node, but keep traversing the tree! */
  else if (tree->type == TAG_LIST) {
    ret->payload.tag_list = filter_list(tree->payload.tag_list, filter, aux);
    if (ret->payload.tag_list == NULL) goto filter_error;
  } else if (tree->type == TAG_COMPOUND) {
    ret->payload.tag_compound = filter_list(tree->payload.tag_compound, filter, aux);
    if (ret->payload.tag_compound == NULL) goto filter_error;
  } else {
    ret->payload = tree->payload;
  }

  return ret;

filter_error:
  if (errno == NBTX_OK)
    errno = NBTX_EMEM;

  if (ret) free(ret->name);

  free(ret);
  return NULL;
}

nbtx_node* nbtx_filter_inplace(nbtx_node* tree, nbtx_predicate_t filter, void* aux) {
  assert(filter);

  if (tree == NULL)               return                 NULL;
  if (!filter(tree, aux))         return nbtx_free(tree), NULL;
  if (tree->type != TAG_LIST &&
      tree->type != TAG_COMPOUND) return tree;

  struct list_head* pos;
  struct list_head* n;
  struct nbtx_list* list = tree->type == TAG_LIST ? tree->payload.tag_list : tree->payload.tag_compound;

  list_for_each_safe(pos, n, &list->entry) {
    struct nbtx_list* cur = list_entry(pos, struct nbtx_list, entry);

    cur->data = nbtx_filter_inplace(cur->data, filter, aux);

    if (cur->data == NULL) {
      list_del(pos);
      free(cur);
    }
  }

  return tree;
}

nbtx_node* nbtxfind(nbtx_node* tree, nbtx_predicate_t predicate, void* aux) {
  if (tree == NULL)                  return NULL;
  if (predicate(tree, aux))          return tree;
  if (tree->type != TAG_LIST &&
      tree->type != TAG_COMPOUND)    return NULL;

  struct list_head* pos;
  struct nbtx_list* list = tree->type == TAG_LIST ? tree->payload.tag_list : tree->payload.tag_compound;

  list_for_each(pos, &list->entry) {
    struct nbtx_list* p = list_entry(pos, struct nbtx_list, entry);
    struct nbtx_node* found;

    if ((found = nbtxfind(p->data, predicate, aux)))
      return found;
  }

  return NULL;
}

static bool names_are_equal(const nbtx_node* node, void* vname) {
  const char* name = vname;

  assert(node);

  if (name == NULL && node->name == NULL)
    return true;

  if (name == NULL || node->name == NULL)
    return false;

  return strcmp(node->name, name) == 0;
}

nbtx_node* nbtx_find_by_name(nbtx_node* tree, const char* name) {
  return nbtxfind(tree, &names_are_equal, (void*)name);
}

/*
 * Returns the index of the first occurence of `c' in `s', or the index of the
 * NULL-terminator. Whichever comes first.
 */
static size_t index_of(const char* s, char c) {
  const char* p = s;

  for (; *p; p++)
    if (*p == c)
      return p - s;

  return p - s;
}

/*
 * Pretends that s1 ends after `len' bytes, and does a strcmp.
 */
static int partial_strcmp(const char* s1, size_t len, const char* s2) {
  assert(s1);

  if (s2 == NULL) return len != 0;

  int r;
  if ((r = strncmp(s1, s2, len)) != 0)
    return r;

  /* at this point, the first `len' characters match. Check for NULL. */
  return s2[len] != '\0';
}

/*
 * Format:
 *   current_name.[other shit]
 * OR
 *   current_name'\0'
 *
 * where current_name can be empty.
 */
nbtx_node* nbtx_find_by_path(nbtx_node* tree, const char* path) {
  assert(tree);
  assert(path);

  /* The end of the "current_name" piece. */
  size_t e = index_of(path, '.');

  bool names_match = partial_strcmp(path, e, tree->name) == 0;

  /* Names don't match. These aren't the droids you're looking for. */
  if (!names_match)                                         return NULL;

  /* We're a leaf node, and the names match. Wooo found it. */
  if (path[e] == '\0')                                     return tree;

  /*
   * Initial names match, but the string isn't at the end. We're expecting a
   * list, but haven't hit one.
   */
  if (tree->type != TAG_LIST && tree->type != TAG_COMPOUND) return NULL;

  /* At this point, the inital names match, and we're not at a leaf node. */

  struct list_head* pos;
  struct nbtx_list* list = tree->type == TAG_LIST ? tree->payload.tag_list : tree->payload.tag_compound;
  list_for_each(pos, &list->entry) {
    struct nbtx_list* elem = list_entry(pos, struct nbtx_list, entry);
    nbtx_node* r;

    if ((r = nbtx_find_by_path(elem->data, path + e + 1)) != NULL)
      return r;
  }

  /* Wasn't found in the list (or the current node isn't a list). Give up. */
  return NULL;
}

/* Gets the length of the list, plus the length of all its children. */
static size_t nbtx_full_list_length(struct nbtx_list* list) {
  size_t accum = 0;

  struct list_head* pos;
  list_for_each(pos, &list->entry)
    accum += nbtx_size(list_entry(pos, const struct nbtx_list, entry)->data);

  return accum;
}

size_t nbtx_size(const nbtx_node* tree) {
  if (tree == NULL)
    return 0;

  if (tree->type == TAG_LIST)
    return nbtx_full_list_length(tree->payload.tag_list) + 1;
  if (tree->type == TAG_COMPOUND)
    return nbtx_full_list_length(tree->payload.tag_compound) + 1;

  return 1;
}

nbtx_node* nbtx_list_item(nbtx_node* list, int n) {
  if (list == NULL || (list->type != TAG_LIST && list->type != TAG_COMPOUND))
    return NULL;

  int i = 0;
  const struct list_head* pos;

  list_for_each(pos, &list->payload.tag_list->entry) {
    if (i++ == n)
      return list_entry(pos, struct nbtx_list, entry)->data;
  }

  return NULL;
}
