#include "mednafen/mednafen.h"
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#include "libretro.h"

static MDFNGI *game;

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;
retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static bool overscan;
static double last_sound_rate;
static MDFN_PixelFormat last_pixel_format;

static MDFN_Surface *surf;

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
#ifdef WANT_DEBUGGER
#include "mednafen/vb/debug.h"
#endif
#include "mednafen/vb/input.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#include "mednafen/mempatcher.h"
#if 0
#include <iconv.h>
#endif

namespace MDFN_IEN_VB
{

enum
{
 ANAGLYPH_PRESET_DISABLED = 0,
 ANAGLYPH_PRESET_RED_BLUE,
 ANAGLYPH_PRESET_RED_CYAN,
 ANAGLYPH_PRESET_RED_ELECTRICCYAN,
 ANAGLYPH_PRESET_RED_GREEN,
 ANAGLYPH_PRESET_GREEN_MAGENTA,
 ANAGLYPH_PRESET_YELLOW_BLUE,
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


int32 VB_InDebugPeek;

static uint32 VB3DMode;

static Blip_Buffer sbuf[2];

static uint8 *WRAM = NULL;

static uint8 *GPRAM = NULL;
static uint32 GPRAM_Mask;

static uint8 *GPROM = NULL;
static uint32 GPROM_Mask;

V810 *VB_V810 = NULL;

VSU *VB_VSU = NULL;
static uint32 VSU_CycleFix;

static uint8 WCR;

static int32 next_vip_ts, next_timer_ts, next_input_ts;


static uint32 IRQ_Asserted;

static INLINE void RecalcIntLevel(void)
{
 int ilevel = -1;

 for(int i = 4; i >= 0; i--)
 {
  if(IRQ_Asserted & (1 << i))
  {
   ilevel = i;
   break;
  }
 }

 VB_V810->SetInt(ilevel);
}

void VBIRQ_Assert(int source, bool assert)
{
 assert(source >= 0 && source <= 4);

 IRQ_Asserted &= ~(1 << source);

 if(assert)
  IRQ_Asserted |= 1 << source;
 
 RecalcIntLevel();
}



static uint8 HWCTRL_Read(v810_timestamp_t &timestamp, uint32 A)
{
 uint8 ret = 0;

 if(A & 0x3)
 { 
  puts("HWCtrl Bogus Read?");
  return(ret);
 }

 switch(A & 0xFF)
 {
  default: printf("Unknown HWCTRL Read: %08x\n", A);
	   break;

  case 0x18:
  case 0x1C:
  case 0x20: ret = TIMER_Read(timestamp, A);
	     break;

  case 0x24: ret = WCR | 0xFC;
	     break;

  case 0x10:
  case 0x14:
  case 0x28: ret = VBINPUT_Read(timestamp, A);
             break;

 }

 return(ret);
}

static void HWCTRL_Write(v810_timestamp_t &timestamp, uint32 A, uint8 V)
{
 if(A & 0x3)
 {
  puts("HWCtrl Bogus Write?");
  return;
 }

 switch(A & 0xFF)
 {
  default: printf("Unknown HWCTRL Write: %08x %02x\n", A, V);
           break;

  case 0x18:
  case 0x1C:
  case 0x20: TIMER_Write(timestamp, A, V);
	     break;

  case 0x24: WCR = V & 0x3;
	     break;

  case 0x10:
  case 0x14:
  case 0x28: VBINPUT_Write(timestamp, A, V);
	     break;
 }
}

uint8 MDFN_FASTCALL MemRead8(v810_timestamp_t &timestamp, uint32 A)
{
 uint8 ret = 0;
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Read8: %d %08x\n", timestamp, A);

 switch(A >> 24)
 {
  case 0: ret = VIP_Read8(timestamp, A);
	  break;

  case 1: break;

  case 2: ret = HWCTRL_Read(timestamp, A);
	  break;

  case 3: break;
  case 4: break;

  case 5: ret = WRAM[A & 0xFFFF];
	  break;

  case 6: if(GPRAM)
	   ret = GPRAM[A & GPRAM_Mask];
	  else
	   printf("GPRAM(Unmapped) Read: %08x\n", A);
	  break;

  case 7: ret = GPROM[A & GPROM_Mask];
	  break;
 }
 return(ret);
}

uint16 MDFN_FASTCALL MemRead16(v810_timestamp_t &timestamp, uint32 A)
{
 uint16 ret = 0;

 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Read16: %d %08x\n", timestamp, A);


 switch(A >> 24)
 {
  case 0: ret = VIP_Read16(timestamp, A);
	  break;

  case 1: break;

  case 2: ret = HWCTRL_Read(timestamp, A);
	  break;

  case 3: break;

  case 4: break;

  case 5: ret = LoadU16_LE((uint16 *)&WRAM[A & 0xFFFF]);
	  break;

  case 6: if(GPRAM)
           ret = LoadU16_LE((uint16 *)&GPRAM[A & GPRAM_Mask]);
	  else printf("GPRAM(Unmapped) Read: %08x\n", A);
	  break;

  case 7: ret = LoadU16_LE((uint16 *)&GPROM[A & GPROM_Mask]);
	  break;
 }
 return(ret);
}

void MDFN_FASTCALL MemWrite8(v810_timestamp_t &timestamp, uint32 A, uint8 V)
{
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Write8: %d %08x %02x\n", timestamp, A, V);

 switch(A >> 24)
 {
  case 0: VIP_Write8(timestamp, A, V);
          break;

  case 1: VB_VSU->Write((timestamp + VSU_CycleFix) >> 2, A, V);
          break;

  case 2: HWCTRL_Write(timestamp, A, V);
          break;

  case 3: break;

  case 4: break;

  case 5: WRAM[A & 0xFFFF] = V;
          break;

  case 6: if(GPRAM)
           GPRAM[A & GPRAM_Mask] = V;
          break;

  case 7: // ROM, no writing allowed!
          break;
 }
}

void MDFN_FASTCALL MemWrite16(v810_timestamp_t &timestamp, uint32 A, uint16 V)
{
 A &= (1 << 27) - 1;

 //if((A >> 24) <= 2)
 // printf("Write16: %d %08x %04x\n", timestamp, A, V);

 switch(A >> 24)
 {
  case 0: VIP_Write16(timestamp, A, V);
          break;

  case 1: VB_VSU->Write((timestamp + VSU_CycleFix) >> 2, A, V);
          break;

  case 2: HWCTRL_Write(timestamp, A, V);
          break;

  case 3: break;

  case 4: break;

  case 5: StoreU16_LE((uint16 *)&WRAM[A & 0xFFFF], V);
          break;

  case 6: if(GPRAM)
           StoreU16_LE((uint16 *)&GPRAM[A & GPRAM_Mask], V);
          break;

  case 7: // ROM, no writing allowed!
          break;
 }
}

static void FixNonEvents(void)
{
 if(next_vip_ts & 0x40000000)
  next_vip_ts = VB_EVENT_NONONO;

 if(next_timer_ts & 0x40000000)
  next_timer_ts = VB_EVENT_NONONO;

 if(next_input_ts & 0x40000000)
  next_input_ts = VB_EVENT_NONONO;
}

static void EventReset(void)
{
 next_vip_ts = VB_EVENT_NONONO;
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

 return(next_timestamp);
}

static void RebaseTS(const v810_timestamp_t timestamp)
{
 //printf("Rebase: %08x %08x %08x\n", timestamp, next_vip_ts, next_timer_ts);

 assert(next_vip_ts > timestamp);
 assert(next_timer_ts > timestamp);
 assert(next_input_ts > timestamp);

 next_vip_ts -= timestamp;
 next_timer_ts -= timestamp;
 next_input_ts -= timestamp;
}

void VB_SetEvent(const int type, const v810_timestamp_t next_timestamp)
{
 //assert(next_timestamp > VB_V810->v810_timestamp);

 if(type == VB_EVENT_VIP)
  next_vip_ts = next_timestamp;
 else if(type == VB_EVENT_TIMER)
  next_timer_ts = next_timestamp;
 else if(type == VB_EVENT_INPUT)
  next_input_ts = next_timestamp;

 if(next_timestamp < VB_V810->GetEventNT())
  VB_V810->SetEventNT(next_timestamp);
}


static int32 MDFN_FASTCALL EventHandler(const v810_timestamp_t timestamp)
{
 if(timestamp >= next_vip_ts)
  next_vip_ts = VIP_Update(timestamp);

 if(timestamp >= next_timer_ts)
  next_timer_ts = TIMER_Update(timestamp);

 if(timestamp >= next_input_ts)
  next_input_ts = VBINPUT_Update(timestamp);

 return(CalcNextTS());
}

// Called externally from debug.cpp in some cases.
void ForceEventUpdates(const v810_timestamp_t timestamp)
{
 next_vip_ts = VIP_Update(timestamp);
 next_timer_ts = TIMER_Update(timestamp);
 next_input_ts = VBINPUT_Update(timestamp);

 VB_V810->SetEventNT(CalcNextTS());
 //printf("FEU: %d %d %d\n", next_vip_ts, next_timer_ts, next_input_ts);
}

static void VB_Power(void)
{
 memset(WRAM, 0, 65536);

 VIP_Power();
 VB_VSU->Power();
 TIMER_Power();
 VBINPUT_Power();

 EventReset();
 IRQ_Asserted = 0;
 RecalcIntLevel();
 VB_V810->Reset();

 VSU_CycleFix = 0;
 WCR = 0;


 ForceEventUpdates(0);	//VB_V810->v810_timestamp);
}

static void SettingChanged(const char *name)
{
 if(!strcasecmp(name, "vb.3dmode"))
 {
  // FIXME, TODO (complicated)
  //VB3DMode = MDFN_GetSettingUI("vb.3dmode");
  //VIP_Set3DMode(VB3DMode);
 }
 else if(!strcasecmp(name, "vb.disable_parallax"))
 {
  VIP_SetParallaxDisable(MDFN_GetSettingB("vb.disable_parallax"));
 }
 else if(!strcasecmp(name, "vb.anaglyph.lcolor") || !strcasecmp(name, "vb.anaglyph.rcolor") ||
	!strcasecmp(name, "vb.anaglyph.preset") || !strcasecmp(name, "vb.default_color"))

 {
  uint32 lcolor = MDFN_GetSettingUI("vb.anaglyph.lcolor"), rcolor = MDFN_GetSettingUI("vb.anaglyph.rcolor");
  int preset = MDFN_GetSettingI("vb.anaglyph.preset");

  if(preset != ANAGLYPH_PRESET_DISABLED)
  {
   lcolor = AnaglyphPreset_Colors[preset][0];
   rcolor = AnaglyphPreset_Colors[preset][1];
  }
  VIP_SetAnaglyphColors(lcolor, rcolor);
  VIP_SetDefaultColor(MDFN_GetSettingUI("vb.default_color"));
 }
 else if(!strcasecmp(name, "vb.input.instant_read_hack"))
 {
  VBINPUT_SetInstantReadHack(MDFN_GetSettingB("vb.input.instant_read_hack"));
 }
 else if(!strcasecmp(name, "vb.instant_display_hack"))
  VIP_SetInstantDisplayHack(MDFN_GetSettingB("vb.instant_display_hack"));
 else if(!strcasecmp(name, "vb.allow_draw_skip"))
  VIP_SetAllowDrawSkip(MDFN_GetSettingB("vb.allow_draw_skip"));
 else
  abort();


}

struct VB_HeaderInfo
{
 char game_title[256];
 uint32 game_code;
 uint16 manf_code;
 uint8 version;
};

static void ReadHeader(MDFNFILE *fp, VB_HeaderInfo *hi)
{
#if 0
 iconv_t sjis_ict = iconv_open("UTF-8", "shift_jis");

 if(sjis_ict != (iconv_t)-1)
 {
  char *in_ptr, *out_ptr;
  size_t ibl, obl;

  ibl = 20;
  obl = sizeof(hi->game_title) - 1;

  in_ptr = (char*)GET_FDATA_PTR(fp) + (0xFFFFFDE0 & (GET_FSIZE_PTR(fp) - 1));
  out_ptr = hi->game_title;

  iconv(sjis_ict, (ICONV_CONST char **)&in_ptr, &ibl, &out_ptr, &obl);
  iconv_close(sjis_ict);

  *out_ptr = 0;

  MDFN_RemoveControlChars(hi->game_title);
  MDFN_trim(hi->game_title);
 }
 else
  hi->game_title[0] = 0;

 hi->game_code = MDFN_de32lsb(fp->data + (0xFFFFFDFB & (fp->size - 1)));
 hi->manf_code = MDFN_de16lsb(fp->data + (0xFFFFFDF9 & (fp->size - 1)));
 hi->version = fp->data[0xFFFFFDFF & (fp->size - 1)];
#endif
}

static bool TestMagic(const char *name, MDFNFILE *fp)
{
 if(!strcasecmp(GET_FEXTS_PTR(fp), "vb") || !strcasecmp(GET_FEXTS_PTR(fp), "vboy"))
  return(true);

 return(false);
}

static int Load(const char *name, MDFNFILE *fp)
{
 V810_Emu_Mode cpu_mode;
 md5_context md5;


 VB_InDebugPeek = 0;

 cpu_mode = (V810_Emu_Mode)MDFN_GetSettingI("vb.cpu_emulation");

 if(GET_FSIZE_PTR(fp) != round_up_pow2(GET_FSIZE_PTR(fp)))
 {
  puts("VB ROM image size is not a power of 2???");
  return(0);
 }

 if(GET_FSIZE_PTR(fp) < 256)
 {
  puts("VB ROM image size is too small??");
  return(0);
 }

 if(GET_FSIZE_PTR(fp) > (1 << 24))
 {
  puts("VB ROM image size is too large??");
  return(0);
 }

 md5.starts();
 md5.update(GET_FDATA_PTR(fp), GET_FSIZE_PTR(fp));
 md5.finish(MDFNGameInfo->MD5);

 VB_HeaderInfo hinfo;

 ReadHeader(fp, &hinfo);

 MDFN_printf(_("Title:     %s\n"), hinfo.game_title);
 MDFN_printf(_("Game ID Code: %u\n"), hinfo.game_code);
 MDFN_printf(_("Manufacturer Code: %d\n"), hinfo.manf_code);
 MDFN_printf(_("Version:   %u\n"), hinfo.version);

 MDFN_printf(_("ROM:       %dKiB\n"), (int)(GET_FSIZE_PTR(fp) / 1024));
 MDFN_printf(_("ROM MD5:   0x%s\n"), md5_context::asciistr(MDFNGameInfo->MD5, 0).c_str());
 
 MDFN_printf("\n");

 MDFN_printf(_("V810 Emulation Mode: %s\n"), (cpu_mode == V810_EMU_MODE_ACCURATE) ? _("Accurate") : _("Fast"));

 VB_V810 = new V810();
 VB_V810->Init(cpu_mode, true);

 VB_V810->SetMemReadHandlers(MemRead8, MemRead16, NULL);
 VB_V810->SetMemWriteHandlers(MemWrite8, MemWrite16, NULL);

 VB_V810->SetIOReadHandlers(MemRead8, MemRead16, NULL);
 VB_V810->SetIOWriteHandlers(MemWrite8, MemWrite16, NULL);

 for(int i = 0; i < 256; i++)
 {
  VB_V810->SetMemReadBus32(i, false);
  VB_V810->SetMemWriteBus32(i, false);
 }

 std::vector<uint32> Map_Addresses;

 for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
 {
  for(uint64 sub_A = 5 << 24; sub_A < (6 << 24); sub_A += 65536)
  {
   Map_Addresses.push_back(A + sub_A);
  }
 }

 WRAM = VB_V810->SetFastMap(&Map_Addresses[0], 65536, Map_Addresses.size(), "WRAM");
 Map_Addresses.clear();


 // Round up the ROM size to 65536(we mirror it a little later)
 GPROM_Mask = (GET_FSIZE_PTR(fp) < 65536) ? (65536 - 1) : (GET_FSIZE_PTR(fp) - 1);

 for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
 {
  for(uint64 sub_A = 7 << 24; sub_A < (8 << 24); sub_A += GPROM_Mask + 1)
  {
   Map_Addresses.push_back(A + sub_A);
   //printf("%08x\n", (uint32)(A + sub_A));
  }
 }


 GPROM = VB_V810->SetFastMap(&Map_Addresses[0], GPROM_Mask + 1, Map_Addresses.size(), "Cart ROM");
 Map_Addresses.clear();

 // Mirror ROM images < 64KiB to 64KiB
 for(uint64 i = 0; i < 65536; i += GET_FSIZE_PTR(fp))
 {
  memcpy(GPROM + i, GET_FDATA_PTR(fp), GET_FSIZE_PTR(fp));
 }

 GPRAM_Mask = 0xFFFF;

 for(uint64 A = 0; A < 1ULL << 32; A += (1 << 27))
 {
  for(uint64 sub_A = 6 << 24; sub_A < (7 << 24); sub_A += GPRAM_Mask + 1)
  {
   //printf("GPRAM: %08x\n", A + sub_A);
   Map_Addresses.push_back(A + sub_A);
  }
 }


 GPRAM = VB_V810->SetFastMap(&Map_Addresses[0], GPRAM_Mask + 1, Map_Addresses.size(), "Cart RAM");
 Map_Addresses.clear();

 memset(GPRAM, 0, GPRAM_Mask + 1);

 {
  FILE *gp = gzopen(MDFN_MakeFName(MDFNMKF_SAV, 0, "sav").c_str(), "rb");

  if(gp)
  {
   if(gzread(gp, GPRAM, 65536) != 65536)
    puts("Error reading GPRAM");
   gzclose(gp);
  }
 }

 VIP_Init();
 VB_VSU = new VSU(&sbuf[0], &sbuf[1]);
 VBINPUT_Init();

 VB3DMode = MDFN_GetSettingUI("vb.3dmode");
 uint32 prescale = MDFN_GetSettingUI("vb.liprescale");
 uint32 sbs_separation = MDFN_GetSettingUI("vb.sidebyside.separation");

 VIP_Set3DMode(VB3DMode, MDFN_GetSettingUI("vb.3dreverse"), prescale, sbs_separation);


 //SettingChanged("vb.3dmode");
 SettingChanged("vb.disable_parallax");
 SettingChanged("vb.anaglyph.lcolor");
 SettingChanged("vb.anaglyph.rcolor");
 SettingChanged("vb.anaglyph.preset");
 SettingChanged("vb.default_color");

 SettingChanged("vb.instant_display_hack");
 SettingChanged("vb.allow_draw_skip");

 SettingChanged("vb.input.instant_read_hack");

 MDFNGameInfo->fps = (int64)20000000 * 65536 * 256 / (259 * 384 * 4);


 VB_Power();


 #ifdef WANT_DEBUGGER
 VBDBG_Init();
 #endif


 MDFNGameInfo->nominal_width = 384;
 MDFNGameInfo->nominal_height = 224;
 MDFNGameInfo->fb_width = 384;
 MDFNGameInfo->fb_height = 224;

 switch(VB3DMode)
 {
  default: break;

  case VB3DMODE_VLI:
        MDFNGameInfo->nominal_width = 768 * prescale;
        MDFNGameInfo->nominal_height = 224;
        MDFNGameInfo->fb_width = 768 * prescale;
        MDFNGameInfo->fb_height = 224;
        break;

  case VB3DMODE_HLI:
        MDFNGameInfo->nominal_width = 384;
        MDFNGameInfo->nominal_height = 448 * prescale;
        MDFNGameInfo->fb_width = 384;
        MDFNGameInfo->fb_height = 448 * prescale;
        break;

  case VB3DMODE_CSCOPE:
	MDFNGameInfo->nominal_width = 512;
	MDFNGameInfo->nominal_height = 384;
	MDFNGameInfo->fb_width = 512;
	MDFNGameInfo->fb_height = 384;
	break;

  case VB3DMODE_SIDEBYSIDE:
	MDFNGameInfo->nominal_width = 384 * 2 + sbs_separation;
  	MDFNGameInfo->nominal_height = 224;
  	MDFNGameInfo->fb_width = 384 * 2 + sbs_separation;
 	MDFNGameInfo->fb_height = 224;
	break;
 }
 MDFNGameInfo->lcm_width = MDFNGameInfo->fb_width;
 MDFNGameInfo->lcm_height = MDFNGameInfo->fb_height;


 MDFNMP_Init(32768, ((uint64)1 << 27) / 32768);
 MDFNMP_AddRAM(65536, 5 << 24, WRAM);
 if((GPRAM_Mask + 1) >= 32768)
  MDFNMP_AddRAM(GPRAM_Mask + 1, 6 << 24, GPRAM);
 return(1);
}

static void CloseGame(void)
{
 // Only save cart RAM if it has been modified.
 for(unsigned int i = 0; i < GPRAM_Mask + 1; i++)
 {
  if(GPRAM[i])
  {
   if(!MDFN_DumpToFile(MDFN_MakeFName(MDFNMKF_SAV, 0, "sav").c_str(), 6, GPRAM, 65536))
   {

   }
   break;
  }
 }
 //VIP_Kill();
 
 if(VB_VSU)
 {
  delete VB_VSU;
  VB_VSU = NULL;
 }

 /*
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
 */

 if(VB_V810)
 {
  VB_V810->Kill();
  delete VB_V810;
  VB_V810 = NULL;
 }
}

void VB_ExitLoop(void)
{
 VB_V810->Exit();
}

static void Emulate(EmulateSpecStruct *espec)
{
 v810_timestamp_t v810_timestamp;

 MDFNMP_ApplyPeriodicCheats();

 VBINPUT_Frame();

 if(espec->SoundFormatChanged)
 {
  for(int y = 0; y < 2; y++)
  {
   sbuf[y].set_sample_rate(espec->SoundRate ? espec->SoundRate : 44100, 50);
   sbuf[y].clock_rate((long)(VB_MASTER_CLOCK / 4));
   sbuf[y].bass_freq(20);
  }
 }

 VIP_StartFrame(espec);

 v810_timestamp = VB_V810->Run(EventHandler);

 FixNonEvents();
 ForceEventUpdates(v810_timestamp);

 VB_VSU->EndFrame((v810_timestamp + VSU_CycleFix) >> 2);

 if(espec->SoundBuf)
 {
  for(int y = 0; y < 2; y++)
  {
   sbuf[y].end_frame((v810_timestamp + VSU_CycleFix) >> 2);
   espec->SoundBufSize = sbuf[y].read_samples(espec->SoundBuf + y, espec->SoundBufMaxSize, 1);
  }
 }

 VSU_CycleFix = (v810_timestamp + VSU_CycleFix) & 3;

 espec->MasterCycles = v810_timestamp;

 TIMER_ResetTS();
 VBINPUT_ResetTS();
 VIP_ResetTS();

 RebaseTS(v810_timestamp);

 VB_V810->ResetTS(0);
}

}

using namespace MDFN_IEN_VB;

#ifdef WANT_DEBUGGER
static DebuggerInfoStruct DBGInfo =
{
 "shift_jis",
 4,
 2,             // Instruction alignment(bytes)
 32,
 32,
 0x00000000,
 ~0U,

 VBDBG_MemPeek,
 VBDBG_Disassemble,
 NULL,
 NULL,	//ForceIRQ,
 NULL,
 VBDBG_FlushBreakPoints,
 VBDBG_AddBreakPoint,
 VBDBG_SetCPUCallback,
 VBDBG_EnableBranchTrace,
 VBDBG_GetBranchTrace,
 NULL, 	//KING_SetGraphicsDecode,
 VBDBG_SetLogFunc,
};
#endif


static int StateAction(StateMem *sm, int load, int data_only)
{
 const v810_timestamp_t timestamp = VB_V810->v810_timestamp;
 int ret = 1;

 SFORMAT StateRegs[] =
 {
  SFARRAY(WRAM, 65536),
  SFARRAY(GPRAM, GPRAM_Mask ? (GPRAM_Mask + 1) : 0),
  SFVAR(WCR),
  SFVAR(IRQ_Asserted),
  SFVAR(VSU_CycleFix),
  SFEND
 };

 ret &= MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN");

 ret &= VB_V810->StateAction(sm, load, data_only);

 ret &= VB_VSU->StateAction(sm, load, data_only);
 ret &= TIMER_StateAction(sm, load, data_only);
 ret &= VBINPUT_StateAction(sm, load, data_only);
 ret &= VIP_StateAction(sm, load, data_only);

 if(load)
 {
  // Needed to recalculate next_*_ts since we don't bother storing their deltas in save states.
  ForceEventUpdates(timestamp);
 }
 return(ret);
}

static void SetLayerEnableMask(uint64 mask)
{

}

static void DoSimpleCommand(int cmd)
{
 switch(cmd)
 {
  case MDFN_MSC_POWER:
  case MDFN_MSC_RESET: VB_Power(); break;
 }
}

static const MDFNSetting_EnumList V810Mode_List[] =
{
 { "fast", (int)V810_EMU_MODE_FAST, gettext_noop("Fast Mode"), gettext_noop("Fast mode trades timing accuracy, cache emulation, and executing from hardware registers and RAM not intended for code use for performance.")},
 { "accurate", (int)V810_EMU_MODE_ACCURATE, gettext_noop("Accurate Mode"), gettext_noop("Increased timing accuracy, though not perfect, along with cache emulation, at the cost of decreased performance.  Additionally, even the pipeline isn't correctly and fully emulated in this mode.") },
 { NULL, 0 },
};

static const MDFNSetting_EnumList VB3DMode_List[] =
{
 { "anaglyph", VB3DMODE_ANAGLYPH, gettext_noop("Anaglyph"), gettext_noop("Used in conjunction with classic dual-lens-color glasses.") },
 { "cscope",  VB3DMODE_CSCOPE, gettext_noop("CyberScope"), gettext_noop("Intended for use with the CyberScope 3D device.") },
 { "sidebyside", VB3DMODE_SIDEBYSIDE, gettext_noop("Side-by-Side"), gettext_noop("The left-eye image is displayed on the left, and the right-eye image is displayed on the right.") },
// { "overunder", VB3DMODE_OVERUNDER },
 { "vli", VB3DMODE_VLI, gettext_noop("Vertical Line Interlaced"), gettext_noop("Vertical lines alternate between left view and right view.") },
 { "hli", VB3DMODE_HLI, gettext_noop("Horizontal Line Interlaced"), gettext_noop("Horizontal lines alternate between left view and right view.") },
 { NULL, 0 },
};

static const MDFNSetting_EnumList AnaglyphPreset_List[] =
{
 { "disabled", ANAGLYPH_PRESET_DISABLED, gettext_noop("Disabled"), gettext_noop("Forces usage of custom anaglyph colors.") },
 { "0", ANAGLYPH_PRESET_DISABLED },

 { "red_blue", ANAGLYPH_PRESET_RED_BLUE, gettext_noop("Red/Blue"), gettext_noop("Classic red/blue anaglyph.") },
 { "red_cyan", ANAGLYPH_PRESET_RED_CYAN, gettext_noop("Red/Cyan"), gettext_noop("Improved quality red/cyan anaglyph.") },
 { "red_electriccyan", ANAGLYPH_PRESET_RED_ELECTRICCYAN, gettext_noop("Red/Electric Cyan"), gettext_noop("Alternate version of red/cyan") },
 { "red_green", ANAGLYPH_PRESET_RED_GREEN, gettext_noop("Red/Green") },
 { "green_magenta", ANAGLYPH_PRESET_GREEN_MAGENTA, gettext_noop("Green/Magenta") },
 { "yellow_blue", ANAGLYPH_PRESET_YELLOW_BLUE, gettext_noop("Yellow/Blue") },

 { NULL, 0 },
};

static MDFNSetting VBSettings[] =
{
 { "vb.cpu_emulation", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, gettext_noop("CPU emulation mode."), NULL, MDFNST_ENUM, "fast", NULL, NULL, NULL, NULL, V810Mode_List },
 { "vb.input.instant_read_hack", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, gettext_noop("Input latency reduction hack."), gettext_noop("Reduces latency in some games by 20ms by returning the current pad state, rather than latched state, on serial port data reads.  This hack may cause some homebrew software to malfunction, but it should be relatively safe for commercial official games."), MDFNST_BOOL, "1", NULL, NULL, NULL, SettingChanged },
 
 { "vb.instant_display_hack", MDFNSF_NOFLAGS, gettext_noop("Display latency reduction hack."), gettext_noop("Reduces latency in games by displaying the framebuffer 20ms earlier.  This hack has some potential of causing graphical glitches, so it is disabled by default."), MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },
 { "vb.allow_draw_skip", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, gettext_noop("Allow draw skipping."), gettext_noop("If vb.instant_display_hack is set to \"1\", and this setting is set to \"1\", then frame-skipping the drawing to the emulated framebuffer will be allowed.  THIS WILL CAUSE GRAPHICAL GLITCHES, AND THEORETICALLY(but unlikely) GAME CRASHES, ESPECIALLY WITH DIRECT FRAMEBUFFER DRAWING GAMES."), MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },

 // FIXME: We're going to have to set up some kind of video mode change notification for changing vb.3dmode while the game is running to work properly.
 { "vb.3dmode", MDFNSF_NOFLAGS, gettext_noop("3D mode."), NULL, MDFNST_ENUM, "anaglyph", NULL, NULL, NULL, /*SettingChanged*/NULL, VB3DMode_List },
 { "vb.liprescale", MDFNSF_NOFLAGS, gettext_noop("Line Interlaced prescale."), NULL, MDFNST_UINT, "2", "1", "10", NULL, NULL },

 { "vb.disable_parallax", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, gettext_noop("Disable parallax for BG and OBJ rendering."), NULL, MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },
 { "vb.default_color", MDFNSF_NOFLAGS, gettext_noop("Default maximum-brightness color to use in non-anaglyph 3D modes."), NULL, MDFNST_UINT, "0xF0F0F0", "0x000000", "0xFFFFFF", NULL, SettingChanged },

 { "vb.anaglyph.preset", MDFNSF_NOFLAGS, gettext_noop("Anaglyph preset colors."), NULL, MDFNST_ENUM, "red_blue", NULL, NULL, NULL, SettingChanged, AnaglyphPreset_List },
 { "vb.anaglyph.lcolor", MDFNSF_NOFLAGS, gettext_noop("Anaglyph maximum-brightness color for left view."), NULL, MDFNST_UINT, "0xffba00", "0x000000", "0xFFFFFF", NULL, SettingChanged },
 { "vb.anaglyph.rcolor", MDFNSF_NOFLAGS, gettext_noop("Anaglyph maximum-brightness color for right view."), NULL, MDFNST_UINT, "0x00baff", "0x000000", "0xFFFFFF", NULL, SettingChanged },

 { "vb.sidebyside.separation", MDFNSF_NOFLAGS, gettext_noop("Number of pixels to separate L/R views by."), gettext_noop("This setting refers to pixels before vb.xscale(fs) scaling is taken into consideration.  For example, a value of \"100\" here will result in a separation of 300 screen pixels if vb.xscale(fs) is set to \"3\"."), MDFNST_UINT, /*"96"*/"0", "0", "1024", NULL, NULL },

 { "vb.3dreverse", MDFNSF_NOFLAGS, gettext_noop("Reverse left/right 3D views."), NULL, MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },
 { NULL }
};


static const InputDeviceInputInfoStruct IDII[] =
{
 { "a", "A", 7, IDIT_BUTTON_CAN_RAPID,  NULL },
 { "b", "B", 6, IDIT_BUTTON_CAN_RAPID, NULL },
 { "rt", "Right-Back", 13, IDIT_BUTTON, NULL },
 { "lt", "Left-Back", 12, IDIT_BUTTON, NULL },

 { "up-r", "UP ↑ (Right D-Pad)", 8, IDIT_BUTTON, "down-r" },
 { "right-r", "RIGHT → (Right D-Pad)", 11, IDIT_BUTTON, "left-r" },

 { "right-l", "RIGHT → (Left D-Pad)", 3, IDIT_BUTTON, "left-l" },
 { "left-l", "LEFT ← (Left D-Pad)", 2, IDIT_BUTTON, "right-l" },
 { "down-l", "DOWN ↓ (Left D-Pad)", 1, IDIT_BUTTON, "up-l" },
 { "up-l", "UP ↑ (Left D-Pad)", 0, IDIT_BUTTON, "down-l" },

 { "start", "Start", 5, IDIT_BUTTON, NULL },
 { "select", "Select", 4, IDIT_BUTTON, NULL },

 { "left-r", "LEFT ← (Right D-Pad)", 10, IDIT_BUTTON, "right-r" },
 { "down-r", "DOWN ↓ (Right D-Pad)", 9, IDIT_BUTTON, "up-r" },
};

static InputDeviceInfoStruct InputDeviceInfo[] =
{
 {
  "gamepad",
  "Gamepad",
  NULL,
  NULL,
  sizeof(IDII) / sizeof(InputDeviceInputInfoStruct),
  IDII,
 }
};

static const InputPortInfoStruct PortInfo[] =
{
 { "builtin", "Built-In", sizeof(InputDeviceInfo) / sizeof(InputDeviceInfoStruct), InputDeviceInfo, "gamepad" }
};

static InputInfoStruct InputInfo =
{
 sizeof(PortInfo) / sizeof(InputPortInfoStruct),
 PortInfo
};


static const FileExtensionSpecStruct KnownExtensions[] =
{
 { ".vb", gettext_noop("Nintendo Virtual Boy") },
 { ".vboy", gettext_noop("Nintendo Virtual Boy") },
 { NULL, NULL }
};

MDFNGI EmulatedVB =
{
 "vb",
 "Virtual Boy",
 KnownExtensions,
 MODPRIO_INTERNAL_HIGH,
 #ifdef WANT_DEBUGGER
 &DBGInfo,
 #else
 NULL,		// Debug info
 #endif
 &InputInfo,	//
 Load,
 TestMagic,
 NULL,
 NULL,
 CloseGame,
 SetLayerEnableMask,
 NULL,		// Layer names, null-delimited
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 false,
 StateAction,
 Emulate,
 VBINPUT_SetInput,
 DoSimpleCommand,
 VBSettings,
 MDFN_MASTERCLOCK_FIXED(VB_MASTER_CLOCK),
 0,
 false, // Multires possible?

 0,   // lcm_width
 0,   // lcm_height
 NULL,  // Dummy

 384,	// Nominal width
 224,	// Nominal height

 384,	// Framebuffer width
 256,	// Framebuffer height

 2,     // Number of output sound channels
};

static bool failed_init;

static void hookup_ports(bool force);

static bool initial_ports_hookup = false;

std::string retro_base_directory;
std::string retro_base_name;
std::string retro_save_directory;

static void set_basename(const char *path)
{
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');

   if (base)
      retro_base_name = base + 1;
   else
      retro_base_name = path;

   retro_base_name = retro_base_name.substr(0, retro_base_name.find_last_of('.'));
}

#define MEDNAFEN_CORE_NAME_MODULE "vb"
#define MEDNAFEN_CORE_NAME "Mednafen VB"
#define MEDNAFEN_CORE_VERSION "v0.9.36.1"
#define MEDNAFEN_CORE_EXTENSIONS "vb|vboy|bin"
#define MEDNAFEN_CORE_TIMING_FPS 50.27
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 384
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 224
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 384
#define FB_HEIGHT 224


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
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else 
      log_cb = NULL;

