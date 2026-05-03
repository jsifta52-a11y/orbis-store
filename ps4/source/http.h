#pragma once
#include <stddef.h>

/* Maximum sizes for HTTP responses */
#define HTTP_RESPONSE_MAX  (256 * 1024)   /* 256 KiB */
#define HTTP_TIMEOUT_SEC   30

/*
 * Initialise / clean up the HTTP subsystem (wraps curl_global_init/cleanup).
 * Call http_init() once at startup and http_cleanup() before exit.
 */
int  http_init(void);
void http_cleanup(void);

/*
 * Perform a synchronous HTTP GET.
 *
 * On success returns a heap-allocated, NUL-terminated string that the caller
 * must free().  On failure returns NULL.
 */
char *http_get(const char *url);

/*
 * Perform a synchronous HTTP POST with a JSON body.
 *
 * body   – NUL-terminated JSON string (may be NULL for an empty body).
 *
 * On success returns a heap-allocated, NUL-terminated string that the caller
 * must free().  On failure returns NULL.
 */
char *http_post_json(const char *url, const char *body);

/*
 * Download a URL to a local file path, reporting progress via an optional
 * callback.  progress_cb receives bytes_downloaded and total_bytes; it may be
 * NULL.  Returns 0 on success, -1 on failure.
 */
typedef void (*http_progress_cb)(long bytes_downloaded, long total_bytes, void *userdata);

int http_download_file(const char *url,
                       const char *dest_path,
                       http_progress_cb cb,
                       void *userdata);
