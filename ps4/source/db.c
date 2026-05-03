#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sqlite3.h>

static sqlite3 *g_db = NULL;

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

static int exec_simple(const char *sql)
{
    char *err = NULL;
    int rc = sqlite3_exec(g_db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[db] SQL error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

/* ── life-cycle ──────────────────────────────────────────────────────────── */

int db_open(void)
{
    if (g_db) return 0;   /* already open */

    ensure_dir(LIBRARY_DB_PATH);

    if (sqlite3_open(LIBRARY_DB_PATH, &g_db) != SQLITE_OK) {
        fprintf(stderr, "[db] Cannot open database: %s\n", sqlite3_errmsg(g_db));
        g_db = NULL;
        return -1;
    }

    const char *schema =
        "CREATE TABLE IF NOT EXISTS library ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name        TEXT    NOT NULL,"
        "  version     TEXT    NOT NULL DEFAULT '1.0.0',"
        "  description TEXT    NOT NULL DEFAULT '',"
        "  pkg_url     TEXT    NOT NULL,"
        "  icon_url    TEXT    NOT NULL DEFAULT '',"
        "  added_at    INTEGER NOT NULL"
        ");";

    return exec_simple(schema);
}

void db_close(void)
{
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

/* ── CRUD ────────────────────────────────────────────────────────────────── */

int db_insert(LibraryEntry *entry)
{
    if (!g_db || !entry) return -1;

    const char *sql =
        "INSERT INTO library (name, version, description, pkg_url, icon_url, added_at)"
        " VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;

    sqlite3_bind_text (stmt, 1, entry->name,        -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, entry->version,     -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 3, entry->description, -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 4, entry->pkg_url,     -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 5, entry->icon_url,    -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)entry->added_at);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) return -1;

    entry->id = (int)sqlite3_last_insert_rowid(g_db);
    return 0;
}

int db_load_all(LibraryEntry **out_entries, int *out_count)
{
    if (!g_db || !out_entries || !out_count) return -1;

    *out_entries = NULL;
    *out_count   = 0;

    const char *sql =
        "SELECT id, name, version, description, pkg_url, icon_url, added_at"
        " FROM library ORDER BY added_at DESC;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;

    /* First pass: count rows */
    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) count++;
    sqlite3_reset(stmt);

    if (count == 0) {
        sqlite3_finalize(stmt);
        return 0;
    }

    LibraryEntry *arr = calloc((size_t)count, sizeof(LibraryEntry));
    if (!arr) { sqlite3_finalize(stmt); return -1; }

    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < count) {
        LibraryEntry *e = &arr[i++];
        e->id       = sqlite3_column_int(stmt, 0);

        const char *s;
#define COPY(idx, field) \
        s = (const char *)sqlite3_column_text(stmt, (idx)); \
        if (s) strncpy(e->field, s, sizeof(e->field) - 1)

        COPY(1, name);
        COPY(2, version);
        COPY(3, description);
        COPY(4, pkg_url);
        COPY(5, icon_url);
#undef COPY
        e->added_at = (time_t)sqlite3_column_int64(stmt, 6);
    }

    sqlite3_finalize(stmt);
    *out_entries = arr;
    *out_count   = i;
    return 0;
}

int db_delete(int id)
{
    if (!g_db) return -1;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, "DELETE FROM library WHERE id = ?;",
                           -1, &stmt, NULL) != SQLITE_OK) return -1;

    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_exists_by_url(const char *pkg_url)
{
    if (!g_db || !pkg_url) return -1;

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, "SELECT 1 FROM library WHERE pkg_url = ? LIMIT 1;",
                           -1, &stmt, NULL) != SQLITE_OK) return -1;

    sqlite3_bind_text(stmt, 1, pkg_url, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW) ? 1 : 0;
}
