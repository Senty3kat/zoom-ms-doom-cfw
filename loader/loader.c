/* Doom CFW loader (shared RAM 0x80000000), MS-70CDR 2.10 OS.

   The OS's boot step calls doom_hook (loader_entry.asm) instead of the boot screen
   function 0xC00CD680. doom_hook shows the boot logo through the original function,
   disables interrupts, moves the stack into shared RAM and calls loader_main, which:
     1. stops what could still write memory behind our back: EDMA3 events, McASP0, the
        USB connection;
     2. finds DOOM.BIN and DOOM.WDZ in the flash file system (0xF0000) over SPI0,
        the way the OS reads flash (fast read 0x0B, SPIDAT1 0x11000000 | byte), after
        switching the muxed flash CS pin back from GPIO (the LCD driver's state) to SPI;
     3. copies the DOOM.BIN segments to their addresses (over the dead OS) and inflates
        DOOM.WDZ to the top of SDRAM;
     4. leaves the IWAD address in the mailbox, cleans the caches and jumps to Doom.
   Nothing here writes flash. On any error the logo stays and a code is shown in the
   top-left corner (see fail()); power off, or hold up+down at power-on to reflash. */
#include <stdint.h>
#include "inflate.h"
#include "../zd_lcd.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define SPIDAT1 REG(0x01C4103C)
#define SPIBUF  REG(0x01C41040)

#define FS_BASE   0x0F0000u
#define BLOCK     4096u
#define SDRAM_END 0xC0800000u

typedef struct { uint32_t magic, wad, wad_len, flags; } zd_mailbox;
#define MAILBOX ((volatile zd_mailbox *)0x8001FFC0)

/* ---------------------------------------------------------------- flash */

static uint8_t xfer(uint32_t w)
{
    while (SPIBUF & (1u << 29))                 /* TXFULL */
        ;
    SPIDAT1 = w;
    while (SPIBUF & 0x80000000u)                /* RXEMPTY */
        ;
    return (uint8_t)SPIBUF;
}

static void flash_read(uint32_t addr, uint8_t *buf, uint32_t n)
{
    uint32_t i;
    if (!n) return;
    xfer(0x11000000u | 0x0B);
    xfer(0x11000000u | ((addr >> 16) & 0xFF));
    xfer(0x11000000u | ((addr >> 8) & 0xFF));
    xfer(0x11000000u | (addr & 0xFF));
    xfer(0x11000000u);                          /* dummy */
    for (i = 0; i + 1 < n; i++)
        buf[i] = xfer(0x11000000u);
    buf[n - 1] = xfer(0x01000000u);             /* last byte releases CS */
}

/* ---------------------------------------------------------------- errors */

/* draw "E" and n bars in the top-left of the screen and stop */
static void fail(int n)
{
    static const uint8_t e[6] = { 0x7F, 0x49, 0x49, 0x49, 0x41, 0x00 };
    uint8_t row[128];
    int x;
    for (x = 0; x < 128; x++)
        row[x] = x < 6 ? e[x] : (x >= 8 && x < 8 + 2 * n && !(x & 1)) ? 0x7F : 0;
    zl_select_lcd();
    zl_page(0, row);
    zl_done();
    for (;;)
        ;
}

/* ---------------------------------------------------------------- file system */

typedef struct { uint16_t block; uint32_t size; } fs_file;

static uint8_t blk[BLOCK];

static int fs_find(const char *name, fs_file *f)
{
    static const uint8_t tables[4] = { 3, 5, 7, 9 };
    uint8_t ent[32];
    int t, i, k;
    for (t = 0; t < 4; t++) {
        uint32_t base = FS_BASE + tables[t] * BLOCK;
        flash_read(base, ent, 8);
        if (ent[1] != 0xA5 || ent[5] != 0xFF || ent[4] != 0xFF)
            continue;                           /* not the live table */
        for (i = 0; i < (int)(2 * BLOCK - 8) / 32; i++) {
            flash_read(base + 8 + 32 * i, ent, 32);
            for (k = 0; name[k] && ent[8 + k] == (uint8_t)name[k]; k++)
                ;
            if (!name[k] && ent[8 + k] == 0) {
                f->block = (uint16_t)(ent[0] | ent[1] << 8);
                f->size = ent[4] | ent[5] << 8 | (uint32_t)ent[6] << 16 | (uint32_t)ent[7] << 24;
                return 1;
            }
        }
    }
    return 0;
}

