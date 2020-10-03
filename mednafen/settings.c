/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdint.h>
#include <string.h>

#include "settings.h"

uint32_t setting_vb_lcolor=0xFF0000;
uint32_t setting_vb_rcolor=0x000000;
uint32_t setting_vb_anaglyph_preset=0;
bool setting_vb_right_analog_to_digital=false;
bool setting_vb_right_invert_x=false;
bool setting_vb_right_invert_y=false;
uint32_t setting_vb_cpu_emulation=0;
uint32_t setting_vb_3dmode=0;
uint32_t setting_vb_liprescale=1;
uint32_t setting_vb_default_color=0xFFFFFF;

uint64_t MDFN_GetSettingUI(const char *name)
{
   if (!strcmp("vb.anaglyph.lcolor", name))
      return setting_vb_lcolor;
   if (!strcmp("vb.anaglyph.rcolor", name))
      return setting_vb_rcolor;
   if (!strcmp("vb.3dmode", name))
      return setting_vb_3dmode;
   if (!strcmp("vb.liprescale", name))
      return setting_vb_liprescale;
   if (!strcmp("vb.default_color", name))
      return setting_vb_default_color;
   return 0;
}

int64_t MDFN_GetSettingI(const char *name)
{
   if (!strcmp("vb.anaglyph.preset", name))
      return setting_vb_anaglyph_preset;
   if (!strcmp("vb.cpu_emulation", name))
      return setting_vb_cpu_emulation;
   return 0;
}

bool MDFN_GetSettingB(const char *name)
{
   if (!strcmp("cheats", name))
      return 0;
   /* LIBRETRO */
   if (!strcmp("vb.instant_display_hack", name))
      return 1;
   if (!strcmp("vb.allow_draw_skip", name))
      return 1;
   return 0;
}
