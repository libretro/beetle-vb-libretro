#ifndef _GIT_H
#define _GIT_H

#include <string.h>

#include "video/surface.h"

#include "state.h"

struct MemoryPatch;

struct CheatFormatStruct
{
 const char *FullName;		//"Game Genie", "GameShark", "Pro Action Catplay", etc.
 const char *Description;	// Whatever?
};

struct CheatFormatInfoStruct
{
 unsigned NumFormats;

 struct CheatFormatStruct *Formats;
};

typedef struct
{
	// Pitch(32-bit) must be equal to width and >= the "fb_width" specified in the MDFNGI struct for the emulated system.
	// Height must be >= to the "fb_height" specified in the MDFNGI struct for the emulated system.
	// The framebuffer pointed to by surface->pixels is written to by the system emulation code.
	struct MDFN_Surface *surface;

	// Will be set to TRUE if the video pixel format has changed since the last call to Emulate(), FALSE otherwise.
	// Will be set to TRUE on the first call to the Emulate() function/method
	bool VideoFormatChanged;

	// Set by the system emulation code every frame, to denote the horizontal and vertical offsets of the image, and the size
	// of the image.  If the emulated system sets the elements of LineWidths, then the horizontal offset(x) and width(w) of this structure
	// are ignored while drawing the image.
	MDFN_Rect DisplayRect;

	// Maximum size of the sound buffer, in frames.  Set by the driver code.
	int32 SoundBufMaxSize;

	// Number of frames currently in internal sound buffer.  Set by the system emulation code, to be read by the driver code.
	int32 SoundBufSize;
} EmulateSpecStruct;

#ifdef __cplusplus
extern "C" {
#endif
int StateAction(StateMem *sm, int load, int data_only);
#ifdef __cplusplus
}
#endif

#endif
