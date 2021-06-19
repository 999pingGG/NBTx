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
#ifndef NBTX_NBTX_H_
#define NBTX_NBTX_H_

#ifdef __cplusplus
#define restrict __restrict__
extern "C" {
  #endif

  #include <stdbool.h>
  #include <stddef.h> /* for size_t */
  #include <stdint.h>
  #include <stdio.h>  /* for FILE* */

  #include "buffer.h" /* for struct buffer */
  #include "list.h"   /* For struct list_entry etc. */

  typedef enum {
    NBTX_OK = 0, /* No error. */
    NBTX_ERR = -1, /* Generic error, most likely of the parsing variety. */
    NBTX_EMEM = -2, /* Out of memory. */
    NBTX_EIO = -3, /* IO error. */
    NBTX_EZ = -4  /* Zlib compression/decompression error. */
  } nbtx_status;

  typedef enum {
    NBTX_TAG_INVALID,
    NBTX_TAG_BYTE,
    NBTX_TAG_UNSIGNED_BYTE,
    NBTX_TAG_SHORT,
    NBTX_TAG_UNSIGNED_SHORT,
    NBTX_TAG_INT,
    NBTX_TAG_UNSIGNED_INT,
    NBTX_TAG_LONG,
    NBTX_TAG_UNSIGNED_LONG,
    NBTX_TAG_FLOAT,
    NBTX_TAG_DOUBLE,
    NBTX_TAG_BYTE_ARRAY,
    NBTX_TAG_STRING,
    NBTX_TAG_LIST,
    NBTX_TAG_COMPOUND
  } nbtx_type;

  typedef enum {
    NBTX_STRATEGY_GZIP,   /* Use a gzip header. */
    NBTX_STRATEGY_INFLATE /* Use a zlib header. */
  } nbtx_compression_strategy;

  struct nbtx_node;

  /*
   * Represents a single node in the tree. You should switch on `type' and ONLY
   * access the union member it signifies. tag_compound and tag_list contain
   * recursive nbtx_node entries, so those will have to be switched on too. I
   * recommended being VERY comfortable with recursion before traversing this
   * beast, or at least sticking to the library routines provided.
   */
  typedef struct nbtx_node {
    nbtx_type type;
    char* name; /* This may be NULL. Check your damn pointers. */

    union { /* payload */

        /* tag_end has no payload */
      int8_t  tag_byte;
      uint8_t  tag_ubyte;
      int16_t tag_short;
      uint16_t tag_ushort;
      int32_t tag_int;
      uint32_t tag_uint;
      int64_t tag_long;
      uint64_t tag_ulong;
      float   tag_float;
      double  tag_double;

      struct nbtx_byte_array {
        unsigned char* data;
        uint32_t length;
      } tag_byte_array;

      char* tag_string; /* TODO: technically, this should be a UTF-8 string */

      /*
       * Design addendum: we make tag_list a linked list instead of an array
       * so that nbtx_node can be a true recursive data structure. If we used
       * an array, it would be incorrect to call free() on any element except
       * the first one. By using a linked list, the context of the node is
       * irrelevant. One tradeoff of this design is that we don't get tight
       * list packing when memory is a concern and huge lists are created.
       *
       * For more information on using the linked list, see `list.h'. The API
       * is well documented.
       */
      struct nbtx_list {
        struct nbtx_node* data; /* A single node's data. */
        struct list_head entry;
      } *tag_list,
        * tag_compound;

      /*
       * The primary difference between a tag_list and a tag_compound is the
       * use of the first (sentinel) node.
       *
       * In an nbtx_list, the sentinel node contains a valid data pointer with
       * only the type filled in. This is to deal with empty lists which
       * still posess types. Therefore, the sentinel's data pointer must be
       * deallocated.
       *
       * In the tag_compound, the only use of the sentinel is to get the
       * beginning and end of the doubly linked list. The data pointer is
       * unused and set to NULL.
       */
    } payload;
  } nbtx_node;

  /***** High Level Loading/Saving Functions *****/

/*
 * Loads a NBT tree from a compressed file. The file must have been opened with
 * a mode of "rb". If an error occurs, NULL will be returned and errno will be
 * set to the appropriate nbtx_status. Check your danm pointers.
 */
  nbtx_node* nbtx_parse_file(FILE* fp);

  /*
   * The same as nbtx_parse_file, but opens and closes the file for you.
   */
  nbtx_node* nbtx_parse_path(const char* filename);

  /*
   * Loads a NBT tree from a compressed block of memory (such as a chunk or a
   * pre-loaded level.dat). If an error occurs, NULL will be returned and errno
   * will be set to the appropriate nbtx_status. Check your damn pointers.
   *
   * PROTIP: Memory map each individual region file, then call
   *         nbtx_parse_compressed for chunks as needed.
   */
  nbtx_node* nbtx_parse_compressed(const void* chunk_start, size_t length);

  /*
   * Dumps a tree into a file. Check your damn error codes. This function should
   * return NBTX_OK.
   *
   * @see nbtx_compression_strategy
   */
  nbtx_status nbtx_dump_file(const nbtx_node* tree,
                             FILE* fp, nbtx_compression_strategy);

  /*
   * Dumps a tree into a block of memory. If an error occurs, a buffer with a NULL
   * `data' pointer will be returned, and errno will be set.
   *
   * 1) Check your damn pointers.
   * 2) Don't forget to free buf->data. Memory leaks are bad, mkay?
   *
   * @see nbtx_compression_strategy
   */
  struct buffer nbtx_dump_compressed(const nbtx_node* tree,
                                     nbtx_compression_strategy);

  /***** Low Level Loading/Saving Functions *****/

/*
 * Loads a NBT tree from memory. The tree MUST NOT be compressed. If an error
 * occurs, NULL will be returned, and errno will be set to the appropriate
 * nbtx_status. Please check your damn pointers.
 */
  nbtx_node* nbtx_parse(const void* memory, size_t length);

  typedef struct nbtx_style {
    enum {
      NBTX_SAME_LINE = 1,
      NBTX_OWN_LINE
    } brace;

    enum {
      NBTX_HEX = 1,
      NBTX_DEC
    } byte_array;

    int spaces;
  } nbtx_style;

  #define NBTX_DEFAULT_STYLE (nbtx_style) { NBTX_SAME_LINE, NBTX_HEX, 2 }

  /*
   * Returns a NULL-terminated string as the ascii representation of the tree. If
   * an error occurs, NULL will be returned and errno will be set.
   *
   * 1) Check your damn pointers.
   * 2) Don't forget to free the returned pointer. Memory leaks are bad, mkay?
   */
  char* nbtx_dump_ascii(const nbtx_node* tree, nbtx_style style);

  /*
   * Returns a buffer representing the uncompressed tree in NBTx official
   * binary format. Trees dumped with this function can be regenerated with
   * nbtx_parse. If an error occurs, a buffer with a NULL `data' pointer will be
   * returned, and errno will be set.
   *
   * 1) Check your damn pointers.
   * 2) Don't forget to free buf->data. Memory leaks are bad, mkay?
   */
  struct buffer nbtx_dump_binary(const nbtx_node* tree);

  /***** Tree Manipulation Functions *****/

/*
 * Clones an existing tree. Returns NULL on memory errors.
 */
  nbtx_node* nbtx_clone(nbtx_node*);

  /*
   * Recursively deallocates a node and all its children. If this is used on a an
   * entire tree, no memory will be leaked.
   */
  void nbtx_free(nbtx_node*);

  /*
   * Recursively frees all the elements of a list, and then frees the list itself.
   */
  void nbtx_free_list(struct nbtx_list*);

  /*
   * A visitor function to traverse the tree. Return true to keep going, false to
   * stop. `aux' is an optional parameter which will be passed to your visitor
   * from the parent function.
   */
  typedef bool (*nbtx_visitor_t)(nbtx_node* node, void* aux);

  /*
   * A function which directs the overall algorithm with its return type.
   * `aux' is an optional parameter which will be passed to your predicate from
   * the parent function.
   */
  typedef bool (*nbtx_predicate_t)(const nbtx_node* node, void* aux);

  /*
   * Traverses the tree until a visitor says stop or all elements are exhausted.
   * Returns false if it was terminated by a visitor, true otherwise. In most
   * cases this can be ignored.
   *
   * TODO: Is there a way to do this without expensive function pointers? Maybe
   * something like list_for_each?
   */
  bool nbtx_map(nbtx_node* tree, nbtx_visitor_t, void* aux);

  /*
   * Returns a new tree, consisting of a copy of all the nodes the predicate
   * returned `true' for. If the new tree is empty, this function will return
   * NULL. If an out of memory error occured, errno will be set to NBTX_EMEM.
   *
   * TODO: What if I want to keep a tree and all of its children? Do I need to
   * augment nbtx_node with parent pointers?
   */
  nbtx_node* nbtx_filter(const nbtx_node* tree, nbtx_predicate_t, void* aux);

  /*
   * The exact same as nbtx_filter, except instead of returning a new tree, the
   * existing tree is modified in place, and then returned for convenience.
   */
  nbtx_node* nbtx_filter_inplace(nbtx_node* tree, nbtx_predicate_t, void* aux);

  /*
   * Returns the first node which causes the predicate to return true. If all
   * nodes are rejected, NULL is returned. If you want to find every instance of
   * something, consider using nbtx_map with a visitor that keeps track.
   *
   * Since const-ing `tree' would require me const-ing the return value, you'll
   * just have to take my word for it that nbtx_find DOES NOT modify the tree.
   * Feel free to cast as necessary.
   */
  nbtx_node* nbtx_find(nbtx_node* tree, nbtx_predicate_t, void* aux);

  /*
   * Returns the first node with the name `name'. If no node with that name is in
   * the tree, returns NULL.
   *
   * If `name' is NULL, this function will find the first unnamed node.
   *
   * Since const-ing `tree' would require me const-ing the return value, you'll
   * just have to take my word for it that nbtx_find DOES NOT modify the tree.
   * Feel free to cast as necessary.
   */
  nbtx_node* nbtx_find_by_name(nbtx_node* tree, const char* name);

  /*
   * Returns the first node with the "path" in the tree of `path'. If no such node
   * exists, returns NULL. If an element has no name, something like:
   *
   * root.subelement..data == "root" -> "subelement" -> "" -> "data"
   *
   * Remember, if multiple elements exist in a sublist which share the same name
   * (including ""), the first one will be chosen.
   */
  nbtx_node* nbtx_find_by_path(nbtx_node* tree, const char* path);

  /* Returns the number of nodes in the tree. */
  size_t nbtx_size(const nbtx_node* tree);

  /*
   * Returns the Nth item of a list
   * Don't use this to iterate through a list, it would be very inefficient
   */
  nbtx_node* nbtx_list_item(nbtx_node* list, int n);

  /*
   * Creates a new, empty TAG_List.
   * If you're adding a list to another list or a compound right away, it's more efficient
   * to call nbtx_put_list() with nbtx_new_tag_list_payload() as parameter and get the
   * nbtx_node* from the nbtx_result returned.
   * Returns NULL on memory errors.
   */
  nbtx_node* nbtx_new_list(const char* name, nbtx_type type);

  /*
   * Creates a new, empty TAG_Compound.
   * If you're adding a compound to a list or another compound right away, it's more efficient
   * to call nbtx_put_compound() with nbtx_new_tag_compound_payload() as parameter and get the
   * nbtx_node* from the nbtx_result returned.
   * Returns NULL on memory errors.
   */
  nbtx_node* nbtx_new_compound(const char* name);

  /*
   * You're supposed to call this function only to get an empty TAG_List payload for
   * nbtx_put_list().
   * If you're manually freeing this nbtx_list*, make sure to call nbtx_free()
   * on the data member and list_del() on the entry member.
   * Returns NULL on memory errors.
   */
  struct nbtx_list* nbtx_new_tag_list_payload(nbtx_type type);

  /*
   * You're supposed to call this function only to get an empty TAG_Compound payload for
   * nbtx_put_compound().
   * If you're manually freeing this nbtx_list*, make sure to call nbtx_free()
   * on the data member and list_del() on the entry member.
   * Returns NULL on memory errors.
   */
  struct nbtx_list* nbtx_new_tag_compound_payload(void);

  /*
   * If you want to put an existing nbtx_node* of type TAG_List into another list or
   * compound, call this to get it's payload and free the node that used to contain it.
   */
  struct nbtx_list* nbtx_extract_tag_list_payload(nbtx_node* list);

  /*
   * If you want to put an existing nbtx_node* of type TAG_Compound into another compound
   * or list, call this to get it's payload and free the node that used to contain it.
   */
  struct nbtx_list* nbtx_extract_tag_compound_payload(nbtx_node* compound);

  typedef struct {
    nbtx_node* reference; // The node that was just added or modified (NULL on error).
    bool inserted; // False if the item by that name already existed. Only meaningful if reference is not null.
  } nbtx_result;

  /*
   * Those functions set (to a TAG_Compound) or append (to a TAG_List) an item.
   * For a compound, if the tag already exists with the same or another type,
   * it gets replaced.
   * For a list, the name parameter is ignored.
   */
  #define NBTX_SPAWN_PUT_FUNCTION_DECLARATION(c_type, datatype, ...) \
    nbtx_result nbtx_put_##datatype(nbtx_node* list_or_compound, const char* name, c_type tag_##datatype __VA_ARGS__)

  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(int8_t, byte);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(uint8_t, ubyte);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(int16_t, short);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(uint16_t, ushort);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(int32_t, int);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(uint32_t, uint);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(int64_t, long);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(uint64_t, ulong);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(float, float);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(double, double);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(unsigned char*, byte_array, , uint32_t length);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(const char*, string);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(struct nbtx_list*, list);
  NBTX_SPAWN_PUT_FUNCTION_DECLARATION(struct nbtx_list*, compound);

  #undef NBTX_SPAWN_PUT_FUNCTION_DECLARATION

  /* TODO: More utilities as requests are made and patches contributed. */

                        /***** Utility Functions *****/

  /* Returns true if the trees are identical. */
  bool nbtx_eq(const nbtx_node* restrict a, const nbtx_node* restrict b);

  /*
   * Converts a type to a print-friendly string. The string is statically
   * allocated, and therefore does not have to be freed by the user.
  */
  const char* nbtx_type_to_string(nbtx_type);

  /*
   * Converts an error code into a print-friendly string. The string is statically
   * allocated, and therefore does not have to be freed by the user.
   */
  const char* nbtx_error_to_string(nbtx_status);

  #ifdef __cplusplus
}
#endif

#endif

