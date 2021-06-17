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
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

 /*
  * zlib resources:
  *
  * http://zlib.net/manual.html
  * http://zlib.net/zlib_how.html
  * http://www.gzip.org/zlib/zlib_faq.html
  */

  /* The number of bytes to process at a time */
#define NBTX_CHUNK_SIZE 4096

/*
 * Reads a whole file into a buffer. Returns a NULL buffer and sets errno on
 * error.
 */
static struct buffer read_file(FILE* fp) {
  struct buffer ret = NBTX_BUFFER_INIT;

  do {
    if (buffer_reserve(&ret, ret.len + NBTX_CHUNK_SIZE))
      return (errno = NBTX_EMEM), buffer_free(&ret), NBTX_BUFFER_INIT;

    size_t bytes_read = fread(ret.data + ret.len, 1, NBTX_CHUNK_SIZE, fp);
    ret.len += bytes_read;

    if (ferror(fp))
      return (errno = NBTX_EIO), buffer_free(&ret), NBTX_BUFFER_INIT;

  } while (!feof(fp));

  return ret;
}

static nbtx_status write_file(FILE* fp, const void* data, const size_t len) {
  const char* cdata = data;
  size_t bytes_left = len;

  do {
    const size_t bytes_written = fwrite(cdata, 1, bytes_left, fp);
    if (ferror(fp)) return NBTX_EIO;

    bytes_left -= bytes_written;
    cdata += bytes_written;

  } while (bytes_left > 0);

  return NBTX_OK;
}

/*
 * Reads in uncompressed data and returns a buffer with the $(strategy)-compressed
 * data within. Returns a NULL buffer on failure, and sets errno appropriately.
 */
static struct buffer nbtx_compress(void* mem,
                                const size_t len,
                                const nbtx_compression_strategy strategy) {
  struct buffer ret = NBTX_BUFFER_INIT;

  errno = NBTX_OK;

  z_stream stream = {
      .zalloc = Z_NULL,
      .zfree = Z_NULL,
      .opaque = Z_NULL,
      .next_in = (void*)mem,
      .avail_in = len
  };

  /* "The default value is 15"... */
  int windowbits = 15;

  /* ..."Add 16 to windowBits to write a simple gzip header and trailer around
   * the compressed data instead of a zlib wrapper." */
  if (strategy == NBTX_STRATEGY_GZIP)
    windowbits += 16;

  if (deflateInit2(&stream,
                   Z_DEFAULT_COMPRESSION,
                   Z_DEFLATED,
                   windowbits,
                   8,
                   Z_DEFAULT_STRATEGY
  ) != Z_OK) {
    errno = NBTX_EZ;
    return NBTX_BUFFER_INIT;
  }

  assert(stream.avail_in == len); /* I'm not sure if zlib will clobber this */

  do {
    if (buffer_reserve(&ret, ret.len + NBTX_CHUNK_SIZE)) {
      errno = NBTX_EMEM;
      goto compression_error;
    }

    stream.next_out = ret.data + ret.len;
    stream.avail_out = NBTX_CHUNK_SIZE;

    if (deflate(&stream, Z_FINISH) == Z_STREAM_ERROR)
      goto compression_error;

    ret.len += NBTX_CHUNK_SIZE - stream.avail_out;

  } while (stream.avail_out == 0);

  (void)deflateEnd(&stream);
  return ret;

compression_error:
  if (errno == NBTX_OK)
    errno = NBTX_EZ;

  (void)deflateEnd(&stream);
  buffer_free(&ret);
  return NBTX_BUFFER_INIT;
}

/*
 * Reads in zlib-compressed data, and returns a buffer with the decompressed
 * data within. Returns a NULL buffer on failure, and sets errno appropriately.
 */
static struct buffer nbtx_decompress(const void* mem, size_t len) {
  struct buffer ret = NBTX_BUFFER_INIT;

  errno = NBTX_OK;

  z_stream stream = {
      .zalloc = Z_NULL,
      .zfree = Z_NULL,
      .opaque = Z_NULL,
      .next_in = (void*)mem,
      .avail_in = len
  };

  /* "Add 32 to windowBits to enable zlib and gzip decoding with automatic
   * header detection" */
  if (inflateInit2(&stream, 15 + 32) != Z_OK) {
    errno = NBTX_EZ;
    return NBTX_BUFFER_INIT;
  }

  int zlib_ret;

  do {
    if (buffer_reserve(&ret, ret.len + NBTX_CHUNK_SIZE)) {
      errno = NBTX_EMEM;
      goto decompression_error;
    }

    stream.avail_out = NBTX_CHUNK_SIZE;
    stream.next_out = (unsigned char*)ret.data + ret.len;

    switch (zlib_ret = inflate(&stream, Z_NO_FLUSH)) {
      case Z_MEM_ERROR:
        errno = NBTX_EMEM;
        /* fall through */

      case Z_DATA_ERROR: case Z_NEED_DICT:
        goto decompression_error;

      default:
        /* update our buffer length to reflect the new data */
        ret.len += NBTX_CHUNK_SIZE - stream.avail_out;
    }

  } while (stream.avail_out == 0);

  /*
   * If we're at the end of the input data, we'd sure as hell be at the end
   * of the zlib stream.
   */
  if (zlib_ret != Z_STREAM_END) goto decompression_error;
  (void)inflateEnd(&stream);

  return ret;

decompression_error:
  if (errno == NBTX_OK)
    errno = NBTX_EZ;

  (void)inflateEnd(&stream);

  buffer_free(&ret);
  return NBTX_BUFFER_INIT;
}

/*
 * No incremental parsing goes on. We just dump the whole compressed file into
 * memory then pass the job off to nbtx_parse_chunk.
 */
nbtx_node* nbtx_parse_file(FILE* fp) {
  errno = NBTX_OK;

  struct buffer compressed = read_file(fp);

  if (compressed.data == NULL)
    return NULL;

  nbtx_node* ret = nbtx_parse_compressed(compressed.data, compressed.len);

  buffer_free(&compressed);
  return ret;
}

nbtx_node* nbtx_parse_path(const char* filename) {
  FILE* fp = fopen(filename, "rb");

  if (fp == NULL) {
    errno = NBTX_EIO;
    return NULL;
  }

  nbtx_node* r = nbtx_parse_file(fp);
  fclose(fp);
  return r;
}

nbtx_node* nbtx_parse_compressed(const void* chunk_start, const size_t length) {
  struct buffer decompressed = nbtx_decompress(chunk_start, length);

  if (decompressed.data == NULL)
    return NULL;

  nbtx_node* ret = nbtx_parse(decompressed.data, decompressed.len);

  buffer_free(&decompressed);
  return ret;
}

/*
 * Once again, all we're doing is handing the actual compression off to
 * nbtx_dump_compressed, then dumping it into the file.
 */
nbtx_status nbtx_dump_file(const nbtx_node* tree, FILE* fp, const nbtx_compression_strategy strategy) {
  struct buffer compressed = nbtx_dump_compressed(tree, strategy);

  if (compressed.data == NULL)
    return (nbtx_status)errno;

  const nbtx_status ret = write_file(fp, compressed.data, compressed.len);

  buffer_free(&compressed);
  return ret;
}

struct buffer nbtx_dump_compressed(const nbtx_node* tree, nbtx_compression_strategy strat) {
  struct buffer uncompressed = nbtx_dump_binary(tree);

  if (uncompressed.data == NULL)
    return NBTX_BUFFER_INIT;

  const struct buffer compressed = nbtx_compress(uncompressed.data, uncompressed.len, strat);

  buffer_free(&uncompressed);
  return compressed;
}
