/* Synthetic check of the host-only color math window buffer
 * (Ppu.mathWindowBuffer): for each composed position it holds 1 where the
 * color window let color math apply under every CGWSEL prevent-math setting,
 * 0 where it did not, clears every line (forced blank and a line run for
 * state only included), is off by default and leaves the picture
 * byte-identical whether it is on or off.
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

enum { kPitch = kPpuXPixels * 4, kWindowLeft = 10, kWindowRight = 13 };

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

/* Line 0 shows only the backdrop, with backdrop math adding a fixed color, so
 * every position goes through the per-pixel math path. The color window
 * (window 1, screen x 10..13) and CGWSEL's prevent-math setting decide where
 * math applies. */
static void setup(Ppu *ppu, uint8_t *pixels, unsigned prevent) {
    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch, kPpuRenderFlags_NewRenderer);
    ppu->inidisp = 0x0f;
    ppu->cgram[0] = 0x0421;
    ppu->fixedColor = 0x2108;
    ppu->cgadsub = 0x20;
    ppu->cgwsel = (uint8_t)(prevent << 4);
    ppu->windowsel = (uint32_t)kWindow1Enabled << 20;
    ppu->window1left = kWindowLeft;
    ppu->window1right = kWindowRight;
}

static void draw_line_0(Ppu *ppu) {
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
}

/* Positions of screen x 0..255 whose value differs from the expectation for
 * `prevent`; positions outside the screen must stay 0. */
static int count_wrong(const uint8_t *buf, unsigned prevent) {
    int wrong = 0;
    for (int i = 0; i < kPpuBufWidth; i++) {
        const int x = i - kPpuExtraLeftRight;
        const bool on_screen = x >= 0 && x < kPpuXPixels;
        const bool inside = x >= kWindowLeft && x <= kWindowRight;
        /* CGWSEL bits 4-5: 0 never prevents, 1 prevents outside the window,
         * 2 inside it, 3 everywhere. */
        const bool allowed = prevent == 0 || (prevent == 1 && inside) || (prevent == 2 && !inside);
        wrong += buf[i] != (on_screen && allowed ? 1 : 0);
    }
    return wrong;
}

int main(void) {
    static uint8_t window[kPpuBufWidth];
    uint8_t pixels[kPitch], plain[kPitch];
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;

    for (unsigned prevent = 0; prevent < 4; prevent++) {
        /* Off: nothing is written, and this is the picture to compare with. */
        setup(ppu, plain, prevent);
        memset(window, 0x5a, sizeof window);
        draw_line_0(ppu);
        bool untouched = true;
        for (size_t i = 0; i < sizeof window; i++) untouched = untouched && window[i] == 0x5a;
        failures += check(untouched && ppu->mathWindowBuffer == NULL, "off by default, and writes nothing");

        setup(ppu, pixels, prevent);
        ppu->mathWindowBuffer = window;
        draw_line_0(ppu);
        char message[96];
        snprintf(message, sizeof message, "prevent-math setting %u: 1 exactly where math applies", prevent);
        failures += check(count_wrong(window, prevent) == 0, message);
        snprintf(message, sizeof message, "prevent-math setting %u: the picture is byte-identical", prevent);
        failures += check(!memcmp(pixels, plain, kPitch), message);
    }

    /* The picture itself shows the window: math on at x 0 and off at x 10
     * under setting 2, so the buffer agrees with what was drawn. */
    setup(ppu, pixels, 2);
    draw_line_0(ppu);
    failures += check(memcmp(pixels + 0 * 4, pixels + kWindowLeft * 4, 4) != 0,
                      "setting 2 draws math outside the window and none inside");

    /* Every line starts clear: forced blank composes nothing. */
    setup(ppu, pixels, 0);
    ppu->mathWindowBuffer = window;
    draw_line_0(ppu);
    ppu->inidisp = 0x8f;
    draw_line_0(ppu);
    {
        bool clear = true;
        for (size_t i = 0; i < sizeof window; i++) clear = clear && window[i] == 0;
        failures += check(clear, "forced blank leaves every position 0");
    }

    /* A line run for state only composes nothing either. */
    setup(ppu, pixels, 0);
    ppu->mathWindowBuffer = window;
    draw_line_0(ppu);
    ppu_runLineState(ppu, 1);
    {
        bool clear = true;
        for (size_t i = 0; i < sizeof window; i++) clear = clear && window[i] == 0;
        failures += check(clear, "a line run for state only leaves every position 0");
    }

    ppu->mathWindowBuffer = window;
    ppu_reset(ppu);
    failures += check(ppu->mathWindowBuffer == NULL, "reset turns the buffer off");

    if (failures) return 1;
    printf("PASS: color math window buffer\n");
    return 0;
}
