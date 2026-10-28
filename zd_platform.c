/* Doom on the Zoom MS pedals: doomgeneric platform layer (DG_*), the IWAD in memory,
   and the frame to the 128x64 LCD.

   The IWAD lies at zd_wad (zd_wad_len bytes); Doom reads lumps straight from it
   (wad_file_t.mapped), so no zone memory goes to lump data. Every frame is reduced to
   the LCD format (8 pages x 128 columns, bit 0 on top) in zd_lcd.

   Two builds:
   - emulator runner (default): the runner loads the IWAD at 0xC3000000 and writes
     zd_wad_len, fills the zd_keys ring, and shows zd_lcd at zd_frame_ready();
   - pedal (ZD_PEDAL, Doom CFW): the loader in shared RAM (loader.c) leaves the IWAD
     address and length in its mailbox, keys and encoders are polled from GPIO
     (zd_pedal.c), and the frame goes to the LCD through the OS's L2 driver. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <c6x.h>

#include "doomgeneric.h"
#include "doomkeys.h"
#include "w_file.h"
#include "z_zone.h"
#include "i_video.h"
#include "doomstat.h"

#define CPU_HZ  300000000u

unsigned char *zd_wad = (unsigned char *)0xC3000000;
volatile uint32_t zd_wad_len;                   /* written by the loader */

typedef struct {
    volatile uint32_t head, tail;               /* producer writes head, Doom advances tail */
    volatile uint8_t ev[64][2];                 /* [pressed, doom key] */
} zd_keyring;
zd_keyring zd_keys;

uint8_t zd_lcd[1024];
volatile uint32_t zd_frames;

/* ---------------------------------------------------------------- files */

static int wad_handle;

FILE *zd_fopen(const char *name, const char *mode)
{
    size_t n = strlen(name);
    if (mode[0] == 'r' && zd_wad_len && n >= 4 && !strcmp(name + n - 4, ".wad"))
        return (FILE *)&wad_handle;             /* only M_FileExists asks */
    return NULL;
}

int zd_fclose(FILE *f) { (void)f; return 0; }
int zd_remove(const char *name) { (void)name; return -1; }
int zd_rename(const char *a, const char *b) { (void)a; (void)b; return -1; }
int system(const char *cmd) { (void)cmd; return -1; }     /* i_system's error dialog */

extern wad_file_class_t stdc_wad_file;

static wad_file_t *mem_open(char *path)
{
    wad_file_t *w;
    (void)path;
    if (!zd_wad_len)
        return NULL;
    w = Z_Malloc(sizeof *w, PU_STATIC, 0);
    w->file_class = &stdc_wad_file;
    w->mapped = zd_wad;
    w->length = zd_wad_len;
    return w;
}

static void mem_close(wad_file_t *w) { Z_Free(w); }

static size_t mem_read(wad_file_t *w, unsigned int off, void *buf, size_t len)
{
    if (off >= w->length)
        return 0;
    if (len > w->length - off)
        len = w->length - off;
    memcpy(buf, w->mapped + off, len);
    return len;
}

/* replaces w_file_stdc.c: W_OpenFile always uses this class */
wad_file_class_t stdc_wad_file = { mem_open, mem_close, mem_read };

/* ---------------------------------------------------------------- time */

static uint64_t cycles(void)
{
    uint32_t lo = TSCL;                         /* reading TSCL latches TSCH */
    return ((uint64_t)TSCH << 32) | lo;
}

uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)(cycles() / (CPU_HZ / 1000));
}

#ifdef ZD_PEDAL
void zd_poll_keys(void);                        /* zd_pedal.c */
void zd_lcd_flush(void);
void zd_pedal_init(void);
#else
static void zd_poll_keys(void) { }
#endif

void DG_SleepMs(uint32_t ms)
{
    uint64_t end = cycles() + (uint64_t)ms * (CPU_HZ / 1000);
    while (cycles() < end)
        zd_poll_keys();                         /* encoders need frequent polling */
}

/* ---------------------------------------------------------------- keys */

int DG_GetKey(int *pressed, unsigned char *key)
{
    uint32_t t;
    zd_poll_keys();
    t = zd_keys.tail;
    if (t == zd_keys.head)
        return 0;
    *pressed = zd_keys.ev[t & 63][0];
    *key = zd_keys.ev[t & 63][1];
    zd_keys.tail = t + 1;
    return 1;
}

/* ---------------------------------------------------------------- video */

/* Frame -> 128x64 LCD. LCD rows are 1.4x taller than wide.
   In a level only the 3D view is shown: x 16..303, y 0..167 (288x168, the status bar
   cut off), which has about the LCD's shape; menus and screens use the whole 320x200.
   Steps: palette luminance, box filter to 128x64, unsharp mask, auto levels (3rd/97th
   percentile, smoothed over frames so the picture does not pump), Floyd-Steinberg.
   Prototyped on emulator frames (doom/lcd_variants.png, variant 3). */
static uint16_t luma[256];                      /* 0..255 << 4 */
static int16_t img[64][128];                    /* 12.4 fixed point */
static int lev_lo = 0, lev_hi = 255 << 4;       /* smoothed levels */

static void make_luma(void)
{
    int i;
    for (i = 0; i < 256; i++)
        luma[i] = (77 * colors[i].r + 150 * colors[i].g + 29 * colors[i].b) >> 4;
}

