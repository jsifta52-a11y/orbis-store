#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/*
 * ui.c – rendering and input helpers for the Orbis Store.
 *
 * The drawing primitives (ui_draw_rect, ui_draw_text, etc.) delegate to the
 * platform back-end.  On PS4 homebrew this is typically orbisGl + a bitmap-
 * font renderer.  On a development host you can swap in an SDL2 back-end for
 * quick iteration.
 *
 * All platform-specific calls are isolated in the ui_platform_* stubs at the
 * bottom of this file so that porting only requires changing those routines.
 */

/* ─────────────────────────────────────────────────────────────────────────
 * Platform stubs – replace with real implementation for your SDK
 * ───────────────────────────────────────────────────────────────────────── */

/* Declared weak so they can be overridden by a platform-specific object. */
__attribute__((weak)) void platform_fill_rect(int x, int y, int w, int h, unsigned int col)  { (void)x;(void)y;(void)w;(void)h;(void)col; }
__attribute__((weak)) void platform_draw_text(int x, int y, unsigned int col, int sz, const char *s) { (void)x;(void)y;(void)col;(void)sz;(void)s; }
__attribute__((weak)) void platform_flip(void) {}

/* ─────────────────────────────────────────────────────────────────────────
 * Life-cycle
 * ───────────────────────────────────────────────────────────────────────── */

int ui_init(void)
{
    /* TODO: initialise graphics back-end (orbisGl / SDL_Init / etc.) */
    return 0;
}

void ui_shutdown(void)
{
    /* TODO: destroy graphics context */
}

void ui_begin_frame(void)
{
    ui_draw_rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, UI_COL_BG);
}

void ui_end_frame(void)
{
    platform_flip();
}

/* ─────────────────────────────────────────────────────────────────────────
 * Drawing primitives
 * ───────────────────────────────────────────────────────────────────────── */

void ui_draw_rect(int x, int y, int w, int h, unsigned int colour)
{
    platform_fill_rect(x, y, w, h, colour);
}

void ui_draw_rect_outline(int x, int y, int w, int h, unsigned int colour, int t)
{
    ui_draw_rect(x,         y,         w, t, colour);
    ui_draw_rect(x,         y + h - t, w, t, colour);
    ui_draw_rect(x,         y,         t, h, colour);
    ui_draw_rect(x + w - t, y,         t, h, colour);
}

