#include "library.h"
#include "ui.h"
#include "db.h"
#include "installer.h"

#include <stdlib.h>
#include <string.h>

static const char *ACTION_LABELS[LIB_ACTION_COUNT] = {
    "",         /* NONE – never shown */
    "Open",
    "Reinstall",
    "Update",
    "Delete",
};

/* ─────────────────────────────────────────────────────────────────────────
 * Helpers
 * ───────────────────────────────────────────────────────────────────────── */

static void clamp_scroll(LibraryState *s)
{
    if (s->selected < s->scroll_offset)
        s->scroll_offset = s->selected;
    if (s->selected >= s->scroll_offset + UI_MAX_VISIBLE_ITEMS)
        s->scroll_offset = s->selected - UI_MAX_VISIBLE_ITEMS + 1;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Life-cycle
 * ───────────────────────────────────────────────────────────────────────── */

void library_init(LibraryState *s)
{
    memset(s, 0, sizeof(*s));
    library_reload(s);
}

void library_free(LibraryState *s)
{
    free(s->entries);
    s->entries     = NULL;
    s->entry_count = 0;
}

void library_reload(LibraryState *s)
{
    free(s->entries);
    s->entries     = NULL;
    s->entry_count = 0;
    db_load_all(&s->entries, &s->entry_count);

    if (s->selected >= s->entry_count)
        s->selected = s->entry_count > 0 ? s->entry_count - 1 : 0;
    clamp_scroll(s);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Input
 * ───────────────────────────────────────────────────────────────────────── */

int library_handle_input(LibraryState *s, unsigned int buttons)
{
    /* ── install in progress ── */
    if (s->install.state == INSTALL_DOWNLOADING ||
        s->install.state == INSTALL_INSTALLING) {
        if (buttons & BTN_CIRCLE) {
            installer_cancel();
            s->install.state = INSTALL_IDLE;
        }
        return 1;
    }

    /* ── context menu open ── */
    if (s->menu_open) {
        if (buttons & BTN_UP) {
            int cur = (int)s->menu_cursor - 1;
            if (cur < 1) cur = LIB_ACTION_COUNT - 1;
            s->menu_cursor = (LibraryAction)cur;
            return 1;
        }
        if (buttons & BTN_DOWN) {
            int cur = (int)s->menu_cursor + 1;
            if (cur >= LIB_ACTION_COUNT) cur = 1;
            s->menu_cursor = (LibraryAction)cur;
            return 1;
        }
        if (buttons & BTN_CIRCLE) {
            s->menu_open = 0;
            return 1;
        }
        if (buttons & BTN_CROSS) {
            if (s->entry_count == 0 || s->selected >= s->entry_count) {
                s->menu_open = 0;
                return 1;
            }
            LibraryEntry *e = &s->entries[s->selected];

            switch (s->menu_cursor) {
            case LIB_ACTION_OPEN:
                /* Launch the app – platform-specific call */
                /* sceSystemServiceLoadExec(e->title_id, NULL); */
                break;

            case LIB_ACTION_REINSTALL:
            case LIB_ACTION_UPDATE:
                installer_begin(e);
                break;

            case LIB_ACTION_DELETE:
                db_delete(e->id);
                library_reload(s);
                break;

            default:
                break;
            }
            s->menu_open = 0;
            return 1;
        }
        return 1;   /* consume all input while menu is open */
    }

    /* ── normal list navigation ── */
    if (buttons & BTN_UP) {
        if (s->selected > 0) { s->selected--; clamp_scroll(s); }
        return 1;
    }
    if (buttons & BTN_DOWN) {
        if (s->selected < s->entry_count - 1) { s->selected++; clamp_scroll(s); }
        return 1;
    }
    if (buttons & BTN_CROSS) {
        if (s->entry_count > 0) {
            s->menu_open   = 1;
            s->menu_cursor = LIB_ACTION_OPEN;
        }
        return 1;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Draw
 * ───────────────────────────────────────────────────────────────────────── */

void library_draw(LibraryState *s)
{
    /* Poll ongoing install */
    if (s->install.state == INSTALL_DOWNLOADING ||
        s->install.state == INSTALL_INSTALLING) {
        installer_poll(&s->install);
    }

    ui_draw_topbar(SECTION_LIBRARY);

    int x = UI_PADDING;
    int y = UI_TOPBAR_H + UI_PADDING;
    int w = UI_SCREEN_W - 2 * UI_PADDING;

    ui_draw_text(x, y, UI_COL_TEXT_BLUE, 26, "LIBRARY  (%d)", s->entry_count);
    y += 44;

    if (s->entry_count == 0) {
        ui_draw_text(x, y, UI_COL_TEXT_DIM, 20,
                     "No apps in library. Use Redeem or install from Store.");
        goto draw_footer;
    }

    for (int i = s->scroll_offset;
         i < s->entry_count && i < s->scroll_offset + UI_MAX_VISIBLE_ITEMS;
         i++) {
        LibraryEntry *e   = &s->entries[i];
        int           iy  = y + (i - s->scroll_offset) * UI_ITEM_H;
        int           sel = (i == s->selected);

        unsigned int bg     = sel ? UI_COL_ACCENT : UI_COL_PANEL;
        unsigned int border = sel ? UI_COL_ACCENT_HOV : UI_COL_BORDER;

        ui_draw_rect(x, iy, w, UI_ITEM_H - 4, bg);
        ui_draw_rect_outline(x, iy, w, UI_ITEM_H - 4, border, 1);

        ui_draw_text(x + 16, iy + 8,  UI_COL_TEXT,     18, "%s", e->name);
        ui_draw_text(x + 16, iy + 32, UI_COL_TEXT_DIM,  14,
                     "v%s  –  added %s", e->version, ctime(&e->added_at));
    }

    /* Context menu overlay */
    if (s->menu_open && s->entry_count > 0) {
        int mw = 260, mh = (LIB_ACTION_COUNT - 1) * 56 + 20;
        int mx = (UI_SCREEN_W - mw) / 2;
        int my = (UI_SCREEN_H - mh) / 2;

        ui_draw_rect(mx, my, mw, mh, UI_COL_PANEL);
        ui_draw_rect_outline(mx, my, mw, mh, UI_COL_BORDER, 2);

        for (int a = LIB_ACTION_OPEN; a < LIB_ACTION_COUNT; a++) {
            int ay  = my + 10 + (a - 1) * 56;
            int sel = (s->menu_cursor == (LibraryAction)a);

            if (sel) ui_draw_rect(mx + 8, ay, mw - 16, 48, UI_COL_ACCENT);

            ui_draw_text(mx + 24, ay + 12, UI_COL_TEXT, 20,
                         "%s", ACTION_LABELS[a]);
        }
    }

draw_footer:
    installer_draw(&s->install);

    if (s->install.state == INSTALL_DOWNLOADING ||
        s->install.state == INSTALL_INSTALLING) {
        ui_draw_hints("O: Cancel download");
    } else if (s->menu_open) {
        ui_draw_hints("D-Pad: Navigate   X: Confirm   O: Close menu");
    } else {
        ui_draw_hints("D-Pad: Navigate   X: Open menu");
    }
}
