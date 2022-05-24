#include "deflate.h"
#include <string.h> /* memcpy */

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static void deflate_init(
    struct deflate_ctx* const ctx, const size_t total_compressed, const size_t total_uncompressed, void* ctx_cb)
{
    ctx->status = TINFL_STATUS_NEEDS_MORE_INPUT;
    ctx->nout = ctx->uncompressed;
    ctx->remaining_compressed = total_compressed;
    ctx->uncompressed_ready_to_write = 0;
    ctx->uncompressed_ready_wrote = 0;
    ctx->compressed_stream_reader = NULL;
    ctx->uncompressed_stream_writer = NULL;
    ctx->write_compressed = NULL;
    ctx->ctx_cb = ctx_cb;
    ctx->remaining = 0;
    tinfl_init(&ctx->decomp);
}

/* this is an internal function that decompresses as much as possible from the compressed input
 * and if there's any left over it can't decompress it will copy it over an internal buffer */
static int decompress(struct deflate_ctx* const ctx, uint8_t* const compressed, const size_t len, const bool left_overs)
{
    ctx->uncompressed_ready_wrote = 0;
    ctx->remaining = len;
    ctx->compressed = compressed;

    while (ctx->status > TINFL_STATUS_DONE) {
        size_t in_bytes = ctx->remaining;
        size_t out_bytes = ctx->uncompressed + UNCOMPRESSED_BUF_SIZE - ctx->nout;
        int flags = TINFL_FLAG_PARSE_ZLIB_HEADER;

        if (ctx->remaining_compressed > ctx->remaining) {
            flags |= TINFL_FLAG_HAS_MORE_INPUT;
        }

        ctx->status = tinfl_decompress(
            &ctx->decomp, ctx->compressed, &in_bytes, ctx->uncompressed, ctx->nout, &out_bytes, flags);

        ctx->nout += out_bytes;

        ctx->remaining_compressed -= in_bytes;
        ctx->remaining -= in_bytes;
        ctx->compressed += in_bytes;

        if (ctx->status < TINFL_STATUS_DONE || (ctx->status == TINFL_STATUS_DONE && ctx->remaining_compressed > 0)
            || (ctx->status != TINFL_STATUS_DONE && !ctx->remaining_compressed)) {
            return DEFLATE_ERROR;
        }

        const size_t towrite = ctx->nout - ctx->uncompressed;
        if (ctx->status == TINFL_STATUS_DONE || towrite == UNCOMPRESSED_BUF_SIZE) {
            /* we finally have some data: from uncompressed up to towrite */
            if (ctx->uncompressed_stream_writer) {
                /* if we have a write callback provide the data back*/
                const int ret = ctx->uncompressed_stream_writer(ctx->ctx_cb, ctx->uncompressed, towrite);
                if (ret) {
                    /* return any non-zero error code from writer callback */
                    return ret;
                }
                ctx->nout = ctx->uncompressed;
                /* since we have a callback we continue for the next set of data */
                continue;
            }

            /* we don't have a callback keep info about the uncompressed data for later use */
            ctx->uncompressed_ready_to_write = towrite;
            ctx->uncompressed_ready_wrote = 0;

            if (left_overs && ctx->remaining) {
                /* we have data left to process, copy it over before we break*/
                memcpy(ctx->compressed_buf, ctx->compressed, ctx->remaining);
                ctx->compressed = ctx->compressed_buf;
                break;
            }

            if (ctx->remaining && left_overs) {
                /* if we have data left we didn't copy over this is broken */
                return DEFLATE_ERROR;
            }

            break;
        }

        if (ctx->status == TINFL_STATUS_NEEDS_MORE_INPUT) {
            /* We need more input break from decompress */
            break;
        }
    }
    return DEFLATE_OK;
}

static int _decompress(void* const ctx, uint8_t* const compressed, const size_t len)
{
    return decompress((struct deflate_ctx*)ctx, compressed, len, true);
}