   MDFNI_InitializeModule();

   const char *dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
   {
      retro_base_directory = dir;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_base_directory.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_base_directory = retro_base_directory.substr(0, last);

      MDFNI_Initialize(retro_base_directory.c_str());
   }
   else
   {
      /* TODO: Add proper fallback */
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "System directory is not defined. Fallback on using same dir as ROM for system directory later ...\n");
      failed_init = true;
   }
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
   {
	  // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *dir ? dir : retro_base_directory;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_save_directory.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_save_directory = retro_save_directory.substr(0, last);      
   }
   else
   {
      /* TODO: Add proper fallback */
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "Save directory is not defined. Fallback on using SYSTEM directory ...\n");
	  retro_save_directory = retro_base_directory;
   }      

#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   enum retro_pixel_format rgb565 = RETRO_PIXEL_FORMAT_RGB565;
   if (environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565) && log_cb)
      log_cb(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
#endif

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   check_system_specs();
}

void retro_reset(void)
{
   game->DoSimpleCommand(MDFN_MSC_RESET);
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

    var.key = "vb_color_mode";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
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
      log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
   }   
   
    var.key = "vb_anaglyph_preset";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
   
   
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

      log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
   }      
}

#define MAX_PLAYERS 1
#define MAX_BUTTONS 14
static uint16_t input_buf[MAX_PLAYERS];

