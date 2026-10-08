#ifndef LIBRETRO_VB_VR_H
#define LIBRETRO_VB_VR_H

#include <libretro.h>
#include <boolean.h>

/*
 *  SOFTWARE : software framebuffer handed to video_cb.
 *  GL_FLAT  : a HW context was granted but the frontend refused VR; the
 *             software surface is drawn flat.
 *  VR       : stereo eye images are drawn into the frontend's SBS VR target. */
enum vb_present_mode
{
   VB_PRESENT_SOFTWARE = 0,
   VB_PRESENT_GL_FLAT,
   VB_PRESENT_VR
};

#if defined(HAVE_VR) && defined(WANT_32BPP) && (defined(HAVE_OPENGL) || defined(HAVE_OPENGLES2))
#define VB_VR_ENABLED 1
#else
#define VB_VR_ENABLED 0
#endif

#if VB_VR_ENABLED

vb_present_mode vb_vr_negotiate(retro_environment_t env, bool allow_vr);
vb_present_mode vb_vr_mode(void);

/* Full VR framebuffer size (2W x H), for retro_get_system_av_info(). */
void vb_vr_get_geometry(unsigned *width, unsigned *height);

/* Call once at the start of retro_run(). Samples the VR frame state.
 * *av_changed is set when the eye target was resized and the core must
 * re-send its system AV info. */
void vb_vr_begin_frame(bool *av_changed);

/* Virtual screen distance and width in metres (clamped). */
void vb_vr_set_screen(float distance_m, float width_m);

/* Draws the emulated frame and submits it with video_cb.
 *  pixels/pitch/surf_rows : software surface (pitch in pixels, allocated rows)
 *  width/height           : DisplayRect of the emulated image. In VR the
 *                           image is side-by-side, so each eye is width/2. */
void vb_vr_present(retro_video_refresh_t video_cb, const uint32_t *pixels,
      unsigned pitch, unsigned surf_rows, unsigned width, unsigned height);

/* Call from retro_unload_game() and on load failure. */
void vb_vr_unload(void);

#else

static inline vb_present_mode vb_vr_negotiate(retro_environment_t, bool) { return VB_PRESENT_SOFTWARE; }
static inline vb_present_mode vb_vr_mode(void) { return VB_PRESENT_SOFTWARE; }
static inline void vb_vr_get_geometry(unsigned *w, unsigned *h) { *w = 0; *h = 0; }
static inline void vb_vr_begin_frame(bool *av_changed) { *av_changed = false; }
static inline void vb_vr_set_screen(float, float) { } 
static inline void vb_vr_present(retro_video_refresh_t, const uint32_t *, unsigned, unsigned, unsigned, unsigned) { }
static inline void vb_vr_unload(void) { }

#endif

#endif
