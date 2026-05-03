#pragma once
#include "ui.h"
#include "db.h"

#define ORBIS_API_BASE   "https://orbis-api.jsifta52.workers.dev"
#define REDEEM_CODE_LEN  8

/* ── redeem section state ────────────────────────────────────────────────── */
typedef enum {
    REDEEM_STATE_IDLE = 0,   /* waiting for code input    */
    REDEEM_STATE_LOADING,    /* waiting for API response  */
    REDEEM_STATE_PREVIEW,    /* showing app metadata      */
    REDEEM_STATE_SAVING,     /* saving to library         */
    REDEEM_STATE_DONE,       /* saved – show confirmation */
    REDEEM_STATE_ERROR       /* API or network error      */
} RedeemState;

typedef struct {
    RedeemState  state;
    OskState     osk;
    char         code[REDEEM_CODE_LEN + 1];
    char         error_msg[256];

    /* Fetched metadata */
    char  name[128];
    char  version[32];
    char  description[512];
    char  pkg_url[512];
    char  icon_url[512];
} RedeemSection;

void redeem_init(RedeemSection *r);

int  redeem_handle_input(RedeemSection *r, unsigned int buttons);
void redeem_draw(RedeemSection *r);

/* Called once per frame when state == REDEEM_STATE_LOADING */
void redeem_tick(RedeemSection *r);
