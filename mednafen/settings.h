#ifndef MDFN_SETTINGS_H
#define MDFN_SETTINGS_H

#include <string.h>

extern uint32_t setting_vb_lcolor;
extern uint32_t setting_vb_rcolor;
extern uint32_t setting_vb_anaglyph_preset;

bool MDFN_LoadSettings(const char *path, const char *section = NULL, bool override = false);
bool MDFN_SaveSettings(const char *path);

void MDFN_KillSettings(void);	// Free any resources acquired.

// This should assert() or something if the setting isn't found, since it would
// be a totally tubular error!
uint64 MDFN_GetSettingUI(const char *name);
int64 MDFN_GetSettingI(const char *name);
double MDFN_GetSettingF(const char *name);
bool MDFN_GetSettingB(const char *name);
#endif
