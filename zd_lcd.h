/* ST7565 LCD on SPI0 of the MS-60B / MS-70CDR, driven the way the Zoom OS's L2 driver
   does it (0x1181A120 / 0x1181A1E0 in MS-70CDR 2.10), but without any OS code, so it
   keeps working after the OS memory has been reused.

   Per byte the OS: raises CS (GPIO 91) and GPIO 84, drops CS, sets D/C (GPIO 37: 0 for a
   command, 1 for data), writes SPIDAT1 = byte (format 0, 1 MHz, no CSHOLD), waits for the
   receive, raises D/C. The flash CS pin is muxed with GPIO: PINMUX7[31:28] = 8 while the
   LCD is talked to, 1 (SPI0_SCS0) for flash. Header-only so the loader and Doom share it. */
#ifndef ZD_LCD_H
#define ZD_LCD_H
#include <stdint.h>

#define ZL_REG(a)    (*(volatile uint32_t *)(a))
#define ZL_PINMUX7   ZL_REG(0x01C1413C)
#define ZL_SET23     ZL_REG(0x01E26040)
#define ZL_CLR23     ZL_REG(0x01E26044)
#define ZL_SET45     ZL_REG(0x01E26068)
#define ZL_CLR45     ZL_REG(0x01E2606C)
#define ZL_SPIDAT1   ZL_REG(0x01C4103C)
#define ZL_SPIBUF    ZL_REG(0x01C41040)
#define ZL_CS        (1u << 27)                 /* GPIO 91 */
#define ZL_G84       (1u << 20)                 /* GPIO 84 */
#define ZL_DC        (1u << 5)                  /* GPIO 37 */

static inline void zl_select_lcd(void)   { ZL_PINMUX7 = (ZL_PINMUX7 & 0x0FFFFFFFu) | 0x80000000u; }
static inline void zl_select_flash(void) { ZL_SET45 = ZL_CS; ZL_PINMUX7 = (ZL_PINMUX7 & 0x0FFFFFFFu) | 0x10000000u; }

static inline void zl_byte(int data, uint8_t b)
{
    ZL_SET45 = ZL_CS;
    ZL_SET45 = ZL_G84;
    while (ZL_SPIBUF & (1u << 29))              /* TXFULL */
        ;
    ZL_CLR45 = ZL_CS;
    if (data) ZL_SET23 = ZL_DC; else ZL_CLR23 = ZL_DC;
    ZL_SPIDAT1 = b;
    while (ZL_SPIBUF & 0x80000000u)             /* RXEMPTY */
        ;
    (void)ZL_SPIBUF;
    ZL_SET23 = ZL_DC;
}

/* one page (0..7) of 128 columns; the glass starts at controller column 4 */
static inline void zl_page(int page, const uint8_t *col128)
{
    int x;
    zl_byte(0, (uint8_t)(0xB0 | page));
    zl_byte(0, 0x04);
    zl_byte(0, 0x10);
    for (x = 0; x < 128; x++)
        zl_byte(1, col128[x]);
}

static inline void zl_done(void) { ZL_SET45 = ZL_CS; }

#endif
