/* Raw DEFLATE (RFC 1951) decoder for the Doom CFW loader: input from a byte callback
   (the flash file reader), output straight into RAM, so the output itself is the window.
   Small and slow-ish on purpose: it runs once, at power-on. No dynamic memory. */
#include <stdint.h>
#include "inflate.h"

typedef struct {
    int (*getbyte)(void *ctx);
    void *ctx;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out, *out_end, *out_start;
    int err;
} state;

static int bits(state *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        int c = s->getbyte(s->ctx);
        if (c < 0) { s->err = INF_EOF; return 0; }
        v |= (uint32_t)c << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1u << need) - 1));
}

#define MAXBITS 15
typedef struct { int16_t count[MAXBITS + 1]; int16_t symbol[288]; } huffman;

static int decode(state *s, const huffman *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len <= MAXBITS; len++) {
        int count;
        code |= bits(s, 1);
        count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (s->err) return -1;
    }
    s->err = INF_BAD;
    return -1;
}

static int construct(huffman *h, const int16_t *length, int n)
{
    int16_t offs[MAXBITS + 1];
    int sym, len, left = 1;
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (sym = 0; sym < n; sym++)
        if (length[sym]) h->symbol[offs[length[sym]]++] = (int16_t)sym;
    return left;
}

static const int16_t lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const int16_t lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const int16_t dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const int16_t dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(state *s, const huffman *lc, const huffman *dc)
{
    for (;;) {
        int sym = decode(s, lc);
        if (s->err) return s->err;
        if (sym < 256) {
            if (s->out >= s->out_end) return INF_FULL;
            *s->out++ = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            int len, dist;
            sym -= 257;
            if (sym >= 29) return INF_BAD;
            len = lbase[sym] + bits(s, lext[sym]);
            sym = decode(s, dc);
            if (sym < 0 || sym >= 30) return INF_BAD;
            dist = dbase[sym] + bits(s, dext[sym]);
            if (s->err) return s->err;
            if (dist > s->out - s->out_start) return INF_BAD;
            if (s->out + len > s->out_end) return INF_FULL;
            while (len--) { *s->out = *(s->out - dist); s->out++; }
        }
    }
}

static int fixed(state *s)
{
    static huffman lc, dc;
    int16_t lengths[288];
    int i;
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    construct(&lc, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    construct(&dc, lengths, 30);
    return codes(s, &lc, &dc);
}

static int dynamic(state *s)
{
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    static huffman lc, dc;
    int16_t lengths[320];
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4, i;
    if (nlen > 286 || ndist > 30) return INF_BAD;
    for (i = 0; i < ncode; i++) lengths[order[i]] = (int16_t)bits(s, 3);
    for (; i < 19; i++) lengths[order[i]] = 0;
    if (construct(&lc, lengths, 19) != 0) return INF_BAD;
    for (i = 0; i < nlen + ndist;) {
        int sym = decode(s, &lc), len, rep;
        if (sym < 0) return INF_BAD;
        if (sym < 16) { lengths[i++] = (int16_t)sym; continue; }
        len = 0;
        if (sym == 16) { if (i == 0) return INF_BAD; len = lengths[i - 1]; rep = 3 + bits(s, 2); }
        else if (sym == 17) rep = 3 + bits(s, 3);
        else rep = 11 + bits(s, 7);
        if (i + rep > nlen + ndist) return INF_BAD;
        while (rep--) lengths[i++] = (int16_t)len;
    }
    if (lengths[256] == 0) return INF_BAD;
    if (construct(&lc, lengths, nlen) < 0) return INF_BAD;
    if (construct(&dc, lengths + nlen, ndist) < 0) return INF_BAD;
    return codes(s, &lc, &dc);
}

int inflate_raw(int (*getbyte)(void *), void *ctx, uint8_t *out, uint32_t out_len, uint32_t *produced)
{
    state s;
    int last, err = 0;
    s.getbyte = getbyte; s.ctx = ctx; s.bitbuf = 0; s.bitcnt = 0;
    s.out = s.out_start = out; s.out_end = out + out_len; s.err = 0;
    do {
        int type;
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (s.err) { err = s.err; break; }
        if (type == 0) {
            int len, nlen;
            s.bitbuf = 0; s.bitcnt = 0;                    /* to a byte boundary */
            len = bits(&s, 16); nlen = bits(&s, 16);
            if (s.err || (len ^ 0xFFFF) != nlen) { err = INF_BAD; break; }
            if (s.out + len > s.out_end) { err = INF_FULL; break; }
            while (len--) *s.out++ = (uint8_t)bits(&s, 8);
            err = s.err;
        } else if (type == 1) err = fixed(&s);
        else if (type == 2) err = dynamic(&s);
        else err = INF_BAD;
    } while (!last && !err);
    *produced = (uint32_t)(s.out - s.out_start);
    return err;
}
