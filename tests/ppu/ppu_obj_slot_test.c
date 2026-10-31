/* Synthetic check of the host-only OBJ slot buffer (Ppu.objSlotBuffer): it
 * names the OAM slot whose pixel won each objBuffer position, including where
 * two sprites drew the same color, follows OAM priority rotation, clears every
 * line, and leaves the picture byte-identical whether it is on or off.
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

uint16_t WsShadowTile(int layer, int screen_x, uint32_t wrapped_y, uint16_t real_tile) {
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

enum { kPitch = kPpuXPixels * 4 };

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

/* Two 8x8 sprites on line 0, both palette 0 and every pixel color 15, so
 * objBuffer holds the same value wherever either shows: slot 3 at x 10 and
 * slot 7 at x 14, overlapping at x 14..17. */
static void setup_two_same_color_sprites(Ppu *ppu, uint8_t *pixels) {
    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch, kPpuRenderFlags_NewRenderer);
    ppu->inidisp = 0x0f;
    ppu->screenEnabled[0] = 1 << 4;
    for (int slot = 0; slot < 128; slot++) ppu->oam[slot * 2] = 0xf000;
    ppu->oam[3 * 2] = 10;
    ppu->oam[7 * 2] = 14;
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++) ppu->vram[i] = 0xffff;
    for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
        ppu->cgram[i] = (uint16_t)(i * 37 & 0x7fff);
}

static void draw_line_0(Ppu *ppu) {
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
}

static uint8_t slot_at(const uint8_t *slots, int x) { return slots[kPpuExtraLeftRight + x]; }

static bool all_none(const uint8_t *slots) {
    for (size_t i = 0; i < kPpuBufWidth; i++)
        if (slots[i] != kPpuObjSlotNone) return false;
    return true;
}

/* Slot expected at screen x 0..255 with `front` winning the overlap. */
static int count_wrong(const uint8_t *slots, int front) {
    int wrong = 0;
    for (int x = 0; x < kPpuXPixels; x++) {
        int want = kPpuObjSlotNone;
        if (x >= 14 && x < 18) want = front;
        else if (x >= 10 && x < 14) want = 3;
        else if (x >= 18 && x < 22) want = 7;
        wrong += slot_at(slots, x) != want;
    }
    return wrong;
}

int main(void) {
    static uint8_t slots[kPpuBufWidth];
    uint8_t pixels[kPitch], plain[kPitch];
    static PpuPixelPrioBufs plain_obj;
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;

    /* Off: nothing is written, and this is the picture to compare with. */
    setup_two_same_color_sprites(ppu, plain);
    memset(slots, 0x5a, sizeof slots);
    draw_line_0(ppu);
    plain_obj = ppu->objBuffer;
    bool untouched = true;
    for (size_t i = 0; i < sizeof slots; i++) untouched = untouched && slots[i] == 0x5a;
    failures += check(untouched && ppu->objSlotBuffer == NULL, "off by default, and writes nothing");
    failures += check(ppu->objBuffer.data[kPpuExtraLeftRight + 12] ==
                          ppu->objBuffer.data[kPpuExtraLeftRight + 20],
                      "the two sprites leave the same objBuffer value");

    /* On: the lower slot wins the overlap, each sprite names its own pixels. */
    setup_two_same_color_sprites(ppu, pixels);
    ppu->objSlotBuffer = slots;
    draw_line_0(ppu);
    failures += check(count_wrong(slots, 3) == 0, "slots: 3 at 10..17, 7 at 18..21, none elsewhere");
    failures += check(!memcmp(pixels, plain, kPitch), "the picture is byte-identical with the buffer on");
    failures += check(!memcmp(&ppu->objBuffer, &plain_obj, sizeof plain_obj),
                      "objBuffer is identical with the buffer on");
    failures += check(!ppu->objSlotLineHasExtra, "no extra objects, no extra flag");

    /* Priority rotation from slot 7: 7 is evaluated first and wins. */
    setup_two_same_color_sprites(ppu, pixels);
    ppu->objSlotBuffer = slots;
    ppu->oamaddh = 0x80;
    ppu->oamaddl = 7 * 2;
    draw_line_0(ppu);
    failures += check(count_wrong(slots, 7) == 0, "rotation: slot 7 wins the overlap");

    /* Every line starts clear: sprites gone, then forced blank. */
    for (int slot = 0; slot < 128; slot++) ppu->oam[slot * 2] = 0xf000;
    draw_line_0(ppu);
    failures += check(all_none(slots), "a line without sprites leaves no slot behind");
    setup_two_same_color_sprites(ppu, pixels);
    ppu->objSlotBuffer = slots;
    ppu->inidisp = 0x8f;
    draw_line_0(ppu);
    failures += check(all_none(slots), "forced blank leaves every position without a slot");

    /* An extra host object on the line flags it; the next line without one clears it. */
    {
        static const uint16_t art[4] = {0x8001, 0x8001, 0x8001, 0x8001};
        PpuExtraObject extra = {.x = 40, .y = 0, .width = 4, .height = 1, .stride = 4,
                                .priority = 2, .oamSlot = 9, .order = 1, .pixels = art};
        setup_two_same_color_sprites(ppu, pixels);
        ppu->objSlotBuffer = slots;
        PpuSetExtraObjects(ppu, &extra, 1);
        draw_line_0(ppu);
        failures += check(ppu->objSlotLineHasExtra, "a line an extra object drew on is flagged");
        PpuSetExtraObjects(ppu, NULL, 0);
        draw_line_0(ppu);
        failures += check(!ppu->objSlotLineHasExtra, "and the next line without one is not");
        /* Forced blank evaluates no sprites, so only the line's own reset
         * clears a flag the previous line set. */
        PpuSetExtraObjects(ppu, &extra, 1);
        draw_line_0(ppu);
        ppu->inidisp = 0x8f;
        draw_line_0(ppu);
        failures += check(!ppu->objSlotLineHasExtra, "nor is a forced-blank line after one");
        PpuSetExtraObjects(ppu, NULL, 0);
    }

    ppu->objSlotBuffer = slots;
    ppu_reset(ppu);
    failures += check(ppu->objSlotBuffer == NULL, "reset turns the buffer off");

    if (failures) return 1;
    printf("PASS: OBJ slot buffer\n");
    return 0;
}
