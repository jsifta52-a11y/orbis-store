#pragma once
#include "ui.h"
#include "db.h"
#include "installer.h"

/* ── context menu actions ────────────────────────────────────────────────── */
typedef enum {
    LIB_ACTION_NONE = 0,
    LIB_ACTION_OPEN,
    LIB_ACTION_REINSTALL,
    LIB_ACTION_UPDATE,
    LIB_ACTION_DELETE,
    LIB_ACTION_COUNT
} LibraryAction;

typedef struct {
    LibraryEntry    *entries;           /* heap-allocated array from db   */
    int              entry_count;
    int              selected;          /* highlighted item               */
    int              scroll_offset;
    int              menu_open;         /* context menu visible?          */
    LibraryAction    menu_cursor;       /* highlighted menu action        */
    InstallProgress  install;
} LibraryState;

void library_init(LibraryState *s);
void library_free(LibraryState *s);
void library_reload(LibraryState *s);   /* re-read from SQLite            */

int  library_handle_input(LibraryState *s, unsigned int buttons);
void library_draw(LibraryState *s);
