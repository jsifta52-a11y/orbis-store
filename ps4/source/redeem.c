#include "redeem.h"
#include "http.h"
#include "db.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Minimal JSON field extraction
 *
 * The API response is small and well-structured, so we use a lightweight
 * key-value extractor rather than pulling in a full JSON library.
 * ───────────────────────────────────────────────────────────────────────── */

/*
 * Extract the value for a string key from a flat JSON object.
 * Writes at most dest_size-1 bytes into dest and NUL-terminates it.
 * Returns 1 on success, 0 if the key was not found.
 */
static int json_get_str(const char *json, const char *key,
                        char *dest, int dest_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return 0;

    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;   /* skip opening quote */

    int i = 0;
    while (*p && *p != '"' && i < dest_size - 1) {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case '"':  dest[i++] = '"';  break;
            case '\\': dest[i++] = '\\'; break;
            case '/':  dest[i++] = '/';  break;
            case 'n':  dest[i++] = '\n'; break;
            case 'r':  dest[i++] = '\r'; break;
            case 't':  dest[i++] = '\t'; break;
            default:   dest[i++] = *p;   break;
            }
        } else {
            dest[i++] = *p;
        }
        p++;
    }
    dest[i] = '\0';
    return 1;
}

static int json_get_bool(const char *json, const char *key)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return strncmp(p, "true", 4) == 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Async API call
 * ───────────────────────────────────────────────────────────────────────── */

typedef struct {
    char  code[REDEEM_CODE_LEN + 1];
    char *response;   /* heap-allocated; NULL on failure */
    int   done;
} ApiTask;

static ApiTask   g_task;
static pthread_t g_thread;

static void *api_thread(void *arg)
{
    ApiTask *t = (ApiTask *)arg;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/redeem/%s", ORBIS_API_BASE, t->code);

    t->response = http_get(url);
    t->done     = 1;
    return NULL;
}

