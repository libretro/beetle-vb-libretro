/******************************************************************************/
/* Mednafen Virtual Boy Emulation Module                                      */
/******************************************************************************/
/* vb.cpp:
**  Copyright (C) 2010-2017 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "vb.h"
#include "timer.h"
#include "vsu.h"
#include "vip.h"
#include "input.h"

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

uint8 *WRAM = NULL;

uint8 *GPRAM = NULL;
uint32 GPRAM_Mask;

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
   //assert(source >= 0 && source <= 4);

   IRQ_Asserted &= ~(1 << source);

   if(assert)
      IRQ_Asserted |= 1 << source;
 
   RecalcIntLevel();
}



static MDFN_FASTCALL uint8 HWCTRL_Read(v810_timestamp_t &timestamp, uint32 A)
{
   uint8 ret = 0;

   if(A & 0x3)
   { 
      //puts("HWCtrl Bogus Read?");
      return(ret);
   }

   switch(A & 0xFF)
   {
      default: //printf("Unknown HWCTRL Read: %08x\n", A);
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

static MDFN_FASTCALL void HWCTRL_Write(v810_timestamp_t &timestamp, uint32 A, uint8 V)
{
   if(A & 0x3)
   {
      puts("HWCtrl Bogus Write?");
      return;
   }

   switch(A & 0xFF)
   {
      default: //printf("Unknown HWCTRL Write: %08x %02x\n", A, V);
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
              {
                 //printf("GPRAM(Unmapped) Read: %08x\n", A);
              }
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

      case 5: ret = MDFN_de16lsb(&WRAM[A & 0xFFFF]);
              break;

      case 6: if(GPRAM)
                 ret = MDFN_de16lsb(&GPRAM[A & GPRAM_Mask]);
              else
              {
                 //printf("GPRAM(Unmapped) Read: %08x\n", A);
              }
              break;

      case 7: ret = MDFN_de16lsb(&GPROM[A & GPROM_Mask]);
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

      case 5: MDFN_en16lsb(&WRAM[A & 0xFFFF], V);
              break;

      case 6: if(GPRAM)
                 MDFN_en16lsb(&GPRAM[A & GPRAM_Mask], V);
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

   //assert(next_vip_ts > timestamp);
   //assert(next_timer_ts > timestamp);
   //assert(next_input_ts > timestamp);

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


   ForceEventUpdates(0);  //VB_V810->v810_timestamp);
}

void SettingChanged(const char *name)
{
   if(!strcmp(name, "vb.3dmode"))
   {
      // FIXME, TODO (complicated)
      VB3DMode = MDFN_GetSettingUI("vb.3dmode");
      uint32 prescale = MDFN_GetSettingUI("vb.liprescale");
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
      int preset = MDFN_GetSettingI("vb.anaglyph.preset");

      if(preset != ANAGLYPH_PRESET_DISABLED)
      {
         lcolor = AnaglyphPreset_Colors[preset][0];
         rcolor = AnaglyphPreset_Colors[preset][1];
      }
      VIP_SetAnaglyphColors(lcolor, rcolor);
      VIP_SetDefaultColor(MDFN_GetSettingUI("vb.default_color"));
   }
   else if(!strcmp(name, "vb.input.instant_read_hack"))
   {
      VBINPUT_SetInstantReadHack(MDFN_GetSettingB("vb.input.instant_read_hack"));
   }
   else if(!strcmp(name, "vb.instant_display_hack"))
      VIP_SetInstantDisplayHack(MDFN_GetSettingB("vb.instant_display_hack"));
   else if(!strcmp(name, "vb.allow_draw_skip"))
      VIP_SetAllowDrawSkip(MDFN_GetSettingB("vb.allow_draw_skip"));
   else if(!strcmp(name, "vb.ledonscale"))
      VIP_SetLEDOnScale(MDFN_GetSettingF("vb.ledonscale"));
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

static void ReadHeader(const uint8* const rom_data, const uint64 rom_size, VB_HeaderInfo *hi)
{
#if 0
   iconv_t sjis_ict = iconv_open("UTF-8", "shift_jis");

   if(sjis_ict != (iconv_t)-1)
   {
      char *in_ptr, *out_ptr;
      size_t ibl, obl;

      ibl = 20;
      obl = sizeof(hi->game_title) - 1;

      in_ptr = (char*)data + (0xFFFFFDE0 & (rom_size - 1));
      out_ptr = hi->game_title;

      iconv(sjis_ict, (ICONV_CONST char **)&in_ptr, &ibl, &out_ptr, &obl);
      iconv_close(sjis_ict);

      *out_ptr = 0;

      MDFN_RemoveControlChars(hi->game_title);
      MDFN_trim(hi->game_title);
   }
   else
#endif
      hi->game_title[0] = 0;

   hi->game_code = MDFN_de32lsb(rom_data + (0xFFFFFDFB & (rom_size - 1)));
   hi->manf_code = MDFN_de16lsb(rom_data + (0xFFFFFDF9 & (rom_size - 1)));
   hi->version = rom_data[0xFFFFFDFF & (rom_size - 1)];
}

static bool TestMagic(MDFNFILE* gf)
{
   if(gf->ext == "vb" || gf->ext == "vboy")
      return true;

   return false;
}

static MDFN_COLD void Cleanup(void)
{
   VIP_Kill();
 
   if(VB_VSU)
   {
      delete VB_VSU;
      VB_VSU = NULL;
   }
 
   if(VB_V810)
   {
      delete VB_V810;
      VB_V810 = NULL;
      WRAM = NULL;
      GPRAM = NULL;
      GPROM = NULL;
   }
}

static int Load(const uint8_t *rom_data, size_t rom_size)
{
   V810_Emu_Mode cpu_mode;

   VB_InDebugPeek = 0;

   cpu_mode = (V810_Emu_Mode)MDFN_GetSettingI("vb.cpu_emulation");

   if(rom_size != round_up_pow2(rom_size))
   {
      MDFN_Error("VB ROM image size is not a power of 2???");
      return(0);
   }

   if(rom_size < 256)
   {
      MDFN_Error("VB ROM image size is too small??");
      return(0);
   }

   if(rom_size > (1 << 24))
   {
      MDFN_Error("VB ROM image size is too large??");
      return(0);
   }

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
   GPROM_Mask = (rom_size < 65536) ? (65536 - 1) : (rom_size - 1);

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

   memcpy(GPROM, rom_data, rom_size);

   // Mirror ROM images < 64KiB to 64KiB
   for(uint64 i = rom_size; i < 65536; i += rom_size)
   {
      memcpy(GPROM + i, rom_data, rom_size);
   }

   VB_HeaderInfo hinfo;

   ReadHeader(rom_data, rom_size, &hinfo);

   MDFN_printf("Title:     %s\n", hinfo.game_title);
   MDFN_printf("Game ID Code: %u\n", hinfo.game_code);
   MDFN_printf("Manufacturer Code: %d\n", hinfo.manf_code);
   MDFN_printf("Version:   %u\n", hinfo.version);

   MDFN_printf("ROM:       %uKiB\n", (unsigned)(rom_size / 1024));
   //MDFN_printf("ROM MD5:   0x%s\n", md5_context::asciistr(MDFNGameInfo->MD5, 0).c_str());
 
   MDFN_printf("\n");

   MDFN_printf("V810 Emulation Mode: %s\n", (cpu_mode == V810_EMU_MODE_ACCURATE) ? "Accurate" : "Fast");

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

   VIP_Init();
   VB_VSU = new VSU();
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
   SettingChanged("vb.ledonscale");

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
   Cleanup();
}

void VB_ExitLoop(void)
{
   VB_V810->Exit();
}

void Emulate(EmulateSpecStruct *espec)
{
   v810_timestamp_t v810_timestamp;

   MDFNMP_ApplyPeriodicCheats();

   VBINPUT_Frame();

   if(espec->SoundFormatChanged)
      VB_VSU->SetSoundRate(espec->SoundRate);

   VIP_StartFrame(espec);

   v810_timestamp = VB_V810->Run(EventHandler);

   FixNonEvents();
   ForceEventUpdates(v810_timestamp);

   espec->SoundBufSize = VB_VSU->EndFrame((v810_timestamp + VSU_CycleFix) >> 2, espec->SoundBuf, espec->SoundBufMaxSize);

   VSU_CycleFix = (v810_timestamp + VSU_CycleFix) & 3;

   espec->MasterCycles = v810_timestamp;

   TIMER_ResetTS();
   VBINPUT_ResetTS();
   VIP_ResetTS();

   RebaseTS(v810_timestamp);

   VB_V810->ResetTS(0);
}

int StateAction(StateMem *sm, const unsigned load, const bool data_only)
{
   const v810_timestamp_t timestamp = VB_V810->v810_timestamp;

   SFORMAT StateRegs[] =
   {
      SFARRAY(WRAM, 65536),
      SFARRAY(GPRAM, GPRAM_Mask ? (GPRAM_Mask + 1) : 0),
      SFVAR(WCR),
      SFVAR(IRQ_Asserted),
      SFVAR(VSU_CycleFix),
      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN", false);
   if(load)
   {
      VSU_CycleFix &= 3;
   }

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

void DoSimpleCommand(int cmd)
{
   switch(cmd)
   {
      case MDFN_MSC_POWER:
      case MDFN_MSC_RESET: VB_Power(); break;
   }
}

static const MDFNSetting_EnumList V810Mode_List[] =
{
   { "fast", (int)V810_EMU_MODE_FAST, "Fast Mode", "Fast mode trades timing accuracy, cache emulation, and executing from hardware registers and RAM not intended for code use for performance."},
   { "accurate", (int)V810_EMU_MODE_ACCURATE, "Accurate Mode", "Increased timing accuracy, though not perfect, along with cache emulation, at the cost of decreased performance.  Additionally, even the pipeline isn't correctly and fully emulated in this mode." },
   { NULL, 0 },
};

static const MDFNSetting_EnumList VB3DMode_List[] =
{
   { "anaglyph", VB3DMODE_ANAGLYPH, "Anaglyph", "Used in conjunction with classic dual-lens-color glasses." },
   { "cscope",  VB3DMODE_CSCOPE, "CyberScope", "Intended for use with the CyberScope 3D device." },
   { "sidebyside", VB3DMODE_SIDEBYSIDE, "Side-by-Side", "The left-eye image is displayed on the left, and the right-eye image is displayed on the right." },
  // { "overunder", VB3DMODE_OVERUNDER },
   { "vli", VB3DMODE_VLI, "Vertical Line Interlaced", "Vertical lines alternate between left view and right view." },
   { "hli", VB3DMODE_HLI, "Horizontal Line Interlaced", "Horizontal lines alternate between left view and right view." },
   { NULL, 0 },
};

static const MDFNSetting_EnumList AnaglyphPreset_List[] =
{
   { "disabled", ANAGLYPH_PRESET_DISABLED, "Disabled", "Forces usage of custom anaglyph colors." },
   { "0", ANAGLYPH_PRESET_DISABLED },

   { "red_blue", ANAGLYPH_PRESET_RED_BLUE, "Red/Blue", "Classic red/blue anaglyph." },
   { "red_cyan", ANAGLYPH_PRESET_RED_CYAN, "Red/Cyan", "Improved quality red/cyan anaglyph." },
   { "red_electriccyan", ANAGLYPH_PRESET_RED_ELECTRICCYAN, "Red/Electric Cyan", "Alternate version of red/cyan" },
   { "red_green", ANAGLYPH_PRESET_RED_GREEN, "Red/Green" },
   { "green_magenta", ANAGLYPH_PRESET_GREEN_MAGENTA, "Green/Magenta" },
   { "yellow_blue", ANAGLYPH_PRESET_YELLOW_BLUE, "Yellow/Blue" },

   { NULL, 0 },
};

static MDFNSetting VBSettings[] =
{
   { "vb.cpu_emulation", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "CPU emulation mode.", NULL, MDFNST_ENUM, "fast", NULL, NULL, NULL, NULL, V810Mode_List },
   { "vb.input.instant_read_hack", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Input latency reduction hack.", "Reduces latency in some games by 20ms by returning the current pad state, rather than latched state, on serial port data reads.  This hack may cause some homebrew software to malfunction, but it should be relatively safe for commercial official games.", MDFNST_BOOL, "1", NULL, NULL, NULL, SettingChanged },
 
   { "vb.instant_display_hack", MDFNSF_NOFLAGS, "Display latency reduction hack.", "Reduces latency in games by displaying the framebuffer 20ms earlier.  This hack has some potential of causing graphical glitches, so it is disabled by default.", MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },
   { "vb.allow_draw_skip", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Allow draw skipping.", "If vb.instant_display_hack is set to \"1\", and this setting is set to \"1\", then frame-skipping the drawing to the emulated framebuffer will be allowed.  THIS WILL CAUSE GRAPHICAL GLITCHES, AND THEORETICALLY(but unlikely) GAME CRASHES, ESPECIALLY WITH DIRECT FRAMEBUFFER DRAWING GAMES.", MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },

   // FIXME: We're going to have to set up some kind of video mode change notification for changing vb.3dmode while the game is running to work properly.
   { "vb.3dmode", MDFNSF_NOFLAGS, "3D mode.", NULL, MDFNST_ENUM, "anaglyph", NULL, NULL, NULL, /*SettingChanged*/NULL, VB3DMode_List },
   { "vb.liprescale", MDFNSF_NOFLAGS, "Line Interlaced prescale.", NULL, MDFNST_UINT, "2", "1", "10", NULL, NULL },

   { "vb.disable_parallax", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Disable parallax for BG and OBJ rendering.", NULL, MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },
   { "vb.default_color", MDFNSF_NOFLAGS, "Default maximum-brightness color to use in non-anaglyph 3D modes.", NULL, MDFNST_UINT, "0xF0F0F0", "0x000000", "0xFFFFFF", NULL, SettingChanged },

   { "vb.anaglyph.preset", MDFNSF_NOFLAGS, "Anaglyph preset colors.", NULL, MDFNST_ENUM, "red_blue", NULL, NULL, NULL, SettingChanged, AnaglyphPreset_List },
   { "vb.anaglyph.lcolor", MDFNSF_NOFLAGS, "Anaglyph maximum-brightness color for left view.", NULL, MDFNST_UINT, "0xffba00", "0x000000", "0xFFFFFF", NULL, SettingChanged },
   { "vb.anaglyph.rcolor", MDFNSF_NOFLAGS, "Anaglyph maximum-brightness color for right view.", NULL, MDFNST_UINT, "0x00baff", "0x000000", "0xFFFFFF", NULL, SettingChanged },

   { "vb.sidebyside.separation", MDFNSF_NOFLAGS, "Number of pixels to separate L/R views by.", "This setting refers to pixels before vb.xscale(fs) scaling is taken into consideration.  For example, a value of \"100\" here will result in a separation of 300 screen pixels if vb.xscale(fs) is set to \"3\".", MDFNST_UINT, /*"96"*/"0", "0", "1024", NULL, NULL },

   { "vb.3dreverse", MDFNSF_NOFLAGS, "Reverse left/right 3D views.", NULL, MDFNST_BOOL, "0", NULL, NULL, NULL, SettingChanged },

   { "vb.ledonscale", MDFNSF_NOFLAGS, "LED on duration to linear RGB conversion coefficient.", "Setting this higher than the default will cause excessive white crush in at least one game.  A value of 1.0 is close to ideal, other than causing the image to be rather dark.", MDFNST_FLOAT, "1.75", "1.0", "2.0", NULL, SettingChanged },

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
   { ".vb", "Nintendo Virtual Boy" },
   { ".vboy", "Nintendo Virtual Boy" },
   { NULL, NULL }
};

