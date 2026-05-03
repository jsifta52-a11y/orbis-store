/*
 * main.c – Orbis Store PS4 homebrew application
 *
 * Entry point, main loop and section navigation.
 *
 * Build with: make -C ps4/
 *
 * Requires:
 *   - OpenOrbis PS4 Toolchain (or compatible PS4 homebrew SDK)
 *   - libcurl (bundled with OpenOrbis)
 *   - sqlite3 (bundled with OpenOrbis)
 */

#include "ui.h"
#include "http.h"
#include "db.h"
#include "store.h"
#include "library.h"
#include "redeem.h"

#include <stdio.h>
#include <string.h>

/* ─────────────────────────────────────────────────────────────────────────
 * PS4 controller polling
 *
 * Replace the stub below with the real SDK call (e.g. scePadReadState).
 * Returns a bitmask of pressed buttons using the BTN_* constants from ui.h.
 * ───────────────────────────────────────────────────────────────────────── */

__attribute__((weak))
unsigned int platform_read_buttons(void)
{
    return 0;   /* stub – override with real scePadReadState wrapper */
}

/* ─────────────────────────────────────────────────────────────────────────
 * Settings section (minimal – can be expanded later)
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    int selected;
} SettingsState;

static void settings_draw(SettingsState *s)
{
    ui_draw_topbar(SECTION_SETTINGS);

    int x = UI_PADDING;
    int y = UI_TOPBAR_H + UI_PADDING;

    ui_draw_text(x, y, UI_COL_TEXT_BLUE, 26, "SETTINGS");
    y += 54;

    const char *items[] = {
        "API Endpoint: " ORBIS_API_BASE,
        "Library DB:   " LIBRARY_DB_PATH,
        "Version:      1.0.0",
    };
    int count = (int)(sizeof(items) / sizeof(items[0]));

    for (int i = 0; i < count; i++) {
        unsigned int bg = (i == s->selected) ? UI_COL_ACCENT : UI_COL_PANEL;
        ui_draw_rect(x, y + i * (UI_ITEM_H), UI_SCREEN_W - 2 * UI_PADDING,
                     UI_ITEM_H - 4, bg);
        ui_draw_text(x + 16, y + i * UI_ITEM_H + 18, UI_COL_TEXT, 18,
                     "%s", items[i]);
    }

    ui_draw_hints("D-Pad: Navigate");
    (void)s;
}

static int settings_handle_input(SettingsState *s, unsigned int buttons)
{
    if (buttons & BTN_UP)   { if (s->selected > 0) s->selected--; return 1; }
    if (buttons & BTN_DOWN) { if (s->selected < 2) s->selected++; return 1; }
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Application state
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    AppSection     section;
    StoreState     store;
    LibraryState   library;
    RedeemSection  redeem;
    SettingsState  settings;
    Notification   notification;

    /* Debounce: hold last buttons to detect rising edge */
    unsigned int   prev_buttons;
} AppState;

/* ─────────────────────────────────────────────────────────────────────────
 * Section switching (L1 / R1 cycle through tabs)
 * ───────────────────────────────────────────────────────────────────────── */

static void switch_section(AppState *app, AppSection next)
{
    if (next == app->section) return;
    app->section = next;

    /* Reload library whenever we enter it so counts stay fresh */
    if (next == SECTION_LIBRARY) {
        library_reload(&app->library);
    }
    /* Re-open the OSK whenever we enter Redeem */
    if (next == SECTION_REDEEM) {
        redeem_init(&app->redeem);
    }
}

static void handle_tab_input(AppState *app, unsigned int pressed)
{
    if (pressed & BTN_L1) {
        int next = ((int)app->section - 1 + SECTION_COUNT) % SECTION_COUNT;
        switch_section(app, (AppSection)next);
    }
    if (pressed & BTN_R1) {
        int next = ((int)app->section + 1) % SECTION_COUNT;
        switch_section(app, (AppSection)next);
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Main
 * ───────────────────────────────────────────────────────────────────────── */

int main(void)
{
    /* ── Initialise subsystems ── */
    if (ui_init() != 0) {
        fprintf(stderr, "[orbis-store] Failed to initialise UI\n");
        return 1;
    }

    if (http_init() != 0) {
        fprintf(stderr, "[orbis-store] Failed to initialise HTTP\n");
        ui_shutdown();
        return 1;
    }

    if (db_open() != 0) {
        fprintf(stderr, "[orbis-store] Failed to open library database\n");
        http_cleanup();
        ui_shutdown();
        return 1;
    }

    /* ── Application state ── */
    AppState app;
    memset(&app, 0, sizeof(app));
    app.section = SECTION_STORE;

    store_init(&app.store);
    library_init(&app.library);
    redeem_init(&app.redeem);

    /* ── Main loop ── */
    while (1) {
        unsigned int buttons = platform_read_buttons();
        unsigned int pressed = buttons & ~app.prev_buttons;   /* rising edge */
        app.prev_buttons = buttons;

        /* Tab switching */
        handle_tab_input(&app, pressed);

        /* Dispatch input to the active section */
        switch (app.section) {
        case SECTION_STORE:
            store_handle_input(&app.store, pressed);
            break;
        case SECTION_LIBRARY:
            library_handle_input(&app.library, pressed);
            break;
        case SECTION_REDEEM:
            redeem_tick(&app.redeem);
            redeem_handle_input(&app.redeem, pressed);
            break;
        case SECTION_SETTINGS:
            settings_handle_input(&app.settings, pressed);
            break;
        default:
            break;
        }

        /* ── Render ── */
        ui_begin_frame();

        switch (app.section) {
        case SECTION_STORE:    store_draw(&app.store);          break;
        case SECTION_LIBRARY:  library_draw(&app.library);      break;
        case SECTION_REDEEM:   redeem_draw(&app.redeem);        break;
        case SECTION_SETTINGS: settings_draw(&app.settings);    break;
        default: break;
        }

        notify_draw(&app.notification);

        ui_end_frame();
    }

    /* ── Clean up (never reached in practice on PS4) ── */
    library_free(&app.library);
    db_close();
    http_cleanup();
    ui_shutdown();
    return 0;
}
