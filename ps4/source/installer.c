#include "installer.h"
#include "http.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ─────────────────────────────────────────────────────────────────────────
 * PS4 PKG installer service
 *
 * On a real PS4 homebrew build, trigger the package manager via:
 *   sceAppInstUtil / orbis_jbc_install_pkg / or a direct syscall.
 *
 * The stub below is replaced by the actual implementation for each SDK.
 * ───────────────────────────────────────────────────────────────────────── */

__attribute__((weak))
int ps4_install_pkg(const char *pkg_path)
{
    /* Stub: on a real device this would call the PS4 package installer. */
    printf("[installer] ps4_install_pkg(%s) – stub\n", pkg_path);
    (void)pkg_path;
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal state (shared between the install thread and the main thread)
 * ───────────────────────────────────────────────────────────────────────── */

#define TMP_PKG_PATH  "/data/orbis_store/tmp/download.pkg"

typedef struct {
    char pkg_url[512];
    char app_name[128];
} InstallArgs;

static pthread_t         g_thread;
static int               g_thread_running = 0;
static InstallProgress   g_progress;
static pthread_mutex_t   g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─────────────────────────────────────────────────────────────────────────
 * Progress callback – called by http_download_file from the worker thread
 * ───────────────────────────────────────────────────────────────────────── */

static void on_download_progress(long downloaded, long total, void *userdata)
{
    (void)userdata;
    pthread_mutex_lock(&g_mutex);
    g_progress.bytes_downloaded = downloaded;
    g_progress.total_bytes      = total;
    g_progress.percent          = (total > 0) ? (int)(downloaded * 100 / total) : 0;
    pthread_mutex_unlock(&g_mutex);
}

/* ─────────────────────────────────────────────────────────────────────────
 * Worker thread
 * ───────────────────────────────────────────────────────────────────────── */

static void *install_thread(void *arg)
{
    InstallArgs *args = (InstallArgs *)arg;

    /* ── Download ── */
    pthread_mutex_lock(&g_mutex);
    g_progress.state = INSTALL_DOWNLOADING;
    pthread_mutex_unlock(&g_mutex);

    if (http_download_file(args->pkg_url, TMP_PKG_PATH,
                           on_download_progress, NULL) != 0) {
        pthread_mutex_lock(&g_mutex);
        g_progress.state = INSTALL_ERROR;
        snprintf(g_progress.error_msg, sizeof(g_progress.error_msg),
                 "Download failed");
        pthread_mutex_unlock(&g_mutex);
        free(args);
        g_thread_running = 0;
        return NULL;
    }

    /* ── Install ── */
    pthread_mutex_lock(&g_mutex);
    g_progress.state   = INSTALL_INSTALLING;
    g_progress.percent = 0;
    pthread_mutex_unlock(&g_mutex);

    if (ps4_install_pkg(TMP_PKG_PATH) != 0) {
        pthread_mutex_lock(&g_mutex);
        g_progress.state = INSTALL_ERROR;
        snprintf(g_progress.error_msg, sizeof(g_progress.error_msg),
                 "PKG installation failed");
        pthread_mutex_unlock(&g_mutex);
        remove(TMP_PKG_PATH);
        free(args);
        g_thread_running = 0;
        return NULL;
    }

    remove(TMP_PKG_PATH);

    pthread_mutex_lock(&g_mutex);
    g_progress.state   = INSTALL_DONE;
    g_progress.percent = 100;
    pthread_mutex_unlock(&g_mutex);

    free(args);
    g_thread_running = 0;
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────────────────── */

int installer_begin(const LibraryEntry *entry)
{
    if (g_thread_running) return -1;   /* already installing */

    InstallArgs *args = malloc(sizeof(InstallArgs));
    if (!args) return -1;

    strncpy(args->pkg_url,  entry->pkg_url,  sizeof(args->pkg_url)  - 1);
    strncpy(args->app_name, entry->name,     sizeof(args->app_name) - 1);

    memset(&g_progress, 0, sizeof(g_progress));
    strncpy(g_progress.app_name, entry->name, sizeof(g_progress.app_name) - 1);
    g_progress.state = INSTALL_IDLE;

    g_thread_running = 1;
    if (pthread_create(&g_thread, NULL, install_thread, args) != 0) {
        g_thread_running = 0;
        free(args);
        return -1;
    }
    pthread_detach(g_thread);
    return 0;
}

void installer_poll(InstallProgress *progress)
{
    pthread_mutex_lock(&g_mutex);
    *progress = g_progress;
    pthread_mutex_unlock(&g_mutex);
}

void installer_cancel(void)
{
    /* Signal the thread to stop and wait briefly */
    g_thread_running = 0;
    remove(TMP_PKG_PATH);
    memset(&g_progress, 0, sizeof(g_progress));
}

/* ─────────────────────────────────────────────────────────────────────────
 * Progress overlay
 * ───────────────────────────────────────────────────────────────────────── */

void installer_draw(const InstallProgress *progress)
{
    if (progress->state == INSTALL_IDLE || progress->state == INSTALL_DONE)
        return;

    /* Dim the background */
    ui_draw_rect(0, 0, UI_SCREEN_W, UI_SCREEN_H, 0xAA000000);

    int pw = 700, ph = 200;
    int px = (UI_SCREEN_W - pw) / 2;
    int py = (UI_SCREEN_H - ph) / 2;

    ui_draw_rect(px, py, pw, ph, UI_COL_PANEL);
    ui_draw_rect_outline(px, py, pw, ph, UI_COL_BORDER, 2);

    /* Title */
    const char *title = (progress->state == INSTALL_INSTALLING)
                        ? "Installing…" : "Downloading…";
    ui_draw_text(px + 30, py + 24, UI_COL_TEXT_BLUE, 24, "%s", title);
    ui_draw_text(px + 30, py + 56, UI_COL_TEXT, 18, "%s", progress->app_name);

    /* Progress bar */
    int bx = px + 30, by = py + 100, bw = pw - 60, bh = 28;
    ui_draw_rect(bx, by, bw, bh, 0xFF071525);
    ui_draw_rect_outline(bx, by, bw, bh, UI_COL_BORDER, 2);
    if (progress->percent > 0) {
        int fill = bw * progress->percent / 100;
        ui_draw_rect(bx, by, fill, bh, UI_COL_ACCENT);
    }
    ui_draw_text(bx + bw / 2 - 20, by + 5, UI_COL_TEXT, 18, "%d%%", progress->percent);

    /* Byte counter */
    if (progress->state == INSTALL_DOWNLOADING && progress->total_bytes > 0) {
        ui_draw_text(px + 30, py + 148, UI_COL_TEXT_DIM, 16,
                     "%ld / %ld MB",
                     progress->bytes_downloaded / (1024 * 1024),
                     progress->total_bytes       / (1024 * 1024));
    }

    if (progress->state == INSTALL_ERROR) {
        ui_draw_text(px + 30, py + 148, UI_COL_ERROR, 16,
                     "Error: %s", progress->error_msg);
    }
}
