#ifndef ZD_INFLATE_H
#define ZD_INFLATE_H
#include <stdint.h>

enum { INF_OK = 0, INF_EOF = -1, INF_BAD = -2, INF_FULL = -3 };

/* Decode a raw DEFLATE stream (no zlib header) into out[0..out_len). */
int inflate_raw(int (*getbyte)(void *ctx), void *ctx, uint8_t *out, uint32_t out_len, uint32_t *produced);

#endif
