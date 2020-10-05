#ifndef __VB_VB_H
#define __VB_VB_H

#include <boolean.h>

enum
{
 VB3DMODE_ANAGLYPH = 0,
 VB3DMODE_CSCOPE = 1,
 VB3DMODE_SIDEBYSIDE = 2,
 VB3DMODE_OVERUNDER = 3,
 VB3DMODE_VLI,
 VB3DMODE_HLI
};

enum
{
 VB_EVENT_VIP = 0,
 VB_EVENT_TIMER,
 VB_EVENT_INPUT,
// VB_EVENT_COMM
};

#define VB_MASTER_CLOCK       20000000.0

#define VB_EVENT_NONONO       0x7fffffff

#define VBIRQ_SOURCE_INPUT      0
#define VBIRQ_SOURCE_TIMER      1
#define VBIRQ_SOURCE_EXPANSION  2
#define VBIRQ_SOURCE_COMM       3
#define VBIRQ_SOURCE_VIP        4

#include "../mednafen-types.h"

#ifdef __cplusplus
extern "C" {
#endif

void VB_SetEvent(const int type, const v810_timestamp_t next_timestamp);

void VBIRQ_Assert(int source, bool assert);

void VB_ExitLoop(void);

#ifdef __cplusplus
}
#endif

#endif