static void hookup_ports(bool force)
{
   MDFNGI *currgame = game;

   if (initial_ports_hookup && !force)
      return;

   // Possible endian bug ...
   currgame->SetInput(0, "gamepad", &input_buf[0]);

   initial_ports_hookup = true;
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (failed_init)
      return false;

#ifdef WANT_32BPP
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "Pixel format XRGB8888 not supported by platform, cannot use %s.\n", MEDNAFEN_CORE_NAME);
      return false;
   }
#endif

   overscan = false;
   environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &overscan);

   set_basename(info->path);

   check_variables();

   game = MDFNI_LoadGame(MEDNAFEN_CORE_NAME_MODULE, info->path);
   if (!game)
      return false;

   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));
   
   surf = new MDFN_Surface(NULL, FB_WIDTH, FB_HEIGHT, FB_WIDTH, pix_fmt);

   hookup_ports(true);

   check_variables();

   return game;
}

void retro_unload_game()
{
   if (!game)
      return;

   MDFNI_CloseGame();
}



static void update_input(void)
{
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

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= map[i] != -1u &&
            input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#endif
   }

}

static uint64_t video_frames, audio_frames;


void retro_run()
{
   MDFNGI *curgame = game;

   input_poll_cb();

   update_input();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[FB_MAX_HEIGHT];
   rects[0].w = ~0;

   EmulateSpecStruct spec = {0};
   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.SoundBuf = sound_buf;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
   spec.SoundVolume = 1.0;
   spec.soundmultiplier = 1.0;
   spec.SoundBufSize = 0;
   spec.VideoFormatChanged = false;
   spec.SoundFormatChanged = false;

   if (memcmp(&last_pixel_format, &spec.surface->format, sizeof(MDFN_PixelFormat)))
   {
      spec.VideoFormatChanged = TRUE;

      last_pixel_format = spec.surface->format;
   }

   if (spec.SoundRate != last_sound_rate)
   {
      spec.SoundFormatChanged = true;
      last_sound_rate = spec.SoundRate;
   }

   curgame->Emulate(&spec);

   int16 *const SoundBuf = spec.SoundBuf + spec.SoundBufSizeALMS * curgame->soundchan;
   int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
   const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;

   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;

   unsigned width  = spec.DisplayRect.w;
   unsigned height = spec.DisplayRect.h;

#if defined(WANT_32BPP)
   const uint32_t *pix = surf->pixels;
   video_cb(pix, width, height, FB_WIDTH << 2);
#elif defined(WANT_16BPP)
   const uint16_t *pix = surf->pixels16;
   video_cb(pix, width, height, FB_WIDTH << 1);
#endif

   video_frames++;
   audio_frames += spec.SoundBufSize;

   audio_batch_cb(spec.SoundBuf, spec.SoundBufSize);

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = MEDNAFEN_CORE_NAME;
   info->library_version  = MEDNAFEN_CORE_VERSION;
   info->need_fullpath    = true;
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
}

