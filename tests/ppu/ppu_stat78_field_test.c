/* Synthetic regression: $213F bit 7 (the interlace field) flips once per
 * frame, also for a host that draws lines 1..224 and never passes line 0
 * (the beam frame driver), and only once for a host that passes both.
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

enum { kField = 0x80 };

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

/* One frame the way a host draws it: the beam frame driver passes lines
 * 1..224, other hosts start at line 0. Returns $213F's field bit after it. */
static int draw_frame(Ppu *ppu, int first_line) {
    for (int line = first_line; line <= 224; line++)
        ppu_runLine(ppu, line);
    snes_frame_counter++;
    return (ppu_read(ppu, 0x3f) & kField) != 0;
}

int main(void) {
    enum { kPitch = kPpuXPixels * 4 };
    static uint8_t pixels[kPitch * 240];
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;

    for (int first_line = 0; first_line <= 1; first_line++) {
        ppu_reset(ppu);
        ppu->inidisp = 0x0f;
        PpuBeginDrawing(ppu, pixels, kPitch, 0);
        int field = (ppu_read(ppu, 0x3f) & kField) != 0;
        for (int frame = 0; frame < 6; frame++) {
            int next = draw_frame(ppu, first_line);
            failures += check(next != field, first_line
                ? "the field bit flips once per frame for a host that starts at line 1"
                : "the field bit flips once per frame for a host that starts at line 0");
            field = next;
        }

        /* The bit holds between frames and across $213F reads. */
        int again = (ppu_read(ppu, 0x3f) & kField) != 0;
        failures += check(again == field, "reading $213F does not change the field bit");

        /* Lines after the first do not flip it. */
        for (int line = first_line; line <= 1; line++)
            ppu_runLine(ppu, line);
        int start = (ppu_read(ppu, 0x3f) & kField) != 0;
        for (int line = 2; line <= 224; line++)
            ppu_runLine(ppu, line);
        failures += check(((ppu_read(ppu, 0x3f) & kField) != 0) == start,
                          "lines 2..224 leave the field bit alone");
    }

    if (failures) {
        fprintf(stderr, "ppu_stat78_field_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("ppu_stat78_field_test: PASS");
    return 0;
}
