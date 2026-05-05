#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define DB_TEXT_PATH  LIBRARY_DB_PATH ".txt"

typedef struct {
    int open;
    LibraryEntry *entries;
    int count;
    int capacity;
    int next_id;
} DbState;

static DbState g_db;

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void ensure_dir(const char *path)
{
    /* Strip filename and create every parent directory */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static void ensure_capacity(int wanted)
{
    if (wanted <= g_db.capacity) return;

    int new_cap = g_db.capacity == 0 ? 16 : g_db.capacity * 2;
    while (new_cap < wanted) new_cap *= 2;

    LibraryEntry *tmp = realloc(g_db.entries, (size_t)new_cap * sizeof(LibraryEntry));
    if (!tmp) {
        fprintf(stderr, "[db] Out of memory while growing database\n");
        return;
    }
    g_db.entries = tmp;
    g_db.capacity = new_cap;
}

static int save_all(void)
{
    FILE *fp = fopen(DB_TEXT_PATH, "w");
    if (!fp) {
        fprintf(stderr, "[db] Cannot write %s: %s\n", DB_TEXT_PATH, strerror(errno));
        return -1;
    }

    for (int i = 0; i < g_db.count; i++) {
        const LibraryEntry *e = &g_db.entries[i];
        fprintf(fp,
                "%d\t%s\t%s\t%s\t%s\t%s\t%ld\n",
                e->id,
                e->name,
                e->version,
                e->description,
                e->pkg_url,
                e->icon_url,
                (long)e->added_at);
    }
    fclose(fp);
    return 0;
}

static void trim_newline(char *s)
{
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int load_all(void)
{
    FILE *fp = fopen(DB_TEXT_PATH, "r");
    if (!fp) {
        if (errno == ENOENT) return 0;
        fprintf(stderr, "[db] Cannot read %s: %s\n", DB_TEXT_PATH, strerror(errno));
        return -1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        if (line[0] == '\0') continue;

        char *fields[7] = {0};
        int field_count = 0;
        char *saveptr = NULL;
        char *tok = strtok_r(line, "\t", &saveptr);
        while (tok && field_count < 7) {
            fields[field_count++] = tok;
            tok = strtok_r(NULL, "\t", &saveptr);
        }
        if (field_count < 7) continue;

        ensure_capacity(g_db.count + 1);
        if (g_db.count >= g_db.capacity) {
            fclose(fp);
            return -1;
        }

        LibraryEntry *e = &g_db.entries[g_db.count++];
        memset(e, 0, sizeof(*e));

        e->id = atoi(fields[0]);
        safe_copy(e->name, sizeof(e->name), fields[1]);
        safe_copy(e->version, sizeof(e->version), fields[2]);
        safe_copy(e->description, sizeof(e->description), fields[3]);
        safe_copy(e->pkg_url, sizeof(e->pkg_url), fields[4]);
        safe_copy(e->icon_url, sizeof(e->icon_url), fields[5]);
        e->added_at = (time_t)atol(fields[6]);

        if (e->id >= g_db.next_id) g_db.next_id = e->id + 1;
    }

    fclose(fp);
    return 0;
}

/* ── life-cycle ──────────────────────────────────────────────────────────── */

int db_open(void)
{
    if (g_db.open) return 0;

    memset(&g_db, 0, sizeof(g_db));
    g_db.open = 1;
    g_db.next_id = 1;

    ensure_dir(DB_TEXT_PATH);

    if (load_all() != 0) {
        free(g_db.entries);
        memset(&g_db, 0, sizeof(g_db));
        return -1;
    }
    return 0;
}

void db_close(void)
{
    if (!g_db.open) return;
    save_all();
    free(g_db.entries);
    memset(&g_db, 0, sizeof(g_db));
}

/* ── CRUD ────────────────────────────────────────────────────────────────── */

int db_insert(LibraryEntry *entry)
{
    if (!g_db.open || !entry) return -1;

    ensure_capacity(g_db.count + 1);
    if (g_db.count >= g_db.capacity) return -1;

    LibraryEntry *dst = &g_db.entries[g_db.count++];
    *dst = *entry;
    dst->id = g_db.next_id++;
    entry->id = dst->id;
    return save_all();
}

int db_load_all(LibraryEntry **out_entries, int *out_count)
{
    if (!g_db.open || !out_entries || !out_count) return -1;

    *out_entries = NULL;
    *out_count   = 0;

    if (g_db.count == 0) {
        return 0;
    }

    LibraryEntry *arr = malloc((size_t)g_db.count * sizeof(LibraryEntry));
    if (!arr) return -1;

    int out_i = 0;
    for (int i = g_db.count - 1; i >= 0; i--) {
        arr[out_i++] = g_db.entries[i];
    }

    *out_entries = arr;
    *out_count = out_i;
    return 0;
}

int db_delete(int id)
{
    if (!g_db.open) return -1;

    int found = -1;
    for (int i = 0; i < g_db.count; i++) {
        if (g_db.entries[i].id == id) {
            found = i;
            break;
        }
    }
    if (found < 0) return -1;

    for (int i = found; i < g_db.count - 1; i++) {
        g_db.entries[i] = g_db.entries[i + 1];
    }
    g_db.count--;
    return save_all();
}

int db_exists_by_url(const char *pkg_url)
{
    if (!g_db.open || !pkg_url) return -1;

    for (int i = 0; i < g_db.count; i++) {
        if (strncmp(g_db.entries[i].pkg_url, pkg_url, sizeof(g_db.entries[i].pkg_url)) == 0)
            return 1;
    }
    return 0;
}
