#ifndef ORBIS_PAD_COMPAT_H
#define ORBIS_PAD_COMPAT_H

/* Compatibility wrapper for orbisPad API against OpenOrbis scePad API */
#include <orbis/Pad.h>
#include <orbis/UserService.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int32_t attr;
	int32_t lightBarCtrl;
} OrbisPadConfig;

#define ORBISPAD_ATTR_LIGHT_BAR 1
#define ORBISPAD_LIGHT_BAR_OFF  0

/* Global pad handle (simplified single-controller support) */
static int32_t g_orbis_pad_handle = -1;
static int32_t g_orbis_user_id = 0;

/* Initialize pad system */
static inline int orbisPadInit(void) {
	if (scePadInit() < 0)
		return -1;
	return 0;
}

/* Finish pad system */
static inline int orbisPadFinish(void) {
	if (g_orbis_pad_handle >= 0) {
		scePadClose(g_orbis_pad_handle);
		g_orbis_pad_handle = -1;
	}
	return 0;
}

/* Set pad configuration */
static inline int orbisPadSetConfig(OrbisPadConfig *config) {
	if (g_orbis_pad_handle < 0) {
		g_orbis_user_id = 0; /* Get primary user */
		g_orbis_pad_handle = scePadGetHandle(g_orbis_user_id, 0, 0);
		if (g_orbis_pad_handle < 0)
			return -1;
	}
    
	if (config && config->attr) {
		if (config->lightBarCtrl == 0) { /* OFF */
			scePadResetLightBar(g_orbis_pad_handle);
		}
	}
	return 0;
}

/* Get pad data */
static inline int orbisPadGetData(int32_t user_id, OrbisPadData *data) {
	if (g_orbis_pad_handle < 0) {
		g_orbis_user_id = user_id;
		g_orbis_pad_handle = scePadGetHandle(user_id, 0, 0);
		if (g_orbis_pad_handle < 0)
			return -1;
	}
    
	if (!data)
		return -1;
        
	return scePadRead(g_orbis_pad_handle, data, 1);
}

#ifdef __cplusplus
}
#endif

#endif /* ORBIS_PAD_COMPAT_H */
