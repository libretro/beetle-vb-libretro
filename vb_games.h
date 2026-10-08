#pragma once

#include <stdint.h>

struct VBGameEntry
{
   uint32_t checksums[16];
   const char *title;
   uint32_t patch_address[512];
};

extern const VBGameEntry VBGames[];
