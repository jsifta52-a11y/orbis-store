#pragma once

/* ── UI colour palette (PS4 blue/white style) ────────────────────────────── */
#define UI_COL_BG          0xFF0A1A2F   /* dark navy background           */
#define UI_COL_PANEL       0xFF0F2A4A   /* panel / card background        */
#define UI_COL_ACCENT      0xFF1F6FEB   /* PS4 blue accent                */
#define UI_COL_ACCENT_HOV  0xFF388BFD   /* hovered / selected accent      */
#define UI_COL_TEXT        0xFFFFFFFF   /* primary text (white)           */
#define UI_COL_TEXT_DIM    0xFF99AACC   /* secondary / dimmed text        */
#define UI_COL_TEXT_BLUE   0xFF4AA3FF   /* heading / label blue           */
#define UI_COL_BORDER      0xFF2C6FB2   /* panel border                   */
#define UI_COL_ERROR       0xFFFF6666   /* error red                      */
#define UI_COL_SUCCESS     0xFF6ABF88   /* success green                  */

/* ── screen layout constants ─────────────────────────────────────────────── */
#define UI_SCREEN_W        1920
#define UI_SCREEN_H        1080
#define UI_TOPBAR_H          70
#define UI_PADDING           30
#define UI_ITEM_H            60   /* height of one list row              */
#define UI_MAX_VISIBLE_ITEMS 12   /* rows visible in the list viewport   */

/* ── PS4 controller button masks (standard HID layout) ───────────────────── */
#define BTN_CROSS     (1 << 14)
#define BTN_CIRCLE    (1 << 13)
#define BTN_SQUARE    (1 << 15)
#define BTN_TRIANGLE  (1 << 12)
#define BTN_UP        (1 << 4)
#define BTN_DOWN      (1 << 6)
#define BTN_LEFT      (1 << 7)
#define BTN_RIGHT     (1 << 5)
#define BTN_L1        (1 << 10)
#define BTN_R1        (1 << 11)
#define BTN_OPTIONS   (1 << 3)

/* ── application sections ────────────────────────────────────────────────── */
typedef enum {
    SECTION_STORE = 0,
    SECTION_LIBRARY,
    SECTION_REDEEM,
    SECTION_SETTINGS,
    SECTION_COUNT
} AppSection;

/* ── on-screen keyboard ──────────────────────────────────────────────────── */
#define OSK_INPUT_MAX 32

typedef struct {
    char  buffer[OSK_INPUT_MAX + 1];   /* current typed text (NUL-term)  */
    int   length;                      /* number of characters typed     */
    int   row;                         /* cursor row on the keyboard     */
    int   col;                         /* cursor column                  */
    int   active;                      /* 1 while keyboard is open       */
    int   confirmed;                   /* 1 when user pressed ✓ / Enter  */
    int   cancelled;                   /* 1 when user pressed ✗ / Back   */
} OskState;

/* ── notification pop-up ─────────────────────────────────────────────────── */
typedef struct {
    char    text[256];
    int     frames_left;   /* counts down each frame; 0 = not visible   */
    int     is_error;      /* 1 = show in red, 0 = show in green         */
} Notification;

/* ── forward declarations for platform drawing primitives ─────────────────
 *
 * These are filled in by ui.c using whatever rendering back-end is available
 * (e.g. orbisGl, SDL2, or a direct frame-buffer renderer).
 * Each returns 0 on success, -1 on failure.
 * ─────────────────────────────────────────────────────────────────────────── */

int  ui_init(void);
void ui_shutdown(void);

/* Call at the start of each frame to clear the back-buffer. */
void ui_begin_frame(void);

/* Call at the end of each frame to flip/present. */
void ui_end_frame(void);

/* Draw a filled rectangle. colour is 0xAARRGGBB. */
void ui_draw_rect(int x, int y, int w, int h, unsigned int colour);

/* Draw outlined rectangle (border only). */
void ui_draw_rect_outline(int x, int y, int w, int h, unsigned int colour, int thickness);

/* Draw UTF-8 text.  size: approx. pixel height. */
void ui_draw_text(int x, int y, unsigned int colour, int size, const char *fmt, ...);

/* Draw the top navigation bar with the current section highlighted. */
void ui_draw_topbar(AppSection current);

/* Draw the standard hint row at the bottom (e.g. ✗ Back  ✓ Select). */
void ui_draw_hints(const char *hints);

/* ── on-screen keyboard API ──────────────────────────────────────────────── */

/* Open the keyboard with an optional placeholder label. */
void osk_open(OskState *osk, const char *label);

/* Close the keyboard without consuming the input. */
void osk_close(OskState *osk);

/*
 * Process a button-press event for the keyboard.
 * Returns 1 if the keyboard consumed the input, 0 otherwise.
 */
int  osk_handle_input(OskState *osk, unsigned int buttons);

/* Render the keyboard overlay. */
void osk_draw(const OskState *osk);

/* ── notification API ────────────────────────────────────────────────────── */

void notify_show(Notification *n, int is_error, const char *fmt, ...);
void notify_draw(Notification *n);   /* decrements frames_left each call */
