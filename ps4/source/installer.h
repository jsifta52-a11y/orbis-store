#pragma once
#include "db.h"

/* ── installer state ─────────────────────────────────────────────────────── */

typedef enum {
    INSTALL_IDLE = 0,
    INSTALL_DOWNLOADING,
    INSTALL_INSTALLING,
    INSTALL_DONE,
    INSTALL_ERROR
} InstallState;

typedef struct {
    InstallState state;
    char         app_name[128];
    long         bytes_downloaded;
    long         total_bytes;
    int          percent;          /* 0-100 */
    char         error_msg[256];
} InstallProgress;

/*
 * Begin a non-blocking installation of the given library entry.
 *
 * The install runs asynchronously; call installer_poll() every frame to check
 * status and installer_draw() to render the progress overlay.
 *
 * Returns 0 if the install was started, -1 if it could not be started
 * (e.g. another install is already in progress).
 */
int installer_begin(const LibraryEntry *entry);

/*
 * Poll the current install state.  Call every frame while
 * progress->state == INSTALL_DOWNLOADING or INSTALL_INSTALLING.
 */
void installer_poll(InstallProgress *progress);

/* Render the download/install progress overlay. */
void installer_draw(const InstallProgress *progress);

/* Cancel an in-progress install and clean up temporary files. */
void installer_cancel(void);
