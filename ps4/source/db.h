#pragma once
#include <time.h>

#define LIBRARY_DB_PATH  "/data/orbis_store/library.db"
#define LIBRARY_NAME_MAX  128
#define LIBRARY_VER_MAX    32
#define LIBRARY_DESC_MAX  512
#define LIBRARY_URL_MAX   512

/* ── entry record ────────────────────────────────────────────────────────── */

typedef struct {
    int   id;
    char  name[LIBRARY_NAME_MAX];
    char  version[LIBRARY_VER_MAX];
    char  description[LIBRARY_DESC_MAX];
    char  pkg_url[LIBRARY_URL_MAX];
    char  icon_url[LIBRARY_URL_MAX];
    time_t added_at;
} LibraryEntry;

/* ── life-cycle ──────────────────────────────────────────────────────────── */

/*
 * Open (or create) the SQLite database and ensure the schema is up to date.
 * Must be called before any other db_* function.
 * Returns 0 on success, -1 on failure.
 */
int db_open(void);

/* Close the database.  Safe to call even if db_open() was never called. */
void db_close(void);

/* ── CRUD ────────────────────────────────────────────────────────────────── */

/*
 * Insert a new entry.  entry->id is populated on success.
 * Returns 0 on success, -1 on failure.
 */
int db_insert(LibraryEntry *entry);

/*
 * Load all library entries, sorted by added_at DESC.
 *
 * out_entries  – pointer to a LibraryEntry* that will be allocated by the
 *               function; the caller must free() it.
 * out_count    – number of entries written.
 *
 * Returns 0 on success, -1 on failure.
 */
int db_load_all(LibraryEntry **out_entries, int *out_count);

/*
 * Delete the entry with the given id.
 * Returns 0 on success, -1 on failure.
 */
int db_delete(int id);

/*
 * Check whether an entry with the given pkg_url already exists.
 * Returns 1 if found, 0 if not, -1 on error.
 */
int db_exists_by_url(const char *pkg_url);