/* sequential reader over a file's block chain (block header: prev, next, length) */
typedef struct { uint16_t next; uint16_t len, pos; uint32_t left; } fs_reader;

static int fs_load_block(fs_reader *r)
{
    if (r->next == 0xFFFF) return 0;
    flash_read(FS_BASE + (10u + r->next) * BLOCK, blk, BLOCK);
    r->next = (uint16_t)(blk[2] | blk[3] << 8);
    r->len = (uint16_t)(blk[4] | blk[5] << 8);
    r->pos = 0;
    return r->len <= BLOCK - 6;
}

static int fs_getbyte(void *ctx)
{
    fs_reader *r = ctx;
    if (!r->left) return -1;
    if (r->pos >= r->len && !fs_load_block(r)) return -1;
    r->left--;
    return blk[6 + r->pos++];
}

static void fs_open(fs_reader *r, const fs_file *f)
{
    r->next = f->block; r->len = 0; r->pos = 0; r->left = f->size;
}

static uint32_t fs_u32(fs_reader *r)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) v |= (uint32_t)fs_getbyte(r) << (8 * i);
    return v;
}

/* ---------------------------------------------------------------- main */

static void stop_hardware(void)
{
    REG(0x01C01028) = 0xFFFFFFFFu;              /* EDMA3 CC0 EECR: no more events */
    REG(0x01C0102C) = 0xFFFFFFFFu;              /* EECRH */
    REG(0x01D00044) = 0;                        /* McASP0 GBLCTL: stop serializers */
    *(volatile uint8_t *)0x01E00401 = 0;        /* USB0 POWER: SOFTCONN off */
}

static void caches_clean(void)
{
    REG(0x01845004) = 1;                        /* L2WBINV: write back + invalidate all */
    while (REG(0x01845004) & 1)
        ;
    REG(0x01845028) = 1;                        /* L1PINV */
    while (REG(0x01845028) & 1)
        ;
}

void loader_main(void)
{
    fs_file fbin, fwad;
    fs_reader r;
    uint32_t entry, nseg, i, raw, got, wad;
    stop_hardware();
    REG(0x01848200) = 1;                        /* MAR128: cache shared RAM (this code) */
    zl_select_flash();                          /* the OS left the CS pin on GPIO (LCD) */
    if (!fs_find("DOOM.BIN", &fbin)) fail(1);
    if (!fs_find("DOOM.WDZ", &fwad)) fail(2);

    fs_open(&r, &fbin);
    if (fs_u32(&r) != 0x4D4F445Au) fail(3);     /* "ZDOM" */
    entry = fs_u32(&r);
    nseg = fs_u32(&r);
    for (i = 0; i < nseg; i++) {
        uint8_t *dst = (uint8_t *)fs_u32(&r);
        uint32_t n = fs_u32(&r), k;
        for (k = 0; k < n; k++) {
            int c = fs_getbyte(&r);
            if (c < 0) fail(4);
            dst[k] = (uint8_t)c;
        }
        for (; k & 3; k++) fs_getbyte(&r);      /* segments are padded to 4 */
    }

    fs_open(&r, &fwad);
    if (fs_u32(&r) != 0x5A44575Au) fail(5);     /* "ZWDZ" */
    raw = fs_u32(&r);
    wad = (SDRAM_END - raw) & ~0xFFFu;
    if (inflate_raw(fs_getbyte, &r, (uint8_t *)wad, raw, &got) || got != raw) fail(6);

    MAILBOX->wad = wad;
    MAILBOX->wad_len = raw;
    MAILBOX->flags = 0;
    MAILBOX->magic = 0x424D445Au;               /* "ZDMB" */
    zl_select_lcd();                            /* Doom only talks to the LCD from now on */
    caches_clean();
    ((void (*)(void))entry)();
    fail(7);
}
