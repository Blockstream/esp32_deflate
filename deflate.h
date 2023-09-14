#ifndef DEFLATE_H
#define DEFLATE_H

#include <sdkconfig.h>
#ifndef ESP_PLATFORM
#error "Error, ESP_PLATFORM required"
#endif

#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include <miniz.h> /* TINFL_LZ_DICT_SIZE */
#else
#include <esp32/rom/miniz.h> /* TINFL_LZ_DICT_SIZE */
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CONFIG_DEFLATE_BUF_COMPRESSED_SIZE
#define CONFIG_DEFLATE_BUF_COMPRESSED_SIZE 4096
#endif

#define COMPRESSED_BUF_SIZE CONFIG_DEFLATE_BUF_COMPRESSED_SIZE

#ifndef CONFIG_DEFLATE_BUF_UNCOMPRESSED_SIZE
#define CONFIG_DEFLATE_BUF_UNCOMPRESSED_SIZE TINFL_LZ_DICT_SIZE
#endif

#define UNCOMPRESSED_BUF_SIZE CONFIG_DEFLATE_BUF_UNCOMPRESSED_SIZE

#define DEFLATE_ERROR -1
#define DEFLATE_OK 0

/* Deflate is a small wrapper around miniz that provides a simplified interface */

/* the context is rather large, 50kb +,  it should be allocated on the heap */
struct deflate_ctx {
    uint8_t uncompressed[UNCOMPRESSED_BUF_SIZE]; /* lib set */
    uint8_t compressed_buf[COMPRESSED_BUF_SIZE]; /* lib set */
    tinfl_decompressor decomp; /* lib set*/
    uint8_t* compressed; /* lib set */
    void* ctx_cb; /* user set */
    uint8_t* nout; /* lib set */
    int status; /* lib set */
    size_t remaining; /* lib set */
    size_t remaining_compressed; /* lib set */
    size_t uncompressed_ready_to_write; /* lib set */
    size_t uncompressed_ready_wrote; /* lib set */
    size_t compressed_buf_len; /* lib set */
    /* this function, if set, is called by this lib when it needs to read some compressed data  */
    int (*compressed_stream_reader)(void*); /* uset set */
    /* this function, if set, is called by this lib when it needs to write some uncompressed data */
    int (*uncompressed_stream_writer)(void*, uint8_t* const, size_t); /* user set */
    /* this function gets set by the client-called init function
     * The client should call it to pass more compressed data into the decompressor as required - either
     * directly if using the 'push compressed' pattern, or via the supplied 'reader' callback if using
     * the 'pull uncompressed' pattern. */
    int (*write_compressed)(void*, uint8_t* const, size_t); /* lib set */
};

/* Function to initialise the decompressor for a 'pull uncompressed' pattern - ie. the client can call
 * read_uncompressed() to pull more uncompressed data from the decompressor.
 * The 'reader' function passed is invoked when the existing compressed input has been exhausted and the
 * client is requesting more uncompressed data.  The 'reader' function should call ctx.write_compressed()
 * with more compressed input.
 * This init function has to be called one time before any uncompressed data is requested.*/
int deflate_init_read_uncompressed(struct deflate_ctx* const ctx, size_t total_compressed,
    int (*reader)(void* ctx), void* ctx_cb);

/* Function to initialise the decompressor for a 'push compressed' pattern - ie. the client can call
 * ctx.write_compressed() to explicitly push more compressed data into the decompressor.
 * The 'writer' function passed is invoked to write the uncompressed data, called by the decompressor
 * when sufficient uncompressed output data is available.
 * This init function has to be called one time before any compressed data is fed. */
int deflate_init_write_compressed(struct deflate_ctx* const ctx, size_t total_compressed,
    int (*writer)(void*, uint8_t* const, size_t), void* ctx_cb);

/* Call this function when you want to read some uncompressed data from the decompressor.
 * Only valid if the 'deflate_init_read_uncompressed()' initialisation was previously called.
 * The function blocks until all data is read.
 * You can call this function for one byte at the time if necessary. */
int read_uncompressed(struct deflate_ctx* const ctx, uint8_t* uncompressed, size_t len);

#endif
