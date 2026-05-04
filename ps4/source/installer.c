#include "installer.h"
#include "http.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

/* PS4 SDK headers */
#include <sceAppInstUtil.h>
#include <sceBgft.h>
#include <orbisFile.h>

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
    SceBgftInitParams bgft_init;
    
    /* Initialize BGFT service */
    memset(&bgft_init, 0, sizeof(bgft_init));
    bgft_init.heap_size = 512 * 1024;  /* 512 KB heap */
    bgft_init.app_id = "NPXS39041";     /* Orbis Store app ID */
    
    rc = sceBgftServiceInit(&bgft_init);
    if (rc < 0) {
        fprintf(stderr, "[installer] sceBgftServiceInit failed: 0x%08x\n", rc);
        return -1;
    }
    
    /* Register PKG download task */
    SceBgftServiceIntDownloadRegisterTaskByStorageEx params;
    memset(&params, 0, sizeof(params));
    strncpy(params.url, pkg_url, sizeof(params.url) - 1);
    params.option = 0;
    params.flags = SCEBGFT_SERVICE_INTDOWNLOAD_FLAGS_NONOPT;
    
    int task_id = sceBgftServiceIntDownloadRegisterTaskByStorageEx(&params);
    if (task_id < 0) {
        fprintf(stderr, "[installer] sceBgftServiceIntDownloadRegisterTaskByStorageEx failed: 0x%08x\n", task_id);
        sceBgftServiceTerm();
        return -1;
    }
    
    *out_task_id = task_id;
    
    /* Start the download */
    rc = sceBgftServiceDownloadStartTask(task_id);
    if (rc < 0) {
        fprintf(stderr, "[installer] sceBgftServiceDownloadStartTask failed: 0x%08x\n", rc);
        sceBgftServiceTerm();
        return -1;
    }
    
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────
 * Internal state (shared between the install thread and the main thread)
 * ───────────────────────────────────────────────────────────────────────── */

#define TMP_PKG_PATH  "/user/app/NPXS39041/sce_sys/download.pkg"

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
    int task_id = -1;

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

    /* ── Install via BGFT ── */
    pthread_mutex_lock(&g_mutex);
    g_progress.state = INSTALL_INSTALLING;
    g_progress.percent = 0;
    pthread_mutex_unlock(&g_mutex);

    if (ps4_install_pkg(args->pkg_url, &task_id) != 0) {
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
    
    /* Monitor BGFT progress */
    SceBgftServiceStatus status;
    int attempts = 0;
    const int max_attempts = 300;  /* ~5 min timeout at 1 update/sec */
    
    while (attempts < max_attempts) {
        int rc = sceBgftServiceGetStatus(task_id, &status);
        if (rc == 0) {
            pthread_mutex_lock(&g_mutex);
            g_progress.percent = (status.downloaded * 100) / (status.length > 0 ? status.length : 1);
            g_progress.bytes_downloaded = status.downloaded;
            g_progress.total_bytes = status.length;
            
            if (status.status == SCEBGFT_SERVICE_STATUS_COMPLETED) {
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

    remove(TMP_PKG_PATH);
    sceBgftServiceTerm();

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
