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

#include <retro_inline.h>

#include "../state_helpers.h"

#include "input.h"
#include "vb.h"

static bool InstantReadHack;

static bool IntPending;

static uint8* data_ptr[2];

static uint16 PadData;
static uint16 PadLatched;

static uint8 SCR;
static uint16 SDR;

#define SCR_S_ABT_DIS	0x01
#define SCR_SI_STAT     0x02
#define SCR_HW_SI       0x04
#define SCR_SOFT_CLK    0x10

#define SCR_PARA_SI     0x20
#define SCR_K_INT_INH   0x80

static uint32 ReadBitPos;
static int32 ReadCounter;

static v810_timestamp_t last_ts;

void VBINPUT_Init(void)
{
   InstantReadHack = true;
}

void VBINPUT_SetInstantReadHack(bool enabled)
{
   InstantReadHack = enabled;
}

void VBINPUT_SetInput(int port, const char *type, void *ptr)
{
   data_ptr[port] = (uint8 *)ptr;
}

uint8 VBINPUT_Read(v810_timestamp_t timestamp, uint32 A)
{
   uint8_t ret = 0;

   VBINPUT_Update(timestamp);

   switch(A & 0xFF)
   {
      case 0x10:
         if(InstantReadHack)
            ret = PadData;
         else
            ret = SDR & 0xFF;
         break;
      case 0x14:
         if(InstantReadHack)
            ret = PadData >> 8;
         else
            ret = SDR >> 8;
         break;
      case 0x28:
         ret = SCR | (0x40 | 0x08 | SCR_HW_SI);
         if(ReadCounter > 0)
            ret |= SCR_SI_STAT;
         break;
   }

   VB_SetEvent(VB_EVENT_INPUT, (ReadCounter > 0) ? (timestamp + ReadCounter) : VB_EVENT_NONONO);

   return(ret);
}

void VBINPUT_Write(v810_timestamp_t timestamp, uint32 A, uint8 V)
{
   VBINPUT_Update(timestamp);

   switch(A & 0xFF)
   {
      case 0x28:
         if((V & SCR_HW_SI) && !(SCR & SCR_S_ABT_DIS) && ReadCounter <= 0)
         {
            PadLatched = PadData;
            ReadBitPos = 0;
            ReadCounter = 640;
         }

         if(V & SCR_S_ABT_DIS)
         {
            ReadCounter = 0;
            ReadBitPos = 0;
         }

         if(V & SCR_K_INT_INH)
         {
            IntPending = false;
            VBIRQ_Assert(VBIRQ_SOURCE_INPUT, IntPending);
         }

         SCR = V & (0x80 | 0x20 | 0x10 | 1);
         break;
   }

   VB_SetEvent(VB_EVENT_INPUT, (ReadCounter > 0) ? (timestamp + ReadCounter) : VB_EVENT_NONONO);
}

static INLINE uint16_t MDFN_de16lsb(const uint8_t *morp)
{
   return(morp[0] | (morp[1] << 8));
}

void VBINPUT_Frame(void)
{
   PadData = (MDFN_de16lsb(data_ptr[0]) << 2) | 0x2 | (*data_ptr[1] & 0x1);
}

v810_timestamp_t VBINPUT_Update(const v810_timestamp_t timestamp)
{
   int32 clocks = timestamp - last_ts;

   if(ReadCounter > 0)
   {
      ReadCounter -= clocks;

      while(ReadCounter <= 0)
      {
         SDR &= ~(1 << ReadBitPos);
         SDR |= PadLatched & (1 << ReadBitPos);

         ReadBitPos++;
         if(ReadBitPos < 16)
            ReadCounter += 640;
         else
         {
            if(!(SCR & SCR_K_INT_INH))
            {
               IntPending = true;
               VBIRQ_Assert(VBIRQ_SOURCE_INPUT, IntPending);
            }
            break;
         }
      }

   }


   last_ts = timestamp;

   return((ReadCounter > 0) ? (timestamp + ReadCounter) : VB_EVENT_NONONO);
}

void VBINPUT_ResetTS(void)
{
   last_ts = 0;
}

void VBINPUT_Power(void)
{
   last_ts = 0;
   PadData = 0;
   PadLatched = 0;
   SDR = 0;
   SCR = 0;
   ReadBitPos = 0;
   ReadCounter = 0;
   IntPending = false;

   VBIRQ_Assert(VBIRQ_SOURCE_INPUT, 0);
}

int VBINPUT_StateAction(StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
      SFVARN(PadData, "PadData"),
      SFVARN(PadLatched, "PadLatched"),
      SFVARN(SCR, "SCR"),
      SFVARN(SDR, "SDR"),
      SFVARN(ReadBitPos, "ReadBitPos"),
      SFVARN(ReadCounter, "ReadCounter"),
      SFVARN_BOOL(IntPending, "IntPending"),
      SFEND
   };

   return MDFNSS_StateAction(sm, load, data_only, StateRegs, "INPUT", false);
}