int deflate_init_read_uncompressed(struct deflate_ctx* const ctx, const size_t total_compressed, const size_t total_uncompressed,
    int (*reader)(void* ctx), void* ctx_cb)
{
    if (!ctx || !reader || !total_compressed || !total_uncompressed) {
        return DEFLATE_ERROR;
    }
    deflate_init(ctx, total_compressed, total_uncompressed, ctx_cb);
    ctx->compressed_stream_reader = reader;
    ctx->write_compressed = _decompress;
    return DEFLATE_OK;
}

static int write_compressed(struct deflate_ctx* const ctx, uint8_t* const compressed, const size_t len)
{
    if (!ctx || !compressed || !len || ctx->compressed_stream_reader || !ctx->uncompressed || !ctx->nout
        || !ctx->uncompressed_stream_writer) {
        return DEFLATE_ERROR;
    }

    /* return any non-zero error code from the decompress routine */
    const int ret = decompress(ctx, compressed, len, false);
    if (ret) {
        return ret;
    }

    if (ctx->status < TINFL_STATUS_DONE || (ctx->status == TINFL_STATUS_DONE && ctx->remaining_compressed > 0)
        || (ctx->status != TINFL_STATUS_DONE && !ctx->remaining_compressed)) {
        return DEFLATE_ERROR;
    }

    return DEFLATE_OK;
}

static int _write_compressed(void* const ctx, uint8_t* const compressed, const size_t len)
{
    return write_compressed((struct deflate_ctx*)ctx, compressed, len);
}

int deflate_init_write_compressed(struct deflate_ctx* const ctx, const size_t total_compressed, const size_t total_uncompressed,
    int (*writer)(void*, uint8_t* const, size_t), void* const ctx_cb)
{
    if (!ctx || !writer || !total_compressed || !total_uncompressed) {
        return DEFLATE_ERROR;
    }
    deflate_init(ctx, total_compressed, total_uncompressed, ctx_cb);
    ctx->uncompressed_stream_writer = writer;
    ctx->write_compressed = _write_compressed;
    return DEFLATE_OK;
}

int read_uncompressed(struct deflate_ctx* const ctx, uint8_t* const uncompressed, const size_t len)
{

    if (!ctx || !uncompressed || !len || ctx->uncompressed_stream_writer || !ctx->nout
        || !ctx->compressed_stream_reader) {
        return DEFLATE_ERROR;
    }

    size_t remaining = len;
    while (remaining) {
        if (ctx->uncompressed_ready_to_write) {
            const size_t towrite = MIN(ctx->uncompressed_ready_to_write, remaining);
            memcpy(uncompressed + (len - remaining), ctx->uncompressed + ctx->uncompressed_ready_wrote, towrite);
            remaining -= towrite;
            ctx->uncompressed_ready_to_write -= towrite;
            ctx->uncompressed_ready_wrote += towrite;
            ctx->nout = ctx->uncompressed;
        }

        if (!remaining) {
            /* we served all the data requested so return */
            break;
        }

        if (ctx->uncompressed_ready_to_write) {
            /* at this point we don't expect any uncompressed data ready to provide */
            return DEFLATE_ERROR;
        }

        if (ctx->remaining) {
            /* we have left over compressed data to process */
            ctx->nout = ctx->uncompressed;
            const int ret = decompress(ctx, ctx->compressed, ctx->remaining, false);
            if (ret) {
                /* return any non-zero error code from the decompress routine */
                return ret;
            }
        }

        if (ctx->uncompressed_ready_to_write) {
            /* we have data to provide again before we can get new uncompressed data */
            continue;
        }

        /* get more compressed data */
        const int ret = ctx->compressed_stream_reader(ctx->ctx_cb);
        if (ret) {
            /* some failure in decompression or reading decompressed data */
            /* return any non-zero error code from reader callback */
            return ret;
        }
    }
    return DEFLATE_OK;
}
