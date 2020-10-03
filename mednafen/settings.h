#ifndef MDFN_SETTINGS_H
#define MDFN_SETTINGS_H

#include <stdint.h>
#include <string.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t setting_vb_lcolor;
extern uint32_t setting_vb_rcolor;
extern uint32_t setting_vb_anaglyph_preset;
extern bool setting_vb_right_analog_to_digital;
extern bool setting_vb_right_invert_x;
extern bool setting_vb_right_invert_y;
extern uint32_t setting_vb_cpu_emulation;
extern uint32_t setting_vb_3dmode;
extern uint32_t setting_vb_liprescale;
extern uint32_t setting_vb_default_color;

/* This should assert() or something if the 
 * setting isn't found, since it would
 * be a totally tubular error! */
uint64_t MDFN_GetSettingUI(const char *name);
int64_t MDFN_GetSettingI(const char *name);
bool MDFN_GetSettingB(const char *name);

#ifdef __cplusplus
}
#endif

#endif