MDFNGI EmulatedVB =
{
   VBSettings,
   MDFN_MASTERCLOCK_FIXED(VB_MASTER_CLOCK),
   0,
   false, // Multires possible?

   0,   // lcm_width
   0,   // lcm_height
   NULL,  // Dummy

   384,   // Nominal width
   224,   // Nominal height

   384,   // Framebuffer width
   256,   // Framebuffer height

   2,     // Number of output sound channels
};


// ========================================================


MDFNGI *MDFNGameInfo = &EmulatedVB;

/* forward declarations */
extern void MDFND_DispMessage(unsigned char *str);

void MDFN_ResetMessages(void)
{
   MDFND_DispMessage(NULL);
}

MDFNGI *MDFNI_LoadGame(const uint8_t *data, size_t size)
{
   MDFNGameInfo = &EmulatedVB;

   // Load per-game settings
   // Maybe we should make a "pgcfg" subdir, and automatically load all files in it?
   // End load per-game settings
   //

   if(Load(data, size) <= 0)
      goto error;

   MDFN_LoadGameCheats(NULL);
   MDFNMP_InstallReadPatches();

   MDFN_ResetMessages();   // Save state, status messages, etc.

   return(MDFNGameInfo);

error:
   MDFNGameInfo = NULL;
   return NULL;
}

void MDFNI_CloseGame(void)
{
   if(!MDFNGameInfo)
      return;

   MDFN_FlushGameCheats(0);

   CloseGame();

   MDFNMP_Kill();

   MDFNGameInfo = NULL;
}

bool MDFNI_InitializeModule(void)
{
   return(1);
}

int MDFNI_Initialize(const char *basedir)
{
   return(1);
}
