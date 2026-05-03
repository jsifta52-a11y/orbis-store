#pragma once
#include "ui.h"
#include "db.h"
#include "installer.h"

/* Maximum number of built-in store items */
#define STORE_MAX_ITEMS  64

typedef struct {
    char name[128];
    char version[32];
    char description[512];
    char pkg_url[512];
    char icon_url[512];
} StoreItem;

typedef struct {
    StoreItem       items[STORE_MAX_ITEMS];
    int             item_count;
    int             selected;       /* index of highlighted item */
    int             scroll_offset;  /* first visible item index  */
    InstallProgress install;        /* ongoing install state      */
} StoreState;

/*
 * Initialise the store list (loads built-in items).
 */
void store_init(StoreState *s);

/*
 * Handle a button-press event.  Returns 1 if the event was consumed.
 */
int store_handle_input(StoreState *s, unsigned int buttons);

/*
 * Render the store section.
 */
void store_draw(StoreState *s);
