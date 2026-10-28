/* Doom CFW: the pedal side of the platform layer (built with ZD_PEDAL).

   Runs after loader.c has stopped the Zoom OS. What is still used from the OS is only
   the hardware setup it did at boot (pin multiplexing, SPI0, GPIO); the LCD is driven by
   zd_lcd.h, a copy of the OS driver's register sequence (the OS driver itself calls OS
   helpers in SDRAM, which Doom's heap overwrites).

   Controls (GPIO numbers as in the emulator; every key and the footswitch is active low).
   v3, after the first runs on the MS-60B (2026-10-28): the D-pad pins are the mirror of
   the MS-50G table on both axes (up 7, down 5, left 6, right 4).
     D-pad up / down (7 / 5)       walk forward / back (held)   menus: up / down
     D-pad left / right (6 / 4)    strafe left / right (held)   menus: left / right
     knob 1 (encoder 17+30)        turn, TURN_DEG per click     menus: left / right
     knob 2 (encoder 18+29)        previous / next weapon       menus: up / down
     knob 3 (encoder 26+28)        automap on / off
     knob 3 press (27, right)      fire
     knob 1 press (24)             use (doors, switches)        menus: Enter
     footswitch (35)               use                          menus: Enter
     knob 2 press (25)             menu (Esc)
   Always run. Keys are debounced (DEBOUNCE_MS), so a bouncing contact gives one press.
   The encoders are decoded in the GPIO bank 1 interrupt (zd_gpio_isr, vectors in
   zd_vectors.asm), as the Zoom OS does: polling between frames lost clicks of a fast turn.
   Turning adds straight to Doom's mouse turn (mousex), summed into the next tic. */
#include <stdint.h>
#include <string.h>
#include <c6x.h>
#include "doomkeys.h"
#include "zd_lcd.h"
#include "doomstat.h"

extern int mousex, joybspeed;                   /* g_game.c, m_controls.c */

#define CPU_HZ   300000000u
#define DEBOUNCE_MS 20
#define TURN_DEG    10                 /* per knob 1 click */

/* mailbox left by loader.c at the top of shared RAM */
typedef struct { uint32_t magic, wad, wad_len, flags; } zd_mailbox;
#define MAILBOX ((volatile zd_mailbox *)0x8001FFC0)
#define MAILBOX_MAGIC 0x424D445Au                /* "ZDMB" */

extern unsigned char *zd_wad;
extern volatile uint32_t zd_wad_len;
extern uint8_t zd_lcd[1024];

typedef struct {
    volatile uint32_t head, tail;
    volatile uint8_t ev[64][2];
} zd_keyring;
extern zd_keyring zd_keys;

#define GPIO_IN01 (*(volatile uint32_t *)0x01E26020)
#define GPIO_IN23 (*(volatile uint32_t *)0x01E26048)
#define GPIO_BINTEN        (*(volatile uint32_t *)0x01E26008)
#define GPIO_SET_RIS_TRIG01 (*(volatile uint32_t *)0x01E26024)
#define GPIO_SET_FAL_TRIG01 (*(volatile uint32_t *)0x01E2602C)
#define GPIO_INTSTAT01     (*(volatile uint32_t *)0x01E26034)
#define INTC_EVTCLR1       (*(volatile uint32_t *)0x01800044)
#define INTC_INTMUX2       (*(volatile uint32_t *)0x01800108)   /* INT8..11 */
#define EVT_GPIO_B1        41

static void push(int down, int key)
{
    uint32_t h = zd_keys.head;
    if (h - zd_keys.tail >= 64)
        return;                                 /* full: drop rather than overwrite */
    zd_keys.ev[h & 63][0] = (uint8_t)down;
    zd_keys.ev[h & 63][1] = (uint8_t)key;
    zd_keys.head = h + 1;
}

static uint32_t now_ms(void)
{
    uint32_t lo = TSCL;
    uint64_t t = ((uint64_t)TSCH << 32) | lo;
    return (uint32_t)(t / (CPU_HZ / 1000));
}

/* ---------------------------------------------------------------- buttons */

/* key in a level, key in menus and other screens; the key pressed is the one released */
typedef struct { uint8_t pin, level_key, menu_key, raw, held, sent; uint32_t since; } button;
static button buttons[] = {
    { 7, KEY_UPARROW, KEY_UPARROW }, { 5, KEY_DOWNARROW, KEY_DOWNARROW },
    { 6, KEY_STRAFE_L, KEY_LEFTARROW }, { 4, KEY_STRAFE_R, KEY_RIGHTARROW },
    { 27, KEY_FIRE, KEY_FIRE }, { 24, KEY_USE, KEY_ENTER }, { 35, KEY_USE, KEY_ENTER },
    { 25, KEY_ESCAPE, KEY_ESCAPE },
};

static int pin_low(uint32_t in01, uint32_t in23, int pin)
{
    uint32_t v = pin < 32 ? in01 >> pin : in23 >> (pin - 32);
    return !(v & 1);
}

/* ---------------------------------------------------------------- encoders */

typedef struct { uint8_t a, b, state; int8_t acc; } quad;
static quad knob[3] = { { 17, 30, 0, 0 }, { 18, 29, 0, 0 }, { 26, 28, 0, 0 } };

/* quadrature: +1 / -1 per valid transition, 0 for none or a skipped state */
static const int8_t qdec[16] = { 0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0 };

