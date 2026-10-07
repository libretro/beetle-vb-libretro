#include <stdarg.h>
#include <assert.h>

#include <libretro.h>

#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/state_helpers.h"
#include "mednafen/masmem.h"
#include "mednafen/settings.h"

#include "libretro_vr.h"
#include "vb_games.h"

/* Forward declarations */
void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;
retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static bool libretro_supports_bitmasks = false;

static bool overscan;
static struct MDFN_PixelFormat last_pixel_format;

static struct MDFN_Surface surf;

bool vr_option_enabled = true;

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

#include "mednafen/vb/vb.h"
#include "mednafen/vb/timer.h"
#include "mednafen/vb/vsu.h"
#include "mednafen/vb/vip.h"
#include "mednafen/vb/input.h"
#include "mednafen/mempatcher.h"
#include "mednafen/hw_cpu/v810/v810_cpu.h"

#include "libretro_core_options.h"

enum
{
 ANAGLYPH_PRESET_DISABLED = 0,
 ANAGLYPH_PRESET_RED_BLUE,
 ANAGLYPH_PRESET_RED_CYAN,
 ANAGLYPH_PRESET_RED_ELECTRICCYAN,
 ANAGLYPH_PRESET_RED_GREEN,
 ANAGLYPH_PRESET_GREEN_MAGENTA,
 ANAGLYPH_PRESET_YELLOW_BLUE
};

static const uint32 AnaglyphPreset_Colors[][2] =
{
 { 0, 0 },
 { 0xFF0000, 0x0000FF },
 { 0xFF0000, 0x00B7EB },
 { 0xFF0000, 0x00FFFF },
 { 0xFF0000, 0x00FF00 },
 { 0x00FF00, 0xFF00FF },
 { 0xFFFF00, 0x0000FF },
};

#define STICK_DEADZONE 0x4000
#define LEFT_DPAD_LEFT 0x0080
#define LEFT_DPAD_RIGHT 0x0040
#define LEFT_DPAD_UP 0x0200
#define LEFT_DPAD_DOWN 0x0100
#define RIGHT_DPAD_LEFT 0x1000
#define RIGHT_DPAD_RIGHT 0x0020
#define RIGHT_DPAD_UP 0x0010
#define RIGHT_DPAD_DOWN 0x2000

// D-Pads opposite directions
#define LEFT_DPAD_LEFT_RIGHT (LEFT_DPAD_LEFT | LEFT_DPAD_RIGHT)
#define LEFT_DPAD_UP_DOWN (LEFT_DPAD_UP | LEFT_DPAD_DOWN)
#define RIGHT_DPAD_LEFT_RIGHT (RIGHT_DPAD_LEFT | RIGHT_DPAD_RIGHT)
#define RIGHT_DPAD_UP_DOWN (RIGHT_DPAD_UP | RIGHT_DPAD_DOWN)

static bool opposite_directions = false;

static uint32 VB3DMode;

static Blip_Buffer sbuf[2];

static uint8 *WRAM = NULL;

static uint8 *GPRAM = NULL;
static uint32 GPRAM_Mask;

static uint8 *GPROM = NULL;
static uint32 GPROM_Mask;

V810 *VB_V810 = NULL;

static uint32 VSU_CycleFix;

static uint8 WCR;

static int32 next_vip_ts, next_timer_ts, next_input_ts;

static uint32 IRQ_Asserted;

static INLINE void RecalcIntLevel(void)
{
   int i, ilevel = -1;

   for(i = 4; i >= 0; i--)
   {
      if(IRQ_Asserted & (1 << i))
      {
         ilevel = i;
         break;
      }
   }

   VB_V810->SetInt(ilevel);
}

extern "C" void VBIRQ_Assert(int source, bool assert)
{
   assert(source >= 0 && source <= 4);

   IRQ_Asserted &= ~(1 << source);

   if(assert)
      IRQ_Asserted |= 1 << source;

   RecalcIntLevel();
}

static uint8 HWCTRL_Read(v810_timestamp_t &timestamp, uint32 A)
{
   /* HWCtrl Bogus Read? */
   if(A & 0x3)
      return 0;

   switch(A & 0xFF)
   {
      case 0x18:
      case 0x1C:
      case 0x20:
         return TIMER_Read(timestamp, A);
      case 0x24:
         return WCR | 0xFC;
      case 0x10:
      case 0x14:
      case 0x28:
         return VBINPUT_Read(timestamp, A);
   }

   return 0;
}

static void HWCTRL_Write(v810_timestamp_t &timestamp, uint32 A, uint8 V)
{
   /* HWCtrl Bogus Write? */
   if(A & 0x3)
      return;

   switch(A & 0xFF)
   {
      case 0x18:
      case 0x1C:
      case 0x20:
         TIMER_Write(timestamp, A, V);
         break;
      case 0x24:
         WCR = V & 0x3;
         break;
      case 0x10:
      case 0x14:
      case 0x28:
         VBINPUT_Write(timestamp, A, V);
         break;
   }
}

