#include "installer.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

/* PS4 SDK headers */
#include <sceBgft.h>

/* ─────────────────────────────────────────────────────────────────────────
 * PS4 PKG installer via BGFT (Background File Transfer) service
 *
 * Uses sceBgft API to download and install .pkg files via the PS4
 * system package manager. This is the proper way to install apps
 * on modern PS4 firmware versions.
 * ───────────────────────────────────────────────────────────────────────── */

__attribute__((weak))
int ps4_install_pkg(const char *pkg_url, int *out_task_id)
{
    if (!pkg_url || !out_task_id) return -1;

    int rc;
    static unsigned char bgft_heap[1024 * 1024];
    OrbisBgftInitParams bgft_init;

    memset(&bgft_init, 0, sizeof(bgft_init));
    bgft_init.heap = bgft_heap;
    bgft_init.heapSize = sizeof(bgft_heap);

    rc = sceBgftServiceIntInit(&bgft_init);
    if (rc < 0) {
        fprintf(stderr, "[installer] sceBgftServiceIntInit failed: 0x%08x\n", rc);
        return -1;
    }

    OrbisBgftDownloadParamEx params;
    memset(&params, 0, sizeof(params));
    params.params.userId = 0;
    params.params.entitlementType = 0;
    params.params.id = "ORBS00001";
    params.params.contentUrl = pkg_url;
    params.params.contentName = "Orbis Store Package";
    params.params.iconPath = "";
    params.params.option = ORBIS_BGFT_TASK_OPT_NONE;
    params.slot = 0;

    OrbisBgftTaskId task_id = -1;
    rc = sceBgftServiceIntDownloadRegisterTaskByStorageEx(&params, &task_id);
    if (rc < 0 || task_id < 0) {
        fprintf(stderr, "[installer] RegisterTask failed: 0x%08x\n", rc);
        sceBgftServiceIntTerm();
        return -1;
    }

    *out_task_id = task_id;

    /* Start the download */
    rc = sceBgftServiceDownloadStartTask(task_id);
    if (rc < 0) {
        fprintf(stderr, "[installer] sceBgftServiceDownloadStartTask failed: 0x%08x\n", rc);
        sceBgftServiceIntTerm();
        return -1;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal state (shared between the install thread and the main thread)
 * ───────────────────────────────────────────────────────────────────────── */

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

static void *install_thread(void *arg)
{
    InstallArgs *args = (InstallArgs *)arg;
    int task_id = -1;

    /* ── BGFT install ── */
    pthread_mutex_lock(&g_mutex);
    g_progress.state = INSTALL_INSTALLING;
    pthread_mutex_unlock(&g_mutex);

    if (ps4_install_pkg(args->pkg_url, &task_id) != 0) {
        pthread_mutex_lock(&g_mutex);
        g_progress.state = INSTALL_ERROR;
        snprintf(g_progress.error_msg, sizeof(g_progress.error_msg),
                 "PKG installation failed");
        pthread_mutex_unlock(&g_mutex);
        free(args);
        g_thread_running = 0;
        return NULL;
    }
    
    /* Monitor BGFT progress */
    OrbisBgftTaskProgress status;
    memset(&status, 0, sizeof(status));

    int attempts = 0;
    const int max_attempts = 1800;  /* ~30 min timeout at 1 update/sec */

    while (attempts < max_attempts) {
        if (!g_thread_running) {
            sceBgftServiceDownloadStopTask(task_id);
            pthread_mutex_lock(&g_mutex);
            g_progress.state = INSTALL_IDLE;
            pthread_mutex_unlock(&g_mutex);
            break;
        }

        int rc = sceBgftServiceIntDownloadGetProgress(task_id, &status);
        if (rc == 0) {
            pthread_mutex_lock(&g_mutex);
            g_progress.percent = (status.transferredTotal * 100) / (status.lengthTotal > 0 ? status.lengthTotal : 1);
            g_progress.bytes_downloaded = status.transferredTotal;
            g_progress.total_bytes = status.lengthTotal;

            if (status.lengthTotal > 0 && status.transferredTotal >= status.lengthTotal) {
                g_progress.state = INSTALL_DONE;
                g_progress.percent = 100;
                pthread_mutex_unlock(&g_mutex);
                break;
            }
            pthread_mutex_unlock(&g_mutex);
        }
        sleep(1);
        attempts++;
    }

    sceBgftServiceIntTerm();

    if (attempts >= max_attempts) {
        pthread_mutex_lock(&g_mutex);
        g_progress.state = INSTALL_ERROR;
        snprintf(g_progress.error_msg, sizeof(g_progress.error_msg),
                 "Installation timeout");
        pthread_mutex_unlock(&g_mutex);
    }

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
