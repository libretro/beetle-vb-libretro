#ifndef __MDFN_MEMPATCHER_DRIVER_H
#define __MDFN_MEMPATCHER_DRIVER_H

#include "mednafen-types.h"

int MDFNI_DecodePAR(const char *code, uint32 *a, uint8 *v, uint8 *c, char *type);
int MDFNI_DecodeGG(const char *str, uint32 *a, uint8 *v, uint8 *c, char *type);
int MDFNI_AddCheat(const char *name, uint32 addr, uint64 val, uint64 compare, char type, unsigned int length, bool bigendian);
int MDFNI_DelCheat(uint32 which);
int MDFNI_ToggleCheat(uint32 which);

void MDFNI_ListCheats(int (*callb)(char *name, uint32 a, uint64 v, uint64 compare, int s, char type, unsigned int length, bool bigendian, void *data), void *data);

int MDFNI_GetCheat(uint32 which, char **name, uint32 *a, uint64 *v, uint64 *compare, int *s, char *type, unsigned int *length, bool *bigendian);
int MDFNI_SetCheat(uint32 which, const char *name, uint32 a, uint64 v, uint64 compare, int s, char type, unsigned int length, bool bigendian);

#endif
