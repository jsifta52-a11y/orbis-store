#include "http.h"

#include <stdio.h>
#include <stdlib.h>

int http_init(void)
{
    return 0;
}

void http_cleanup(void)
{
    /* No global resources in the fallback implementation. */
}

char *http_get(const char *url)
{
    (void)url;
    return NULL;
}

char *http_post_json(const char *url, const char *body)
{
    (void)url;
    (void)body;
    return NULL;
}

int http_download_file(const char *url,
                       const char *dest_path,
                       http_progress_cb cb,
                       void *userdata)
{
    (void)url;
    (void)dest_path;
    if (cb) cb(0, 0, userdata);
    return -1;
}