void retro_deinit()
{
   delete surf;
   surf = NULL;

   if (log_cb)
   {
      log_cb(RETRO_LOG_INFO, "[%s]: Samples / Frame: %.5f\n",
            mednafen_core_str, (double)audio_frames / video_frames);
      log_cb(RETRO_LOG_INFO, "[%s]: Estimated FPS: %.5f\n",
            mednafen_core_str, (double)video_frames * 44100 / audio_frames);
   }
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC; // FIXME: Regions for other cores.
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
   MDFNGI *currgame = (MDFNGI*)game;

   if (!currgame)
      return;
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

    static const struct retro_variable vars[] = {
	  { "vb_anaglyph_preset", "Anaglyph preset (restart); disabled|red & blue|red & cyan|red & electric cyan|red & green|green & magenta|yellow & blue" },
      { "vb_color_mode", "Palette (restart); black & red|black & white" },
      { NULL, NULL },
   };
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
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

static size_t serialize_size;

size_t retro_serialize_size(void)
{
   MDFNGI *curgame = (MDFNGI*)game;
   //if (serialize_size)
   //   return serialize_size;

   if (!curgame->StateAction)
   {
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "[mednafen]: Module %s doesn't support save states.\n", curgame->shortname);
      return 0;
   }

   StateMem st;
   memset(&st, 0, sizeof(st));

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
   {
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "[mednafen]: Module %s doesn't support save states.\n", curgame->shortname);
      return 0;
   }

   free(st.data);
   return serialize_size = st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   memset(&st, 0, sizeof(st));
   st.data     = (uint8_t*)data;
   st.malloced = size;

   return MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;
   memset(&st, 0, sizeof(st));
   st.data = (uint8_t*)data;
   st.len  = size;

   return MDFNSS_LoadSM(&st, 0, 0);
}