static void start_api_call(RedeemSection *r)
{
    memset(&g_task, 0, sizeof(g_task));
    strncpy(g_task.code, r->code, REDEEM_CODE_LEN);

    if (pthread_create(&g_thread, NULL, api_thread, &g_task) != 0) {
        r->state = REDEEM_STATE_ERROR;
        strncpy(r->error_msg, "Failed to start network request",
                sizeof(r->error_msg) - 1);
    } else {
        pthread_detach(g_thread);
        r->state = REDEEM_STATE_LOADING;
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

void redeem_init(RedeemSection *r)
{
    memset(r, 0, sizeof(*r));
    r->state = REDEEM_STATE_IDLE;
    osk_open(&r->osk, "Enter 8-character redeem code");
}

int redeem_handle_input(RedeemSection *r, unsigned int buttons)
{
    switch (r->state) {

    case REDEEM_STATE_IDLE:
        if (osk_handle_input(&r->osk, buttons)) {
            if (r->osk.confirmed) {
                /* Start the API call */
                strncpy(r->code, r->osk.buffer, REDEEM_CODE_LEN);
                start_api_call(r);
            } else if (r->osk.cancelled) {
                /* Re-open the keyboard for a fresh attempt */
                osk_open(&r->osk, "Enter 8-character redeem code");
            }
        }
        return 1;

    case REDEEM_STATE_LOADING:
        /* Nothing interactive while loading */
        return 1;

    case REDEEM_STATE_PREVIEW:
        if (buttons & BTN_CROSS) {
            /* Confirm: save to library */
            LibraryEntry entry;
            memset(&entry, 0, sizeof(entry));
            strncpy(entry.name,        r->name,        sizeof(entry.name) - 1);
            strncpy(entry.version,     r->version,     sizeof(entry.version) - 1);
            strncpy(entry.description, r->description, sizeof(entry.description) - 1);
            strncpy(entry.pkg_url,     r->pkg_url,     sizeof(entry.pkg_url) - 1);
            strncpy(entry.icon_url,    r->icon_url,    sizeof(entry.icon_url) - 1);
            entry.added_at = time(NULL);

            if (db_exists_by_url(entry.pkg_url) == 0) {
                db_insert(&entry);
            }
            r->state = REDEEM_STATE_DONE;
            return 1;
        }
        if (buttons & BTN_CIRCLE) {
            /* Cancel: go back to keyboard */
            r->state = REDEEM_STATE_IDLE;
            osk_open(&r->osk, "Enter 8-character redeem code");
            return 1;
        }
        return 1;

    case REDEEM_STATE_DONE:
    case REDEEM_STATE_ERROR:
        if (buttons & (BTN_CROSS | BTN_CIRCLE)) {
            /* Reset for a new redeem */
            r->state = REDEEM_STATE_IDLE;
            osk_open(&r->osk, "Enter 8-character redeem code");
            return 1;
        }
        return 1;

    default:
        return 0;
    }
}

void redeem_tick(RedeemSection *r)
{
    if (r->state != REDEEM_STATE_LOADING) return;
    if (!g_task.done) return;

    /* API call completed */
    const char *resp = g_task.response;

    if (!resp) {
        r->state = REDEEM_STATE_ERROR;
        strncpy(r->error_msg, "Network error – check your connection",
                sizeof(r->error_msg) - 1);
        return;
    }

    if (!json_get_bool(resp, "valid")) {
        char err[256] = "Invalid or expired code";
        json_get_str(resp, "error", err, sizeof(err));
        r->state = REDEEM_STATE_ERROR;
        strncpy(r->error_msg, err, sizeof(r->error_msg) - 1);
        free(g_task.response);
        g_task.response = NULL;
        return;
    }

    json_get_str(resp, "name",        r->name,        sizeof(r->name));
    json_get_str(resp, "version",     r->version,     sizeof(r->version));
    json_get_str(resp, "description", r->description, sizeof(r->description));
    json_get_str(resp, "pkg_url",     r->pkg_url,     sizeof(r->pkg_url));
    json_get_str(resp, "icon_url",    r->icon_url,    sizeof(r->icon_url));

    free(g_task.response);
    g_task.response = NULL;

    r->state = REDEEM_STATE_PREVIEW;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Draw
 * ───────────────────────────────────────────────────────────────────────── */

void redeem_draw(RedeemSection *r)
{
    ui_draw_topbar(SECTION_REDEEM);

    int x = UI_PADDING;
    int y = UI_TOPBAR_H + UI_PADDING;
    int w = UI_SCREEN_W - 2 * UI_PADDING;

    ui_draw_text(x, y, UI_COL_TEXT_BLUE, 26, "REDEEM");
    y += 54;

    switch (r->state) {

    case REDEEM_STATE_IDLE:
        ui_draw_text(x, y, UI_COL_TEXT_DIM, 18,
                     "Enter your 8-character redeem code below.");
        y += 36;
        /* Render the on-screen keyboard */
        osk_draw(&r->osk);
        ui_draw_hints("D-Pad: Navigate   X: Select key   O: Cancel");
        break;

    case REDEEM_STATE_LOADING:
        ui_draw_text(x, y, UI_COL_TEXT, 22, "Contacting server…");
        ui_draw_hints("");
        break;

    case REDEEM_STATE_PREVIEW: {
        /* App metadata card */
        int cx = x, cy = y;
        int cw = w, ch = 260;

        ui_draw_rect(cx, cy, cw, ch, UI_COL_PANEL);
        ui_draw_rect_outline(cx, cy, cw, ch, UI_COL_BORDER, 2);

        ui_draw_text(cx + 20, cy + 20,  UI_COL_TEXT_BLUE, 24, "%s", r->name);
        ui_draw_text(cx + 20, cy + 56,  UI_COL_TEXT_DIM,  18,
                     "Version: %s", r->version);
        if (r->description[0]) {
            ui_draw_text(cx + 20, cy + 90, UI_COL_TEXT_DIM, 16, "%s",
                         r->description);
        }
        if (r->pkg_url[0]) {
            ui_draw_text(cx + 20, cy + 190, UI_COL_TEXT_DIM, 14,
                         "PKG: %.80s", r->pkg_url);
        }

        ui_draw_text(cx + 20, cy + ch + 20, UI_COL_TEXT, 18,
                     "Press X to save to Library, O to cancel.");

        ui_draw_hints("X: Save to Library   O: Cancel");
        break;
    }

    case REDEEM_STATE_DONE:
        ui_draw_text(x, y,      UI_COL_SUCCESS, 22, "✓ Saved to Library!");
        ui_draw_text(x, y + 40, UI_COL_TEXT_DIM, 18,
                     "Go to Library to install the app.");
        ui_draw_text(x, y + 80, UI_COL_TEXT_DIM, 16,
                     "Press X or O to redeem another code.");
        ui_draw_hints("X / O: Redeem another code");
        break;

    case REDEEM_STATE_ERROR:
        ui_draw_text(x, y,      UI_COL_ERROR,   22, "Error");
        ui_draw_text(x, y + 40, UI_COL_TEXT_DIM, 18, "%s", r->error_msg);
        ui_draw_text(x, y + 80, UI_COL_TEXT_DIM, 16,
                     "Press X or O to try again.");
        ui_draw_hints("X / O: Try again");
        break;

    default:
        break;
    }
}
