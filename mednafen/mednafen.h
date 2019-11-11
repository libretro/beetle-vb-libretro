#ifndef _MEDNAFEN_H
#define _MEDNAFEN_H

#include "mednafen-types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "git.h"
#include "file.h"

#ifdef _WIN32
   #define strcasecmp _stricmp
#endif

#ifndef MAX
   #define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef MIN
   #define MIN(a,b) ((a)<(b)?(a):(b))
#endif

extern MDFNGI *MDFNGameInfo;

#include "settings.h"

void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

void MDFN_MidSync(EmulateSpecStruct *espec);
void MDFN_MidLineUpdate(EmulateSpecStruct *espec, int y);

#include "mednafen-driver.h"
#include "mempatcher.h"
#include "general.h"
#include "mednafen-endian.h"
#include "math_ops.h"

extern void MDFN_PrintError(const char *format, ...);
extern void MDFN_Notify(const char *format, ...);
extern void MDFN_Error(const char *format, ...);
extern void MDFN_printf(const char *format, ...);

#endif