uint8 MDFN_FASTCALL MemRead8(v810_timestamp_t &timestamp, uint32 A)
{
   A &= (1 << 27) - 1;

   switch(A >> 24)
   {
      case 0:
         return VIP_Read8(timestamp, A);
      case 2:
         return HWCTRL_Read(timestamp, A);
      case 1:
      case 3:
      case 4:
         break;
      case 5:
         return WRAM[A & 0xFFFF];
      case 6:
         if(GPRAM)
            return GPRAM[A & GPRAM_Mask];
         break;
      case 7:
         return GPROM[A & GPROM_Mask];
   }
   return 0;
}

uint16 MDFN_FASTCALL MemRead16(v810_timestamp_t &timestamp, uint32 A)
{
   A &= (1 << 27) - 1;

   switch(A >> 24)
   {
      case 0:
         return VIP_Read16(timestamp, A);
      case 2:
         return HWCTRL_Read(timestamp, A);
      case 1:
      case 3:
      case 4:
         break;
      case 5:
         return LoadU16_LE((uint16 *)&WRAM[A & 0xFFFF]);
      case 6:
         if(GPRAM)
            return LoadU16_LE((uint16 *)&GPRAM[A & GPRAM_Mask]);
         break;

      case 7:
         return LoadU16_LE((uint16 *)&GPROM[A & GPROM_Mask]);
   }

   return 0;
}

void MDFN_FASTCALL MemWrite8(v810_timestamp_t &timestamp, uint32 A, uint8 V)
{
   A &= (1 << 27) - 1;

   switch(A >> 24)
   {
      case 0:
         VIP_Write8(timestamp, A, V);
         break;
      case 1:
         VSU_Write((timestamp + VSU_CycleFix) >> 2, A, V);
         break;
      case 2:
         HWCTRL_Write(timestamp, A, V);
         break;
      case 5:
         WRAM[A & 0xFFFF] = V;
         break;
      case 6:
         if(GPRAM)
            GPRAM[A & GPRAM_Mask] = V;
         break;

      case 7:
         // ROM, no writing allowed!
      case 3:
      case 4:
         break;
   }
}

void MDFN_FASTCALL MemWrite16(v810_timestamp_t &timestamp, uint32 A, uint16 V)
{
   A &= (1 << 27) - 1;

   switch(A >> 24)
   {
      case 0:
         VIP_Write16(timestamp, A, V);
         break;
      case 1:
         VSU_Write((timestamp + VSU_CycleFix) >> 2, A, V);
         break;
      case 2:
         HWCTRL_Write(timestamp, A, V);
         break;
      case 5:
         StoreU16_LE((uint16 *)&WRAM[A & 0xFFFF], V);
         break;
      case 6:
         if(GPRAM)
            StoreU16_LE((uint16 *)&GPRAM[A & GPRAM_Mask], V);
         break;
      case 3:
      case 4:
      case 7:
         /* ROM, no writing allowed! */
         break;
   }
}

static void FixNonEvents(void)
{
   if(next_vip_ts & 0x40000000)
      next_vip_ts   = VB_EVENT_NONONO;

   if(next_timer_ts & 0x40000000)
      next_timer_ts = VB_EVENT_NONONO;

   if(next_input_ts & 0x40000000)
      next_input_ts = VB_EVENT_NONONO;
}

static void EventReset(void)
{
   next_vip_ts   = VB_EVENT_NONONO;
   next_timer_ts = VB_EVENT_NONONO;
   next_input_ts = VB_EVENT_NONONO;
}

static INLINE int32 CalcNextTS(void)
{
   int32 next_timestamp = next_vip_ts;

   if(next_timestamp > next_timer_ts)
      next_timestamp  = next_timer_ts;

   if(next_timestamp > next_input_ts)
      next_timestamp  = next_input_ts;

   return next_timestamp;
}

static void RebaseTS(const v810_timestamp_t timestamp)
{
   assert(next_vip_ts   > timestamp);
   assert(next_timer_ts > timestamp);
   assert(next_input_ts > timestamp);

   next_vip_ts   -= timestamp;
   next_timer_ts -= timestamp;
   next_input_ts -= timestamp;
}

extern "C" void VB_SetEvent(const int type,
      const v810_timestamp_t next_timestamp)
{
   if      (type == VB_EVENT_VIP)
      next_vip_ts = next_timestamp;
   else if (type == VB_EVENT_TIMER)
      next_timer_ts = next_timestamp;
   else if (type == VB_EVENT_INPUT)
      next_input_ts = next_timestamp;

   if(next_timestamp < VB_V810->GetEventNT())
      VB_V810->SetEventNT(next_timestamp);
}

static int32 MDFN_FASTCALL EventHandler(const v810_timestamp_t timestamp)
{
   if (timestamp >= next_vip_ts)
      next_vip_ts = VIP_Update(timestamp);
   if (timestamp >= next_timer_ts)
      next_timer_ts = TIMER_Update(timestamp);
   if (timestamp >= next_input_ts)
      next_input_ts = VBINPUT_Update(timestamp);

   return CalcNextTS();
}