void *retro_get_memory_data(unsigned)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned)
{
   return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned, bool, const char *)
{}

#ifdef _WIN32
static void sanitize_path(std::string &path)
{
   size_t size = path.size();
   for (size_t i = 0; i < size; i++)
      if (path[i] == '/')
         path[i] = '\\';
}
#endif

// Use a simpler approach to make sure that things go right for libretro.
std::string MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
   char slash;
#ifdef _WIN32
   slash = '\\';
#else
   slash = '/';
#endif
   std::string ret;
   switch (type)
   {
      case MDFNMKF_SAV:
         ret = retro_save_directory +slash + retro_base_name +
            std::string(".") +
#ifndef _XBOX
	    md5_context::asciistr(MDFNGameInfo->MD5, 0) + std::string(".") +
#endif
            std::string(cd1);
         break;
      case MDFNMKF_FIRMWARE:
         ret = retro_base_directory + slash + std::string(cd1);
#ifdef _WIN32
   sanitize_path(ret); // Because Windows path handling is mongoloid.
#endif
         break;
      default:	  
         break;
   }

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "MDFN_MakeFName: %s\n", ret.c_str());
   return ret;
}

void MDFND_DispMessage(unsigned char *str)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "%s\n", str);
}

void MDFND_Message(const char *str)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "%s\n", str);
}

void MDFND_MidSync(const EmulateSpecStruct *)
{}

void MDFN_MidLineUpdate(EmulateSpecStruct *espec, int y)
{
 //MDFND_MidLineUpdate(espec, y);
}

void MDFND_PrintError(const char* err)
{
   if (log_cb)
      log_cb(RETRO_LOG_ERROR, "%s\n", err);
}