/* +1 / -1 once per detent, else 0. The encoders rest at 11 or 00 (pedal GPIO dump at rest:
   knobs 1-2 at 11, knob 3 at 00), so a detent is half a quadrature cycle: two transitions. */
static int detent(quad *q, uint32_t in01)
{
    uint8_t ab = (uint8_t)((((in01 >> q->a) & 1) << 1) | ((in01 >> q->b) & 1));
    q->acc -= qdec[(q->state << 2) | ab];            /* sign as the OS: + = clockwise = more */
    q->state = ab;
    if (q->acc >= 2) { q->acc = 0; return 1; }
    if (q->acc <= -2) { q->acc = 0; return -1; }
    return 0;
}

/* clicks counted by the interrupt; only it writes them, knobs() keeps what it has used */
static volatile int32_t clicks[3];
static int32_t used[3];

interrupt void zd_gpio_isr(void)
{
    uint32_t in01;
    unsigned i;
    GPIO_INTSTAT01 = GPIO_INTSTAT01;            /* ack first: an edge after the read raises again */
    INTC_EVTCLR1 = 1u << (EVT_GPIO_B1 - 32);
    in01 = GPIO_IN01;
    for (i = 0; i < 3; i++)
        clicks[i] += detent(&knob[i], in01);
}

static int take(int i)
{
    int d = clicks[i] - used[i];
    used[i] += d;
    return d;
}

static void tap(int key) { push(1, key); push(0, key); }

static int weapon = 1;
static void weapon_step(int d)
{
    weapon = (weapon + d + 6) % 7 + 1;          /* 1..7; Doom ignores ones you lack */
    tap('0' + weapon);
}

static void knobs(void)
{
    int in_level = gamestate == GS_LEVEL && !menuactive;
    int d1 = take(0), d2 = take(1), d3 = take(2), n;
    if (!in_level) {
        for (n = d1 < 0 ? -d1 : d1; n-- > 0;) tap(d1 > 0 ? KEY_RIGHTARROW : KEY_LEFTARROW);
        for (n = d2 < 0 ? -d2 : d2; n-- > 0;) tap(d2 > 0 ? KEY_UPARROW : KEY_DOWNARROW);
    } else {
        /* angleturn -= mousex * 8, 65536 = full circle; + = clockwise = turn right */
        mousex += d1 * (TURN_DEG * 65536 / 360 / 8);
        if (mousex > 3000) mousex = 3000;
        if (mousex < -3000) mousex = -3000;
        for (n = d2 < 0 ? -d2 : d2; n-- > 0;) weapon_step(d2 > 0 ? 1 : -1);
        if (d3 & 1) tap(KEY_TAB);
    }
}

void zd_poll_keys(void)
{
    uint32_t in01 = GPIO_IN01, in23 = GPIO_IN23, t = now_ms();
    int in_level = gamestate == GS_LEVEL && !menuactive;
    unsigned i;
    joybspeed = 29;                             /* >= MAX_JOY_BUTTONS: always run */
    for (i = 0; i < sizeof buttons / sizeof buttons[0]; i++) {
        button *b = &buttons[i];
        int low = pin_low(in01, in23, b->pin);
        if (low != b->raw) {
            b->raw = (uint8_t)low;
            b->since = t;
        } else if (low != b->held && t - b->since >= DEBOUNCE_MS) {
            b->held = (uint8_t)low;
            if (low)
                b->sent = in_level ? b->level_key : b->menu_key;
            push(low, b->sent);
        }
    }
    knobs();
}

/* ---------------------------------------------------------------- LCD */

void zd_lcd_flush(void)
{
    int p;
    zl_select_lcd();
    for (p = 0; p < 8; p++) {
        zl_page(p, zd_lcd + p * 128);
        zd_poll_keys();
    }
    zl_done();
}

void zd_pedal_init(void)
{
    volatile zd_mailbox *mb = MAILBOX;
    if (mb->magic == MAILBOX_MAGIC) {
        zd_wad = (unsigned char *)mb->wad;
        zd_wad_len = mb->wad_len;
    }
    {
        uint32_t in01 = GPIO_IN01;
        unsigned i;
        for (i = 0; i < 3; i++)
            knob[i].state = (uint8_t)((((in01 >> knob[i].a) & 1) << 1) | ((in01 >> knob[i].b) & 1));
        for (i = 0; i < sizeof buttons / sizeof buttons[0]; i++)
            buttons[i].raw = buttons[i].held = 0;   /* held at power-on: pressed once it settles */
    }
    {
        extern far uint32_t zd_vectors[];
        uint32_t enc = 0;
        unsigned i;
        for (i = 0; i < 3; i++)
            enc |= 1u << knob[i].a | 1u << knob[i].b;
        GPIO_SET_RIS_TRIG01 = enc;
        GPIO_SET_FAL_TRIG01 = enc;
        GPIO_INTSTAT01 = enc;
        GPIO_BINTEN |= 1u << 1;                 /* bank 1: GPIO 16..31 */
        INTC_INTMUX2 = (INTC_INTMUX2 & ~0x7Fu) | EVT_GPIO_B1;
        INTC_EVTCLR1 = 1u << (EVT_GPIO_B1 - 32);
        ISTP = (uint32_t)zd_vectors;
        ICR = 1u << 8;
        IER = (1u << 8) | 2u;                   /* INT8 + NMIE (needed for any maskable one) */
        CSR |= 1u;                              /* GIE */
    }
}