/* Called externally from debug.cpp in some cases. */
static void ForceEventUpdates(const v810_timestamp_t timestamp)
{
   next_vip_ts   = VIP_Update(timestamp);
   next_timer_ts = TIMER_Update(timestamp);
   next_input_ts = VBINPUT_Update(timestamp);

   VB_V810->SetEventNT(CalcNextTS());
}

static void VB_Power(void)
{
   memset(WRAM, 0, 65536);

   VIP_Power();
   VSU_Power();
   TIMER_Power();
   VBINPUT_Power();

   /* VSU_Power() clears the synth's per-channel last_output tracking, but the
    * resampler buffers live out here and keep their fractional offset and
    * integrator state, so without this a reset carries audio state over from
    * before it. Safe on the initial Load() path too, where the buffers have
    * not been configured yet and this is a no-op. */
   {
      int y;
      for(y = 0; y < 2; y++)
         Blip_Buffer_clear(&sbuf[y], 1);
   }

   EventReset();
   IRQ_Asserted = 0;
   RecalcIntLevel();
   VB_V810->Reset();

   VSU_CycleFix = 0;
   WCR = 0;

   ForceEventUpdates(0);
}

static void SettingChanged(const char *name)
{
   if(!strcmp(name, "vb.3dmode"))
   {
      VB3DMode              = MDFN_GetSettingUI("vb.3dmode");
      uint32 prescale       = MDFN_GetSettingUI("vb.liprescale");
      uint32 sbs_separation = MDFN_GetSettingUI("vb.sidebyside.separation");

      VIP_Set3DMode(VB3DMode, MDFN_GetSettingUI("vb.3dreverse"), prescale, sbs_separation);
   }
   else if(!strcmp(name, "vb.disable_parallax"))
   {
      VIP_SetParallaxDisable(MDFN_GetSettingB("vb.disable_parallax"));
   }
   else if(!strcmp(name, "vb.anaglyph.lcolor") || !strcmp(name, "vb.anaglyph.rcolor") ||
         !strcmp(name, "vb.anaglyph.preset") || !strcmp(name, "vb.default_color"))
   {
      uint32 lcolor = MDFN_GetSettingUI("vb.anaglyph.lcolor"), rcolor = MDFN_GetSettingUI("vb.anaglyph.rcolor");
      int preset    = MDFN_GetSettingI("vb.anaglyph.preset");

      if(preset != ANAGLYPH_PRESET_DISABLED)
      {
         lcolor = AnaglyphPreset_Colors[preset][0];
         rcolor = AnaglyphPreset_Colors[preset][1];
      }
      VIP_SetAnaglyphColors(lcolor, rcolor);
      VIP_SetDefaultColor(MDFN_GetSettingUI("vb.default_color"));
   }
   else if(!strcmp(name, "vb.input.instant_read_hack"))
      VBINPUT_SetInstantReadHack(MDFN_GetSettingB("vb.input.instant_read_hack"));
   else if(!strcmp(name, "vb.instant_display_hack"))
      VIP_SetInstantDisplayHack(MDFN_GetSettingB("vb.instant_display_hack"));
   else if(!strcmp(name, "vb.allow_draw_skip"))
      VIP_SetAllowDrawSkip(MDFN_GetSettingB("vb.allow_draw_skip"));
}

struct VB_HeaderInfo
{
   char game_title[256];
   uint32 game_code;
   uint16 manf_code;
   uint8 version;
};

// Source: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
// Rounds up to the nearest power of 2.
static INLINE uint32 round_up_pow2(uint32 v)
{
   v--;
   v |= v >> 1;
   v |= v >> 2;
   v |= v >> 4;
   v |= v >> 8;
   v |= v >> 16;
   v++;

   v += (v == 0);

   return(v);
}

