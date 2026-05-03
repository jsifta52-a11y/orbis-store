#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* ── internal write buffer ───────────────────────────────────────────────── */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} WriteBuffer;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    WriteBuffer *buf = (WriteBuffer *)userdata;
    size_t incoming = size * nmemb;

    if (buf->len + incoming + 1 > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 4096 : buf->cap * 2;
        while (new_cap < buf->len + incoming + 1) new_cap *= 2;
        if (new_cap > HTTP_RESPONSE_MAX) {
            /* Response too large – abort */
            return 0;
        }
        char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->cap  = new_cap;
    }

    memcpy(buf->data + buf->len, ptr, incoming);
    buf->len += incoming;
    buf->data[buf->len] = '\0';
    return incoming;
}

/* ── progress wrapper ────────────────────────────────────────────────────── */

typedef struct {
    http_progress_cb  cb;
    void             *userdata;
} ProgressCtx;

static int progress_cb_wrapper(void *clientp,
                                curl_off_t dltotal, curl_off_t dlnow,
                                curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    ProgressCtx *ctx = (ProgressCtx *)clientp;
    if (ctx->cb) ctx->cb((long)dlnow, (long)dltotal, ctx->userdata);
    return 0;
}

/* ── public API ──────────────────────────────────────────────────────────── */

int http_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 0 : -1;
}

void http_cleanup(void)
{
    curl_global_cleanup();
}

/* Shared CURL setup used by all requests */
static CURL *make_curl(WriteBuffer *buf)
{
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       (long)HTTP_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "orbis-store/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    return curl;
}

char *http_get(const char *url)
{
    WriteBuffer buf = {0};
    CURL *curl = make_curl(&buf);
    if (!curl) return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;   /* caller must free() */
}

char *http_post_json(const char *url, const char *body)
{
    WriteBuffer buf = {0};
    CURL *curl = make_curl(&buf);
    if (!curl) return NULL;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     body ? body : "{}");

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

int http_download_file(const char *url,
                       const char *dest_path,
                       http_progress_cb cb,
                       void *userdata)
{
    FILE *fp = fopen(dest_path, "wb");
    if (!fp) return -1;

    CURL *curl = curl_easy_init();
    if (!curl) { fclose(fp); return -1; }

    ProgressCtx pctx = { cb, userdata };

    curl_easy_setopt(curl, CURLOPT_URL,              url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,    NULL);   /* use default fwrite */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,        fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,   1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,          0L);     /* no timeout for downloads */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,   1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,   2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,        "orbis-store/1.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb_wrapper);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     &pctx);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (rc != CURLE_OK) {
        remove(dest_path);
        return -1;
    }
    return 0;
}