void ui_draw_text(int x, int y, unsigned int colour, int size, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    platform_draw_text(x, y, colour, size, buf);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Top navigation bar
 * ───────────────────────────────────────────────────────────────────────── */

static const char *SECTION_NAMES[SECTION_COUNT] = {
    "Store", "Library", "Redeem", "Settings"
};

/* Approximate pixel width of one character at size 20 – used for label centering */
#define APPROX_CHAR_WIDTH_PX  12

{
    /* Background bar */
    ui_draw_rect(0, 0, UI_SCREEN_W, UI_TOPBAR_H, UI_COL_PANEL);
    ui_draw_rect(0, UI_TOPBAR_H - 2, UI_SCREEN_W, 2, UI_COL_BORDER);

    int tab_w = 180;
    int gap   = 16;
    int total = SECTION_COUNT * tab_w + (SECTION_COUNT - 1) * gap;
    int start_x = (UI_SCREEN_W - total) / 2;

    for (int i = 0; i < SECTION_COUNT; i++) {
        int tx = start_x + i * (tab_w + gap);
        int ty = (UI_TOPBAR_H - 44) / 2;
        unsigned int bg = (i == (int)current) ? UI_COL_ACCENT_HOV : UI_COL_ACCENT;

        ui_draw_rect(tx, ty, tab_w, 44, bg);
        /* Centre the label */
        int label_x = tx + (tab_w - (int)strlen(SECTION_NAMES[i]) * APPROX_CHAR_WIDTH_PX) / 2;
        ui_draw_text(label_x, ty + 12, UI_COL_TEXT, 20, "%s", SECTION_NAMES[i]);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Hint row
 * ───────────────────────────────────────────────────────────────────────── */

void ui_draw_hints(const char *hints)
{
    int y = UI_SCREEN_H - 40;
    ui_draw_rect(0, y - 4, UI_SCREEN_W, 44, UI_COL_PANEL);
    ui_draw_text(UI_PADDING, y, UI_COL_TEXT_DIM, 18, "%s", hints);
}

/* ─────────────────────────────────────────────────────────────────────────
 * On-screen keyboard
 *
 * Layout – 4 rows, uppercase alphanumeric + backspace/confirm/cancel.
 * ───────────────────────────────────────────────────────────────────────── */

static const char *OSK_ROWS[] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM<>",   /* < = backspace, > = confirm */
};
#define OSK_ROW_COUNT 4

void osk_open(OskState *osk, const char *label)
{
    (void)label;
    memset(osk, 0, sizeof(*osk));
    osk->active = 1;
}

void osk_close(OskState *osk)
{
    osk->active = 0;
}

int osk_handle_input(OskState *osk, unsigned int buttons)
{
    if (!osk->active) return 0;

    const char *row = OSK_ROWS[osk->row];
    int row_len = (int)strlen(row);

    if (buttons & BTN_UP) {
        if (osk->row > 0) { osk->row--; osk->col = 0; }
        return 1;
    }
    if (buttons & BTN_DOWN) {
        if (osk->row < OSK_ROW_COUNT - 1) { osk->row++; osk->col = 0; }
        return 1;
    }
    if (buttons & BTN_LEFT) {
        if (osk->col > 0) osk->col--;
        return 1;
    }
    if (buttons & BTN_RIGHT) {
        if (osk->col < row_len - 1) osk->col++;
        return 1;
    }

    if (buttons & BTN_CROSS) {
        char ch = row[osk->col];
        if (ch == '<') {
            /* Backspace */
            if (osk->length > 0) {
                osk->buffer[--osk->length] = '\0';
            }
        } else if (ch == '>') {
            /* Confirm */
            osk->confirmed = 1;
            osk->active    = 0;
        } else if (osk->length < OSK_INPUT_MAX) {
            osk->buffer[osk->length++] = ch;
            osk->buffer[osk->length]   = '\0';
        }
        return 1;
    }

    if (buttons & BTN_CIRCLE) {
        /* Cancel */
        osk->cancelled = 1;
        osk->active    = 0;
        return 1;
    }

    return 0;
}

void osk_draw(const OskState *osk)
{
    if (!osk->active) return;

    /* Semi-transparent overlay */
    ui_draw_rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, 0xCC000000);

    int kw = 800, kh = 320;
    int kx = (UI_SCREEN_W - kw) / 2;
    int ky = (UI_SCREEN_H - kh) / 2;

    ui_draw_rect(kx, ky, kw, kh, UI_COL_PANEL);
    ui_draw_rect_outline(kx, ky, kw, kh, UI_COL_BORDER, 2);

    /* Input buffer */
    ui_draw_text(kx + 20, ky + 16, UI_COL_TEXT_BLUE, 22, "Code: %s_", osk->buffer);

    /* Keys */
    int cell_w = 62, cell_h = 48, pad = 6;
    for (int r = 0; r < OSK_ROW_COUNT; r++) {
        const char *row = OSK_ROWS[r];
        int len = (int)strlen(row);
        int row_total = len * cell_w + (len - 1) * pad;
        int rx = kx + (kw - row_total) / 2;
        int ry = ky + 70 + r * (cell_h + pad);

        for (int c = 0; c < len; c++) {
            int cx = rx + c * (cell_w + pad);
            int selected = (r == osk->row && c == osk->col);

            unsigned int bg = selected ? UI_COL_ACCENT_HOV : UI_COL_ACCENT;
            ui_draw_rect(cx, ry, cell_w, cell_h, bg);

            char key[3] = { row[c], 0, 0 };
            if (key[0] == '<') { key[0] = '\x08'; key[1] = 0; }   /* backspace symbol */
            if (key[0] == '>') { key[0] = 'O'; key[1] = 'K'; }    /* confirm */

            ui_draw_text(cx + (cell_w - 14) / 2, ry + 14, UI_COL_TEXT, 18, "%s", key);
        }
    }

    ui_draw_text(kx + 20, ky + kh - 30, UI_COL_TEXT_DIM, 16,
                 "D-Pad: navigate   X: select   O: cancel");
}

/* ─────────────────────────────────────────────────────────────────────────
 * Notifications
 * ───────────────────────────────────────────────────────────────────────── */

#define NOTIFY_DURATION_FRAMES 180   /* ~3 s at 60 fps */

void notify_show(Notification *n, int is_error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(n->text, sizeof(n->text), fmt, ap);
    va_end(ap);
    n->frames_left = NOTIFY_DURATION_FRAMES;
    n->is_error    = is_error;
}

void notify_draw(Notification *n)
{
    if (n->frames_left <= 0) return;

    unsigned int bg  = n->is_error ? 0xCC882222 : 0xCC226622;
    unsigned int col = n->is_error ? UI_COL_ERROR : UI_COL_SUCCESS;

    int nw = 600, nh = 60;
    int nx = (UI_SCREEN_W - nw) / 2;
    int ny = UI_SCREEN_H - 100;

    ui_draw_rect(nx, ny, nw, nh, bg);
    ui_draw_rect_outline(nx, ny, nw, nh, col, 2);
    ui_draw_text(nx + 20, ny + 16, col, 20, "%s", n->text);

    n->frames_left--;
}