static int Load(const uint8_t *data, size_t size)
{
   uint32_t* Map_Addresses;
   uint32_t map_size = 0;
   int i;
   uint64 A, sub_A;
   V810_Emu_Mode cpu_mode = (V810_Emu_Mode)MDFN_GetSettingI("vb.cpu_emulation");

   /* VB ROM image size is not a power of 2??? */
   if(size != round_up_pow2(size))
      return 0;

   /* VB ROM image size is too small?? */
   if(size < 256)
      return 0;

   /* VB ROM image size is too large?? */
   if(size > (1 << 24))
      return 0;

   VB_V810 = new V810();
   VB_V810->Init(cpu_mode, true);

   VB_V810->SetMemReadHandlers(MemRead8, MemRead16, NULL);
   VB_V810->SetMemWriteHandlers(MemWrite8, MemWrite16, NULL);

   VB_V810->SetIOReadHandlers(MemRead8, MemRead16, NULL);
   VB_V810->SetIOWriteHandlers(MemWrite8, MemWrite16, NULL);

   for(i = 0; i < 256; i++)
   {
      VB_V810->SetMemReadBus32(i, false);
      VB_V810->SetMemWriteBus32(i, false);
   }

   Map_Addresses = (uint32_t*)malloc(8192 * 4);

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 5 << 24; sub_A < (6 << 24); sub_A += 65536)
         Map_Addresses[map_size++] = A + sub_A;
   }
   WRAM = VB_V810->SetFastMap(Map_Addresses, 65536, map_size, "WRAM");

   // Round up the ROM size to 65536(we mirror it a little later)
   GPROM_Mask = (size < 65536) ? (65536 - 1) : (size - 1);

   map_size = 0;
   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 7 << 24; sub_A < (8 << 24); sub_A += GPROM_Mask + 1)
         Map_Addresses[map_size++] = A + sub_A;
   }

   GPROM = VB_V810->SetFastMap(Map_Addresses, GPROM_Mask + 1, map_size, "Cart ROM");
   map_size = 0;

   // Mirror ROM images < 64KiB to 64KiB
   for(uint64 i = 0; i < 65536; i += size)
      memcpy(GPROM + i, data, size);

   GPRAM_Mask = 0xFFFF;

   for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
   {
      for(uint64 sub_A = 6 << 24; sub_A < (7 << 24); sub_A += GPRAM_Mask + 1)
         Map_Addresses[map_size++] = A + sub_A;
   }
   GPRAM = VB_V810->SetFastMap(Map_Addresses, GPRAM_Mask + 1, map_size, "Cart RAM");

   if (Map_Addresses)
   {
      free(Map_Addresses);
      Map_Addresses = NULL;
   }

   memset(GPRAM, 0, GPRAM_Mask + 1);

   VIP_Init();
   VSU_Init(&sbuf[0], &sbuf[1]);
   VBINPUT_Init();

   VB3DMode = MDFN_GetSettingUI("vb.3dmode");
   uint32 prescale = MDFN_GetSettingUI("vb.liprescale");
   uint32 sbs_separation = MDFN_GetSettingUI("vb.sidebyside.separation");

   VIP_Set3DMode(VB3DMode, MDFN_GetSettingUI("vb.3dreverse"), prescale, sbs_separation);

   SettingChanged("vb.3dmode");
   SettingChanged("vb.disable_parallax");
   SettingChanged("vb.anaglyph.lcolor");
   SettingChanged("vb.anaglyph.rcolor");
   SettingChanged("vb.anaglyph.preset");
   SettingChanged("vb.default_color");

   SettingChanged("vb.instant_display_hack");
   SettingChanged("vb.allow_draw_skip");

   SettingChanged("vb.input.instant_read_hack");

   VB_Power();

   MDFNMP_Init(32768, ((uint64)1 << 27) / 32768);
   MDFNMP_AddRAM(65536, 5 << 24, WRAM);
   if((GPRAM_Mask + 1) >= 32768)
      MDFNMP_AddRAM(GPRAM_Mask + 1, 6 << 24, GPRAM);
   return 1;
}

static void CloseGame(void)
{
#if 0
   VIP_Kill();
#endif

#if 0
   if(GPRAM)
   {
      MDFN_free(GPRAM);
      GPRAM = NULL;
   }

   if(GPROM)
   {
      MDFN_free(GPROM);
      GPROM = NULL;
   }
#endif

   if(VB_V810)
   {
      VB_V810->Kill();
      delete VB_V810;
      VB_V810 = NULL;
   }
}

extern "C" void VB_ExitLoop(void)
{
   VB_V810->Exit();
}

static void Emulate(EmulateSpecStruct *espec, int16_t *sound_buf)
{
   v810_timestamp_t v810_timestamp;

   MDFNMP_ApplyPeriodicCheats();

   VBINPUT_Frame();

   VIP_StartFrame(espec);

   v810_timestamp = VB_V810->Run(EventHandler);

   FixNonEvents();
   ForceEventUpdates(v810_timestamp);

   VSU_EndFrame((v810_timestamp + VSU_CycleFix) >> 2);

   if(sound_buf)
   {
      int y;
      for(y = 0; y < 2; y++)
      {
         Blip_Buffer_end_frame(&sbuf[y], (v810_timestamp + VSU_CycleFix) >> 2);
         /* Both channels are clocked identically and so always yield the same
          * count; take it from the last one rather than letting each pass
          * overwrite the previous. */
         espec->SoundBufSize = Blip_Buffer_read_samples(&sbuf[y], sound_buf + y, espec->SoundBufMaxSize);
      }
   }

   VSU_CycleFix = (v810_timestamp + VSU_CycleFix) & 3;

   TIMER_ResetTS();
   VBINPUT_ResetTS();
   VIP_ResetTS();

   RebaseTS(v810_timestamp);

   VB_V810->ResetTS(0);
}