static void downsample(const uint8_t *src, int x0, int y0, int w, int h)
{
    int x, y;
    for (y = 0; y < 64; y++) {
        int sy0 = y0 + y * h / 64, sy1 = y0 + (y + 1) * h / 64;
        for (x = 0; x < 128; x++) {
            int sx0 = x0 + x * w / 128, sx1 = x0 + (x + 1) * w / 128;
            uint32_t sum = 0, n = 0;
            int i, j;
            for (j = sy0; j < sy1; j++)
                for (i = sx0; i < sx1; i++) {
                    sum += luma[src[j * 320 + i]];
                    n++;
                }
            img[y][x] = (int16_t)(sum / n);
        }
    }
}

/* img += 1.5 * (img - blur3x3(img)) */
static void sharpen(void)
{
    static int16_t blur[64][128];
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 128; x++) {
            int s = 0, n = 0, dx, dy;
            for (dy = -1; dy <= 1; dy++)
                for (dx = -1; dx <= 1; dx++) {
                    int yy = y + dy, xx = x + dx;
                    if (yy >= 0 && yy < 64 && xx >= 0 && xx < 128) {
                        s += img[yy][xx];
                        n++;
                    }
                }
            blur[y][x] = (int16_t)(s / n);
        }
    for (y = 0; y < 64; y++)
        for (x = 0; x < 128; x++)
            img[y][x] = (int16_t)(img[y][x] + (3 * (img[y][x] - blur[y][x])) / 2);
}

static void levels(void)
{
    static uint16_t hist[256];
    int x, y, i, c, lo = 0, hi = 255;
    memset(hist, 0, sizeof hist);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 128; x++) {
            int v = img[y][x] >> 4;
            hist[v < 0 ? 0 : v > 255 ? 255 : v]++;
        }
    for (i = 0, c = 0; i < 256; i++)
        if ((c += hist[i]) > 8192 * 3 / 100) { lo = i; break; }
    for (i = 255, c = 0; i >= 0; i--)
        if ((c += hist[i]) > 8192 * 3 / 100) { hi = i; break; }
    if (hi - lo < 24)
        hi = lo + 24;
    lev_lo += ((lo << 4) - lev_lo) / 4;
    lev_hi += ((hi << 4) - lev_hi) / 4;
}

static void dither(void)
{
    static int err[2][130];
    int x, y, range = lev_hi - lev_lo;
    if (range < 16)
        range = 16;
    memset(err, 0, sizeof err);
    memset(zd_lcd, 0, sizeof zd_lcd);
    for (y = 0; y < 64; y++) {
        int *cur = err[y & 1] + 1, *nxt = err[(y + 1) & 1] + 1;
        memset(nxt - 1, 0, sizeof err[0]);
        for (x = 0; x < 128; x++) {
            int v = ((img[y][x] - lev_lo) << 8) / range + cur[x];   /* 0..256 wanted */
            int on = v < 128;                                       /* dark -> ink */
            int e = v - (on ? 0 : 256);
            if (on)
                zd_lcd[(y >> 3) * 128 + x] |= 1 << (y & 7);
            cur[x + 1] += e * 7 / 16;
            nxt[x - 1] += e * 3 / 16;
            nxt[x] += e * 5 / 16;
            nxt[x + 1] += e / 16;
        }
    }
}

/* Automap: thin coloured lines on black. Averaging and dithering leave stray dots, so
   here the screen is light and an LCD pixel gets ink when any source pixel of its block
   is lit (a maximum instead of a mean); the status bar is left out like in the 3D view. */
static void automap_frame(const uint8_t *src)
{
    int x, y;
    memset(zd_lcd, 0, sizeof zd_lcd);
    for (y = 0; y < 64; y++) {
        int sy0 = y * 168 / 64, sy1 = (y + 1) * 168 / 64;
        for (x = 0; x < 128; x++) {
            int sx0 = x * 320 / 128, sx1 = (x + 1) * 320 / 128, i, j, lit = 0;
            for (j = sy0; j < sy1 && !lit; j++)
                for (i = sx0; i < sx1; i++)
                    if (luma[src[j * 320 + i]] > (40 << 4)) { lit = 1; break; }
            if (lit)
                zd_lcd[(y >> 3) * 128 + x] |= 1 << (y & 7);
        }
    }
}

/* host breakpoint; the asm keeps cl6x from dropping the empty call */
#pragma FUNC_CANNOT_INLINE(zd_frame_ready)
void zd_frame_ready(void) { asm(" nop"); }

void DG_DrawFrame(void)
{
    const uint8_t *src = (const uint8_t *)DG_ScreenBuffer;
    make_luma();
    if (gamestate == GS_LEVEL && automapactive) {
        automap_frame(src);
    } else {
        if (gamestate == GS_LEVEL)
            downsample(src, 16, 0, 288, 168);
        else
            downsample(src, 0, 0, 320, 200);
        sharpen();
        levels();
        dither();
    }
    zd_frames++;
#ifdef ZD_PEDAL
    zd_lcd_flush();
#endif
    zd_frame_ready();
}

void DG_Init(void) { TSCL = 0; }
void DG_SetWindowTitle(const char *title) { (void)title; }

int main(void)
{
#ifdef ZD_PEDAL
    /* zone of 2 MB: the rest of the heap holds the screen buffers and small mallocs */
    static char *argv[] = { "doom", "-iwad", "doom1.wad", "-mb", "2", NULL };
    zd_pedal_init();                            /* takes the IWAD from the loader's mailbox */
    doomgeneric_Create(5, argv);
#else
    static char *argv[] = { "doom", "-iwad", "doom1.wad", NULL };
    doomgeneric_Create(3, argv);
#endif
    for (;;)
        doomgeneric_Tick();
}
