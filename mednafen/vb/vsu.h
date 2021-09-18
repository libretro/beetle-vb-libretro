#ifndef __VB_VSU_H
#define __VB_VSU_H

#include "../include/blip/Blip_Buffer.h"

#include "../state.h"

#ifdef __cplusplus
extern "C" {
#endif

void VSU_Init(Blip_Buffer *bb_l, Blip_Buffer *bb_r) MDFN_COLD;

void VSU_Power(void) MDFN_COLD;

void VSU_Write(int32 timestamp, uint32 A, uint8 V);

void VSU_EndFrame(int32 timestamp);

int VSU_StateAction(StateMem *sm, int load, int data_only);

uint8 VSU_PeekWave(const unsigned int which, uint32 Address);
void VSU_PokeWave(const unsigned int which, uint32 Address, uint8 value);

uint8 VSU_PeekModWave(uint32 Address);
void VSU_PokeModWave(uint32 Address, uint8 value);

#ifdef __cplusplus
}
#endif

#endif
