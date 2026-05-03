#include "store.h"
#include "ui.h"
#include "db.h"
#include "installer.h"

#include <string.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Built-in homebrew catalogue
 *
 * Add entries here to ship pre-defined apps inside the store.
 * Keep pkg_url pointing to real GitHub release assets.
 * ───────────────────────────────────────────────────────────────────────── */

static const StoreItem BUILT_IN_ITEMS[] = {
    {
        "GoldHEN",
        "2.4b16.2",
        "PS4 exploit and homebrew enabler by SiSTRo.",
        "https://github.com/GoldHEN/GoldHEN/releases/download/2.4b16.2/goldhen_2.4b16.2.pkg",
        "https://avatars.githubusercontent.com/GoldHEN",
    },
    {
        "PKG Linker",
        "1.6",
        "Install PKG files over the network from your PC.",
        "https://github.com/flatz/pkg_sender/releases/download/v1.6/pkg_sender_v1.6.pkg",
        "",
    },
    {
        "PS4 Xplorer",
        "1.1",
        "Full-featured file manager for PS4 internal and USB storage.",
        "https://github.com/cy33hc/ps4-xplorer/releases/latest/download/PS4Xplorer.pkg",
        "https://avatars.githubusercontent.com/cy33hc",
    },
    {
        "RetroArch",
        "1.17",
        "Multi-system emulator frontend supporting 50+ cores.",
        "https://buildbot.libretro.com/nightly/playstation/ps4/RetroArch.pkg",
        "https://www.libretro.com/wp-content/uploads/2016/11/retroarch-512.png",
    },
    {
        "OpenOrbis Toolchain",
        "0.5.3",
        "Open-source PS4 homebrew development toolchain.",
        "https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/latest/download/openorbis.pkg",
        "https://avatars.githubusercontent.com/OpenOrbis",
    },
};

#define BUILT_IN_COUNT  ((int)(sizeof(BUILT_IN_ITEMS) / sizeof(BUILT_IN_ITEMS[0])))

/* ─────────────────────────────────────────────────────────────────────────
 * Helpers
 * ───────────────────────────────────────────────────────────────────────── */

static void clamp_scroll(StoreState *s)
{
    if (s->selected < s->scroll_offset)
        s->scroll_offset = s->selected;
    if (s->selected >= s->scroll_offset + UI_MAX_VISIBLE_ITEMS)
        s->scroll_offset = s->selected - UI_MAX_VISIBLE_ITEMS + 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

void store_init(StoreState *s)
{
    memset(s, 0, sizeof(*s));

    int n = BUILT_IN_COUNT < STORE_MAX_ITEMS ? BUILT_IN_COUNT : STORE_MAX_ITEMS;
    for (int i = 0; i < n; i++) {
        s->items[i] = BUILT_IN_ITEMS[i];
    }
    s->item_count = n;
}

int store_handle_input(StoreState *s, unsigned int buttons)
{
    /* Block navigation during an install */
    if (s->install.state == INSTALL_DOWNLOADING ||
        s->install.state == INSTALL_INSTALLING) {
        if (buttons & BTN_CIRCLE) {
            installer_cancel();
            s->install.state = INSTALL_IDLE;
        }
        return 1;
    }

    if (buttons & BTN_UP) {
        if (s->selected > 0) { s->selected--; clamp_scroll(s); }
        return 1;
    }
    if (buttons & BTN_DOWN) {
        if (s->selected < s->item_count - 1) { s->selected++; clamp_scroll(s); }
        return 1;
    }

    if (buttons & BTN_CROSS) {
        /* Install selected item */
        const StoreItem *item = &s->items[s->selected];

        LibraryEntry entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.name,        item->name,        sizeof(entry.name) - 1);
        strncpy(entry.version,     item->version,     sizeof(entry.version) - 1);
        strncpy(entry.description, item->description, sizeof(entry.description) - 1);
        strncpy(entry.pkg_url,     item->pkg_url,     sizeof(entry.pkg_url) - 1);
        strncpy(entry.icon_url,    item->icon_url,    sizeof(entry.icon_url) - 1);
        entry.added_at = time(NULL);

        if (installer_begin(&entry) == 0) {
            /* Also add to library so it shows up there */
            if (db_exists_by_url(entry.pkg_url) == 0) {
                db_insert(&entry);
            }
        }
        return 1;
    }

    return 0;
}

void store_draw(StoreState *s)
{
    /* Poll install if in progress */
    if (s->install.state == INSTALL_DOWNLOADING ||
        s->install.state == INSTALL_INSTALLING) {
        installer_poll(&s->install);
    }

    ui_draw_topbar(SECTION_STORE);

    int x = UI_PADDING;
    int y = UI_TOPBAR_H + UI_PADDING;
    int w = UI_SCREEN_W - 2 * UI_PADDING;

    ui_draw_text(x, y, UI_COL_TEXT_BLUE, 26, "STORE");
    y += 44;

    if (s->item_count == 0) {
        ui_draw_text(x, y, UI_COL_TEXT_DIM, 20, "No items available.");
        goto draw_footer;
    }

    for (int i = s->scroll_offset;
         i < s->item_count && i < s->scroll_offset + UI_MAX_VISIBLE_ITEMS;
         i++) {

        const StoreItem *item = &s->items[i];
        int iy = y + (i - s->scroll_offset) * UI_ITEM_H;
        int selected = (i == s->selected);

        unsigned int bg     = selected ? UI_COL_ACCENT : UI_COL_PANEL;
        unsigned int border = selected ? UI_COL_ACCENT_HOV : UI_COL_BORDER;

        ui_draw_rect(x, iy, w, UI_ITEM_H - 4, bg);
        ui_draw_rect_outline(x, iy, w, UI_ITEM_H - 4, border, 1);

        ui_draw_text(x + 16, iy + 8,  UI_COL_TEXT, 18, "%s", item->name);
        ui_draw_text(x + 16, iy + 32, UI_COL_TEXT_DIM, 14, "v%s – %s",
                     item->version, item->description);
    }

    /* Scroll indicator */
    if (s->item_count > UI_MAX_VISIBLE_ITEMS) {
        ui_draw_text(w - 40, y, UI_COL_TEXT_DIM, 16,
                     "%d/%d", s->selected + 1, s->item_count);
    }

draw_footer:
    /* Detail panel for the selected item */
    if (s->item_count > 0) {
        const StoreItem *sel = &s->items[s->selected];
        int dy = UI_TOPBAR_H + UI_PADDING + UI_MAX_VISIBLE_ITEMS * UI_ITEM_H + 16;
        ui_draw_rect(x, dy, w, 80, UI_COL_PANEL);
        ui_draw_rect_outline(x, dy, w, 80, UI_COL_BORDER, 1);
        ui_draw_text(x + 16, dy + 10, UI_COL_TEXT_BLUE, 18, "%s  v%s",
                     sel->name, sel->version);
        ui_draw_text(x + 16, dy + 38, UI_COL_TEXT_DIM, 15, "%s",
                     sel->description);
    }

    installer_draw(&s->install);

    if (s->install.state == INSTALL_DOWNLOADING ||
        s->install.state == INSTALL_INSTALLING) {
        ui_draw_hints("O: Cancel download");
    } else {
        ui_draw_hints("D-Pad: Navigate   X: Install");
    }
}