extern "C" int StateAction(StateMem *sm, int load, int data_only)
{
   const v810_timestamp_t timestamp = VB_V810->v810_timestamp;
   int ret = 1;

   SFORMAT StateRegs[] =
   {
      SFARRAY(WRAM, 65536),
      SFARRAY(GPRAM, GPRAM_Mask ? (GPRAM_Mask + 1) : 0),
      SFVARN(WCR, "WCR"),
      SFVARN(IRQ_Asserted, "IRQ_Asserted"),
      SFVARN(VSU_CycleFix, "VSU_CycleFix"),
      SFEND
   };

   ret &= MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN", false);

   ret &= VB_V810->StateAction(sm, load, data_only);

   ret &= VSU_StateAction(sm, load, data_only);
   ret &= TIMER_StateAction(sm, load, data_only);
   ret &= VBINPUT_StateAction(sm, load, data_only);
   ret &= VIP_StateAction(sm, load, data_only);

   // Needed to recalculate next_*_ts since we don't bother storing their deltas in save states.
   if(load)
   {
      /* VSU_CycleFix is a sub-4-cycle remainder: the emulation loop always
       * leaves it as (timestamp + VSU_CycleFix) & 3, so only 0..3 is valid.
       * A corrupt savestate could otherwise push
       * (v810_timestamp + VSU_CycleFix) >> 2 far past the end of the Blip
       * buffer, producing an out-of-bounds read when the frame is flushed. */
      VSU_CycleFix &= 3;

      ForceEventUpdates(timestamp);
   }
   return ret;
}

#define MEDNAFEN_CORE_NAME_MODULE "vb"
#define MEDNAFEN_CORE_NAME "Beetle VB"
#define MEDNAFEN_CORE_VERSION "v1.31.0"
#define MEDNAFEN_CORE_EXTENSIONS "vb|vboy|bin"
#define MEDNAFEN_CORE_TIMING_FPS 50.27
#define MEDNAFEN_CORE_GEOMETRY_BASE_W 384
#define MEDNAFEN_CORE_GEOMETRY_BASE_H 224
#define MEDNAFEN_CORE_GEOMETRY_MAX_W (384 * 2 + 256)
#define MEDNAFEN_CORE_GEOMETRY_MAX_H (224 * 2)
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (12.0 / 7.0)
#define FB_WIDTH (384 * ((setting_vb_3dmode == VB3DMODE_ANAGLYPH) ? 1 : 2) + ((setting_vb_3dmode == VB3DMODE_SIDEBYSIDE) ? 256 : 0))
#define FB_HEIGHT 224 * 2


#define FB_MAX_HEIGHT FB_HEIGHT

const char *mednafen_core_str = MEDNAFEN_CORE_NAME;

