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

#include <stdlib.h>
#include "surface.h"

MDFN_Surface::MDFN_Surface()
{
   format.bpp        = 0;
   format.colorspace = 0;
   format.Rshift     = 0;
   format.Gshift     = 0;
   format.Bshift     = 0;
   format.Ashift     = 0;

   pixels     = NULL;
   pixels16   = NULL;
   pitchinpix = 0;
   w          = 0;
   h          = 0;
}

MDFN_Surface::MDFN_Surface(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const struct MDFN_PixelFormat &nf)
{
   void *rpix = NULL;
   format     = nf;

   pixels16   = NULL;
   pixels     = NULL;

   if(!(rpix = calloc(1, p_pitchinpix * p_height * (nf.bpp / 8))))
      return;

#if defined(WANT_16BPP)
   pixels16 = (uint16 *)rpix;
#elif defined(WANT_32BPP)
   pixels = (uint32 *)rpix;
#endif

   w = p_width;
   h = p_height;

   pitchinpix = p_pitchinpix;
}

MDFN_Surface::~MDFN_Surface()
{
#if defined(WANT_16BPP)
   if(pixels16)
      free(pixels16);
#elif defined(WANT_32BPP)
   if(pixels)
      free(pixels);
#endif
}

