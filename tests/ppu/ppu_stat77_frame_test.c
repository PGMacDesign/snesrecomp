/* Synthetic regression: $213E's range-over (bit 6) and time-over (bit 7)
 * flags hold for the rest of the frame that set them and clear at the start
 * of the next one, also for a host that draws lines 1..224 and never passes
 * line 0 (the beam frame driver).
 * No game ROM, generated data, or platform frontend is required. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "snes/ppu.h"
#include "snes/snes.h"

Snes *g_snes;

/* ppu.c references these runtime globals, but this harness links ppu.c alone. */
int snes_frame_counter;
unsigned char g_snesrecomp_last_hdmaen;

uint16_t WsShadowTile(int layer, int screen_x, uint32_t wrapped_y,
                      uint16_t real_tile) {
    (void)layer;
    (void)screen_x;
    (void)wrapped_y;
    return real_tile;
}

bool WsShadowLayerActive(int layer) {
    (void)layer;
    return false;
}

uint32_t WsShadowWorldX(int layer) {
    (void)layer;
    return 0;
}

uint32_t WsShadowPresentWorldY(int layer, int screen_x) {
    (void)layer;
    (void)screen_x;
    return 0;
}

uint32_t WsShadowScrollY(int layer) {
    (void)layer;
    return 0;
}

void WsShadowOnVramWrite(uint16_t word_adr, uint16_t value) {
    (void)word_adr;
    (void)value;
}

enum { kRangeOver = 0x40, kTimeOver = 0x80 };

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

/* With crowd, 33 large (16x16) sprites at OAM y 0, drawn on lines 1..16:
 * one more than the 32-sprite range limit, and 32 x 2 slivers is past the
 * 34-sliver time limit. Without it, every sprite sits below the field. */
static void place_sprites(Ppu *ppu, bool crowd) {
    memset(ppu->highOam, 0, sizeof ppu->highOam);
    for (int slot = 0; slot < 128; slot++) {
        bool on = crowd && slot < 33;
        ppu->oam[slot * 2] = (uint16_t)(((on ? 0 : 0xf0) << 8) | ((slot * 7) & 0xff));
        ppu->oam[slot * 2 + 1] = 0;
        if (on) ppu->highOam[slot >> 2] |= (uint8_t)(2u << ((slot & 3) * 2));
    }
}

/* The beam frame driver's order: lines first..last, never line 0. */
static void draw_lines(Ppu *ppu, int first, int last) {
    for (int line = first; line <= last; line++)
        ppu_runLine(ppu, line);
}

int main(void) {
    enum { kPitch = kPpuXPixels * 4 };
    static uint8_t pixels[kPitch * 240];
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;
    ppu_reset(ppu);
    ppu->inidisp = 0x0f;
    ppu->screenEnabled[0] = 1 << 4;
    PpuBeginDrawing(ppu, pixels, kPitch, 0);

    /* Frame 1: the crowd is on line 1; it leaves before line 20. */
    place_sprites(ppu, true);
    draw_lines(ppu, 1, 1);
    uint8_t stat = ppu_read(ppu, 0x3e);
    failures += check((stat & (kRangeOver | kTimeOver)) == (kRangeOver | kTimeOver),
                      "33 large sprites on a line set range-over and time-over");
    failures += check((stat & 0x0f) == 1, "the PPU1 version stays in bits 0-3");
    place_sprites(ppu, false);
    draw_lines(ppu, 2, 224);
    snes_frame_counter++;
    stat = ppu_read(ppu, 0x3e);
    failures += check((stat & (kRangeOver | kTimeOver)) == (kRangeOver | kTimeOver),
                      "the flags hold for the rest of the frame and through vblank");

    /* Frame 2: no line overflows, so the flags clear on its first drawn line. */
    draw_lines(ppu, 1, 224);
    snes_frame_counter++;
    stat = ppu_read(ppu, 0x3e);
    failures += check((stat & (kRangeOver | kTimeOver)) == 0,
                      "a frame with no overflow clears both flags");

    /* Frame 3: overflow again; the reset never stops the flags being set. */
    place_sprites(ppu, true);
    draw_lines(ppu, 1, 224);
    snes_frame_counter++;
    stat = ppu_read(ppu, 0x3e);
    failures += check((stat & (kRangeOver | kTimeOver)) == (kRangeOver | kTimeOver),
                      "an overflow in a later frame sets the flags again");

    /* A host that passes line 0 still gets the same per-frame reset. */
    place_sprites(ppu, false);
    draw_lines(ppu, 0, 224);
    snes_frame_counter++;
    stat = ppu_read(ppu, 0x3e);
    failures += check((stat & (kRangeOver | kTimeOver)) == 0,
                      "a host that draws line 0 also clears the flags");

    if (failures) {
        fprintf(stderr, "ppu_stat77_frame_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("ppu_stat77_frame_test: PASS");
    return 0;
}
