#include "ui.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*
 * Fallback UI backend.
 *
 * This keeps the app logic buildable even when a full GPU/text renderer
 * is not available in the current toolchain setup.
 */

int ui_init(void)
{
    return 0;
}

void ui_shutdown(void)
{
}

void ui_begin_frame(void)
{
}

void ui_end_frame(void)
{
}

void ui_draw_rect(int x, int y, int w, int h, unsigned int colour)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)colour;
}

void ui_draw_rect_outline(int x, int y, int w, int h, unsigned int colour, int thickness)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)colour;
    (void)thickness;
}

void ui_draw_text(int x, int y, unsigned int colour, int size, const char *fmt, ...)
{
    (void)x;
    (void)y;
    (void)colour;
    (void)size;
    (void)fmt;
}

static const char *SECTION_NAMES[SECTION_COUNT] = {
    "Store", "Library", "Redeem", "Settings"
};

void ui_draw_topbar(AppSection current)
{
    (void)current;
    (void)SECTION_NAMES;
}

void ui_draw_hints(const char *hints)
{
    (void)hints;
}

static const char *OSK_ROWS[] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM<>",
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
        if (osk->row > 0) {
            osk->row--;
            osk->col = 0;
        }
        return 1;
    }
    if (buttons & BTN_DOWN) {
        if (osk->row < OSK_ROW_COUNT - 1) {
            osk->row++;
            osk->col = 0;
        }
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
            if (osk->length > 0) {
                osk->buffer[--osk->length] = '\0';
            }
        } else if (ch == '>') {
            osk->confirmed = 1;
            osk->active = 0;
        } else if (osk->length < OSK_INPUT_MAX) {
            osk->buffer[osk->length++] = ch;
            osk->buffer[osk->length] = '\0';
        }
        return 1;
    }

    if (buttons & BTN_CIRCLE) {
        osk->cancelled = 1;
        osk->active = 0;
        return 1;
    }

    return 0;
}

void osk_draw(const OskState *osk)
{
    (void)osk;
}

#define NOTIFY_DURATION_FRAMES 180

void notify_show(Notification *n, int is_error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(n->text, sizeof(n->text), fmt, ap);
    va_end(ap);
    n->frames_left = NOTIFY_DURATION_FRAMES;
    n->is_error = is_error;
}

void notify_draw(Notification *n)
{
    if (n->frames_left <= 0) return;
    n->frames_left--;
}