static void check_system_specs(void)
{
   unsigned level = 0;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init(void)
{
   struct retro_log_callback log;
#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   enum retro_pixel_format rgb565 = RETRO_PIXEL_FORMAT_RGB565;
#endif
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else 
      log_cb = NULL;

#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   if (environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565) && log_cb)
      log_cb(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
#endif

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   check_system_specs();

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;
}

void retro_reset(void)
{
   VB_Power();
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

static void set_volume (uint32_t *ptr, unsigned number)
{
   switch(number)
   {
      default:
         *ptr = number;
         break;
   }
}

static void check_variables(void)
{
   struct retro_variable var = {0};

   var.key = "vb_3dmode";

   if (vb_vr_mode() != VB_PRESENT_VR && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_3dmode = setting_vb_3dmode;

      if (strcmp(var.value, "anaglyph") == 0)
         setting_vb_3dmode = VB3DMODE_ANAGLYPH;
      else if (strcmp(var.value, "cyberscope") == 0)
         setting_vb_3dmode = VB3DMODE_CSCOPE;
      else if (strcmp(var.value, "side-by-side") == 0)
         setting_vb_3dmode = VB3DMODE_SIDEBYSIDE;
      else if (strcmp(var.value, "vli") == 0)
         setting_vb_3dmode = VB3DMODE_VLI;
      else if (strcmp(var.value, "hli") == 0)
         setting_vb_3dmode = VB3DMODE_HLI;

      if (old_3dmode != setting_vb_3dmode)
      {
         SettingChanged("vb.3dmode");

         log_cb(RETRO_LOG_INFO, "[%s]: 3D mode changed: %s .\n", mednafen_core_str, var.value);  
      }
   }

   var.key = "vb_anaglyph_preset";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_preset = setting_vb_anaglyph_preset;

      if (strcmp(var.value, "disabled") == 0)
         setting_vb_anaglyph_preset = 0;
      else if (strcmp(var.value, "red & blue") == 0)
         setting_vb_anaglyph_preset = 1;
      else if (strcmp(var.value, "red & cyan") == 0)
         setting_vb_anaglyph_preset = 2;
      else if (strcmp(var.value, "red & electric cyan") == 0)    
         setting_vb_anaglyph_preset = 3;
      else if (strcmp(var.value, "red & green") == 0)
         setting_vb_anaglyph_preset = 4;
      else if (strcmp(var.value, "green & magenta") == 0)
         setting_vb_anaglyph_preset = 5;
      else if (strcmp(var.value, "yellow & blue") == 0)
         setting_vb_anaglyph_preset = 6;

      if (old_preset != setting_vb_anaglyph_preset)
      {
         SettingChanged("vb.anaglyph.preset");

         log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
      }
   }

   var.key = "vb_color_mode";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_color = setting_vb_default_color;

      if (strcmp(var.value, "black & red") == 0)
      {
         setting_vb_lcolor = 0xFF0000;
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & white") == 0)
      {
         setting_vb_lcolor = 0xFFFFFF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & blue") == 0)
      {
         setting_vb_lcolor = 0x0000FF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & cyan") == 0)
      {
         setting_vb_lcolor = 0x00B7EB;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & electric cyan") == 0)
      {
         setting_vb_lcolor = 0x00FFFF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & green") == 0)
      {
         setting_vb_lcolor = 0x00FF00;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & magenta") == 0)
      {
         setting_vb_lcolor = 0xFF00FF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & yellow") == 0)
      {
         setting_vb_lcolor = 0xFFFF00;      
         setting_vb_rcolor = 0x000000;
      }
      setting_vb_default_color = setting_vb_lcolor;

      if (old_color != setting_vb_default_color)
      {
         SettingChanged("vb.default_color");

         log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
      }
   }   

   var.key = "vb_right_analog_to_digital";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         setting_vb_right_analog_to_digital = false;
      else if (strcmp(var.value, "enabled") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = false;
         setting_vb_right_invert_y = false;
      }
      else if (strcmp(var.value, "invert x") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = true;
         setting_vb_right_invert_y = false;
      }
      else if (strcmp(var.value, "invert y") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = false;
         setting_vb_right_invert_y = true;
      }
      else if (strcmp(var.value, "invert both") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = true;
         setting_vb_right_invert_y = true;
      }
      else
         setting_vb_right_analog_to_digital = false;
   }

   var.key = "vb_opposite_directions";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      opposite_directions = !strcmp(var.value, "enabled");

   var.key = "vb_cpu_emulation";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      setting_vb_cpu_emulation = !strcmp(var.value, "accurate")
         ? V810_EMU_MODE_ACCURATE
         : V810_EMU_MODE_FAST;
   }

   var.key = "vb_sidebyside_separation";

   if (vb_vr_mode() != VB_PRESENT_VR && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_separation = setting_vb_sidebyside_separation;

      setting_vb_sidebyside_separation = strtoul(var.value, NULL, 10);

      if (old_separation != setting_vb_sidebyside_separation)
      {
         SettingChanged("vb.3dmode");

         log_cb(RETRO_LOG_INFO, "[%s]: Side-by-side separation changed: %u pixels.\n", mednafen_core_str, setting_vb_sidebyside_separation);
      }
   }

   var.key = "vb_vr";
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      vr_option_enabled = strcmp(var.value, "disabled") != 0;

   {
      float dist = 1.0f, width = 1.1f;
      var.key = "vb_vr_screen_distance";
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         dist = (float)atof(var.value);
      var.key = "vb_vr_screen_width";
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         width = (float)atof(var.value);
      vb_vr_set_screen(dist, width);
   }
}

#define MAX_PLAYERS 1
#define MAX_BUTTONS 14
static uint16_t input_buf[MAX_PLAYERS];
static uint16_t low_battery;

bool retro_load_game(const struct retro_game_info *info)
{
   struct MDFN_PixelFormat pix_fmt;
   void *rpix = NULL;
#ifdef WANT_32BPP
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
#endif
   static struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Left D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Left D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Left D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Right D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Right D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Right D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "Right D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "Low-Battery Toggle" },

      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right D-Pad X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right D-Pad Y" },
      { 0 },
   };

   if (!info)
      return false;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

#ifdef WANT_32BPP
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "Pixel format XRGB8888 not supported by platform, cannot use %s.\n", MEDNAFEN_CORE_NAME);
      return false;
   }
#endif

   overscan = false;
   environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &overscan);

   check_variables();

   if (vb_vr_negotiate(environ_cb, vr_option_enabled) == VB_PRESENT_VR)
   {
      setting_vb_3dmode                = VB3DMODE_SIDEBYSIDE;
      setting_vb_sidebyside_separation = 0;
   }

   if (Load((const uint8_t*)info->data, info->size) <= 0)
   {
      vb_vr_unload();
      return false;
   }

   MDFN_LoadGameCheats(NULL);

#ifdef WANT_16BPP
   pix_fmt.bpp        = 16;
#else
   pix_fmt.bpp        = 32;
#endif
   pix_fmt.colorspace = MDFN_COLORSPACE_RGB;
   pix_fmt.Rshift     = 16;
   pix_fmt.Gshift     = 8;
   pix_fmt.Bshift     = 0;
   pix_fmt.Ashift     = 24;

   last_pixel_format.bpp        = 0;
   last_pixel_format.colorspace = 0;
   last_pixel_format.Rshift     = 0;
   last_pixel_format.Gshift     = 0;
   last_pixel_format.Bshift     = 0;
   last_pixel_format.Ashift     = 0;

   surf.format                  = pix_fmt;

   /* retro_unload_game() does not release the surface, so a previous load
    * may have left one allocated. Free it before dropping the pointer,
    * otherwise every load/unload cycle leaks a whole framebuffer. */
#if defined(WANT_16BPP)
   if(surf.pixels16)
      free(surf.pixels16);
#elif defined(WANT_32BPP)
   if(surf.pixels)
      free(surf.pixels);
#endif

   surf.pixels16                = NULL;
   surf.pixels                  = NULL;

   if(!(rpix = calloc(1, FB_WIDTH * FB_HEIGHT * (pix_fmt.bpp / 8))))
   {
      vb_vr_unload();
      return false;
   }

#if defined(WANT_16BPP)
   surf.pixels16                = (uint16 *)rpix;
#elif defined(WANT_32BPP)
   surf.pixels                  = (uint32 *)rpix;
#endif
   surf.w                       = FB_WIDTH;
   surf.h                       = FB_HEIGHT;
   surf.pitchinpix              = FB_WIDTH;

   /* Possible endian bug ... */
   VBINPUT_SetInput(0, "gamepad", &input_buf[0]);
   VBINPUT_SetInput(1, "gamepad", &low_battery);

   check_variables();

   {
      int y;
      for(y = 0; y < 2; y++)
      {
         Blip_Buffer_set_sample_rate(&sbuf[y], 44100, 50);
         Blip_Buffer_set_clock_rate(&sbuf[y], (long)(VB_MASTER_CLOCK / 4));
         Blip_Buffer_bass_freq(&sbuf[y], 20);
      }
   }

   return true;
}

void retro_unload_game(void)
{
   MDFN_FlushGameCheats(0);
   CloseGame();
   vb_vr_unload();
   MDFNMP_Kill();
}

static void update_input(void)
{
   unsigned i,j;
   int16_t joy_bits[MAX_PLAYERS] = {0};

   input_buf[0] = 0;

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_L2, //right d-pad UP
      RETRO_DEVICE_ID_JOYPAD_R3, //right d-pad RIGHT
      RETRO_DEVICE_ID_JOYPAD_RIGHT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_LEFT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_DOWN, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_UP, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_R2, //right d-pad LEFT
      RETRO_DEVICE_ID_JOYPAD_L3, //right d-pad DOWN
   };

   for (j = 0; j < MAX_PLAYERS; j++)
   {
      if (libretro_supports_bitmasks)
         joy_bits[j] = input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
      else
      {
         for (i = 0; i < (RETRO_DEVICE_ID_JOYPAD_R3+1); i++)
            joy_bits[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
      }
   }

   for (j = 0; j < MAX_PLAYERS; j++)
   {
      for (i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= (map[i] != -1u) && (joy_bits[j] & (1 << map[i])) ? (1 << i) : 0;

      if (setting_vb_right_analog_to_digital)
      {
         int16_t analog_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
         int16_t analog_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

         if (abs(analog_x) > STICK_DEADZONE)
            input_buf[j] |= (analog_x < 0) ^ !setting_vb_right_invert_x ? RIGHT_DPAD_RIGHT : RIGHT_DPAD_LEFT;
         if (abs(analog_y) > STICK_DEADZONE)
            input_buf[j] |= (analog_y < 0) ^ !setting_vb_right_invert_y ? RIGHT_DPAD_DOWN : RIGHT_DPAD_UP;
      }

      if (!opposite_directions)
      {
         if ((input_buf[j] & LEFT_DPAD_LEFT_RIGHT) == LEFT_DPAD_LEFT_RIGHT)
            input_buf[j] &= ~LEFT_DPAD_LEFT_RIGHT;
         if ((input_buf[j] & LEFT_DPAD_UP_DOWN) == LEFT_DPAD_UP_DOWN)
            input_buf[j] &= ~LEFT_DPAD_UP_DOWN;
         if ((input_buf[j] & RIGHT_DPAD_LEFT_RIGHT) == RIGHT_DPAD_LEFT_RIGHT)
            input_buf[j] &= ~RIGHT_DPAD_LEFT_RIGHT;
         if ((input_buf[j] & RIGHT_DPAD_UP_DOWN) == RIGHT_DPAD_UP_DOWN)
            input_buf[j] &= ~RIGHT_DPAD_UP_DOWN;
      }

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#endif
   }

   /* For low-battery mode switch */
   {
      static int pressed;
      if (joy_bits[0] & (1 << RETRO_DEVICE_ID_JOYPAD_X))
      {
         if (!pressed)
         {
            pressed     ^= 1;
            low_battery ^= 1;
         }
      }
      else
         pressed = 0;
   }
}

static void update_geometry(unsigned width, unsigned height)
{
   struct retro_system_av_info info;

   memset(&info, 0, sizeof(info));
   info.timing.fps            = MEDNAFEN_CORE_TIMING_FPS;
   info.timing.sample_rate    = 44100;
   info.geometry.base_width   = width;
   info.geometry.base_height  = height;
   info.geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   info.geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   info.geometry.aspect_ratio = (float) width / (float) height;

   environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info);
}

/* Capacity of the frame audio buffer, in stereo frames. Blip_Buffer_read_samples()
 * interleaves by stepping the destination two int16_t at a time, so the array
 * needs twice this many elements and the value handed to it as a maximum is a
 * frame count, not an element count. The buffers are configured for 44.1kHz over
 * a 50ms window, which caps samples_avail() at 2250 per call, so this leaves
 * roughly 3.6x headroom and still covers a widened rate or window. */
#define SOUND_BUF_FRAMES 8192

void retro_run(void)
{
   static int16_t sound_buf[SOUND_BUF_FRAMES * 2];
   EmulateSpecStruct spec;
   static unsigned width   = 0, height = 0;
   bool resolution_changed = false;

   input_poll_cb();

   update_input();

   bool vr_av_changed = false;
   vb_vr_begin_frame(&vr_av_changed);
   if (vr_av_changed)
   {
      struct retro_system_av_info av;
      retro_get_system_av_info(&av);
      environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
   }

   spec.surface            = &surf;
   spec.VideoFormatChanged = false;
   spec.DisplayRect.x      = 0;
   spec.DisplayRect.y      = 0;
   spec.DisplayRect.w      = 0;
   spec.DisplayRect.h      = 0;
   spec.SoundBufMaxSize    = SOUND_BUF_FRAMES;
   spec.SoundBufSize       = 0;

   if (memcmp(&last_pixel_format, &spec.surface->format, sizeof(struct MDFN_PixelFormat)))
   {
      spec.VideoFormatChanged = true;
      last_pixel_format       = spec.surface->format;
   }

   Emulate(&spec, sound_buf);

   if (width != spec.DisplayRect.w || height != spec.DisplayRect.h)
      resolution_changed = true;

   width  = spec.DisplayRect.w;
   height = spec.DisplayRect.h;

   /* Declare the new geometry before handing over a frame that already uses
    * it, otherwise the frontend scales one frame against the old dimensions. */
   if (resolution_changed && vb_vr_mode() != VB_PRESENT_VR)
      update_geometry(width, height);

   if (vb_vr_mode() != VB_PRESENT_SOFTWARE)
      vb_vr_present(video_cb, surf.pixels, surf.pitchinpix, surf.h, width, height);
   else
   {
#if defined(WANT_32BPP)
      const uint32_t *pix = surf.pixels;
      video_cb(pix, width, height, FB_WIDTH << 2);
#elif defined(WANT_16BPP)
      const uint16_t *pix = surf.pixels16;
      video_cb(pix, width, height, FB_WIDTH << 1);
#endif
   }

   audio_batch_cb(sound_buf, spec.SoundBufSize);

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = MEDNAFEN_CORE_NAME;
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = MEDNAFEN_CORE_VERSION GIT_VERSION;
   info->need_fullpath    = false;
   info->valid_extensions = MEDNAFEN_CORE_EXTENSIONS;
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = MEDNAFEN_CORE_TIMING_FPS;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   info->geometry.aspect_ratio = MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO;

   if (vb_vr_mode() == VB_PRESENT_VR)
   {
      unsigned w, h;
      vb_vr_get_geometry(&w, &h);
      info->geometry.base_width   = info->geometry.max_width  = w;
      info->geometry.base_height  = info->geometry.max_height = h;
      info->geometry.aspect_ratio = (float)w / (float)h;
   }
}

void retro_deinit(void)
{
#if defined(WANT_16BPP)
   if(surf.pixels16)
      free(surf.pixels16);
#elif defined(WANT_32BPP)
   if(surf.pixels)
      free(surf.pixels);
#endif
   surf.pixels8           = NULL;
   surf.pixels16          = NULL;
   surf.pixels            = NULL;
   surf.w                 = 0;
   surf.h                 = 0;
   surf.pitchinpix        = 0;
   surf.format.bpp        = 0;
   surf.format.colorspace = 0;
   surf.format.Rshift     = 0;
   surf.format.Gshift     = 0;
   surf.format.Bshift     = 0;
   surf.format.Ashift     = 0;

   libretro_supports_bitmasks = false;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_PAL; /* 50fps so default this to PAL 50Hz */
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device) { }

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
   libretro_set_core_options(environ_cb);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

size_t retro_serialize_size(void)
{
   StateMem st;

   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
      return 0;

   free(st.data);
   return st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   bool ret          = false;
   uint8_t *_dat     = (uint8_t*)malloc(size);

   if (!_dat)
      return false;

   /* Mednafen can realloc the buffer so we need to ensure this is safe. */
   st.data           = _dat;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = size;

   ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);

   memcpy(data, st.data, size);
   free(st.data);

   return ret;
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;

   return MDFNSS_LoadSM(&st, 0, 0);
}

void *retro_get_memory_data(unsigned type)
{
   switch(type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return WRAM;
      case RETRO_MEMORY_SAVE_RAM:
         return GPRAM;
      default:
         break;
   }

   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   switch(type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return 0x10000;
      case RETRO_MEMORY_SAVE_RAM:
         return GPRAM_Mask + 1;
      default:
         break;
   }

   return 0;
}

void retro_cheat_reset(void) { }
void retro_cheat_set(unsigned a, bool b, const char *c) { }
