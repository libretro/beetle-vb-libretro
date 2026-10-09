#include "libretro_vr.h"

#include <string.h>

#define VB_EYE_W 384   /* native VB eye image width */
#define VB_EYE_H 224

static retro_environment_t vr_environ_cb;
static unsigned vr_option = VB_VR_SCREEN;
static vb_present_mode present_mode = VB_PRESENT_SOFTWARE;
static bool stereo_map;   /* the frontend holds the untracked eye pair */
static bool status_unsupported;

static void clear_map(void)
{
   struct retro_video_views views;

   memset(&views, 0, sizeof(views));
   vr_environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views);
}

/* The two eyes side by side as an untracked pair: the frontend shows them
 * as one stereo screen. Sent every frame while wanted: it covers state
 * loads and run-ahead. */
static void set_stereo_map(bool stereo)
{
   struct retro_video_views views;
   struct retro_video_view  view[2];

   if (!stereo && !stereo_map)
      return;

   memset(&views, 0, sizeof(views));
   memset(view, 0, sizeof(view));
   if (stereo)
   {
      view[0].width   = VB_EYE_W;
      view[0].height  = VB_EYE_H;
      view[0].eye     = RETRO_VIDEO_VIEW_EYE_LEFT;
      view[1].x       = VB_EYE_W;
      view[1].width   = VB_EYE_W;
      view[1].height  = VB_EYE_H;
      view[1].eye     = RETRO_VIDEO_VIEW_EYE_RIGHT;
      views.views     = view;
      views.num_views = 2;
   }

   if (vr_environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views) || !stereo)
      stereo_map = stereo;
}

#if VB_VR_ENABLED

#include <math.h>

#include <glsym/glsym.h>

extern retro_log_printf_t log_cb;

#define VR_LOG(lvl, ...) do { if (log_cb) log_cb((lvl), __VA_ARGS__); } while (0)
#define VB_VR_FILL 0.5f   /* fraction of the narrower side's half-FOV the image fills */
#define VB_VR_HOLD_FRAMES 251   /* about 5 s at 50.27 Hz */

static struct retro_hw_render_callback hw_render;

static struct retro_vr_frame_state  frame_state;
static bool frame_valid;
static unsigned eye_w = 1024, eye_h = 1024;
static unsigned hold_missing;
static bool refused;   /* not retried until HMD or head-locked drops */

static bool   gl_ready;
static GLuint gl_prog, gl_vbo, gl_tex;
static GLint  gl_u_rect, gl_u_tex;
static unsigned tex_w, tex_h;
static GLint  tex_filter;

static float screen_distance = 1.0f;
static float screen_width    = 1.1f;

#ifdef MSB_FIRST
#define VB_SWIZZLE "gba"
#else
#define VB_SWIZZLE "bgr"
#endif

static const char *vs_src =
   "attribute vec3 a_pos;\n"
   "attribute vec2 a_uv;\n"
   "uniform vec4 u_rect;\n"
   "varying vec2 v_uv;\n"
   "void main() {\n"
   "   gl_Position = vec4(a_pos, 1.0);\n"
   "   v_uv = a_uv * u_rect.xy + u_rect.zw;\n"
   "}\n";

static const char *fs_src =
   "#ifdef GL_ES\n"
   "precision mediump float;\n"
   "#endif\n"
   "varying vec2 v_uv;\n"
   "uniform sampler2D u_tex;\n"
   "void main() {\n"
   "   vec4 t = texture2D(u_tex, v_uv);\n"
   "   gl_FragColor = vec4(t." VB_SWIZZLE ", 1.0);\n"
   "}\n";

static GLuint compile_shader(GLenum type, const char *src)
{
   GLint ok = 0;
   GLuint s = glCreateShader(type);

   glShaderSource(s, 1, &src, NULL);
   glCompileShader(s);
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      char buf[512];
      GLsizei len = 0;
      buf[0] = '\0';
      glGetShaderInfoLog(s, sizeof(buf), &len, buf);
      VR_LOG(RETRO_LOG_ERROR, "[Beetle VB] VR shader compile failed: %s\n", buf);
      glDeleteShader(s);
      return 0;
   }
   return s;
}

static void context_reset(void)
{
   GLuint vs, fs;
   GLint ok = 0;

   gl_tex = gl_vbo = gl_prog = 0;
   tex_w = tex_h = 0;
   gl_ready = false;

   rglgen_resolve_symbols(hw_render.get_proc_address);

   vs = compile_shader(GL_VERTEX_SHADER, vs_src);
   fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
   if (!vs || !fs)
   {
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      return;
   }

   gl_prog = glCreateProgram();
   glAttachShader(gl_prog, vs);
   glAttachShader(gl_prog, fs);
   glBindAttribLocation(gl_prog, 0, "a_pos");
   glBindAttribLocation(gl_prog, 1, "a_uv");
   glLinkProgram(gl_prog);
   glDeleteShader(vs);
   glDeleteShader(fs);

   glGetProgramiv(gl_prog, GL_LINK_STATUS, &ok);
   if (!ok)
   {
      VR_LOG(RETRO_LOG_ERROR, "[Beetle VB] VR shader link failed.\n");
      glDeleteProgram(gl_prog);
      gl_prog = 0;
      return;
   }

   gl_u_rect = glGetUniformLocation(gl_prog, "u_rect");
   gl_u_tex  = glGetUniformLocation(gl_prog, "u_tex");
   glGenBuffers(1, &gl_vbo);
   gl_ready = true;
}

static void context_destroy(void)
{
   if (gl_ready)
   {
      if (gl_tex)  glDeleteTextures(1, &gl_tex);
      if (gl_vbo)  glDeleteBuffers(1, &gl_vbo);
      if (gl_prog) glDeleteProgram(gl_prog);
   }
   gl_tex = gl_vbo = gl_prog = 0;
   tex_w = tex_h = 0;
   gl_ready = false;
}

static void ensure_texture(unsigned w, unsigned h, GLint filter)
{
   if (!gl_tex)
      glGenTextures(1, &gl_tex);

   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, gl_tex);

   if (w != tex_w || h != tex_h || filter != tex_filter)
   {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, NULL);
      tex_w = w;
      tex_h = h;
      tex_filter = filter;
   }
}

static void draw_quad(float cx, float cy, float sx, float sy, const float rect[4])
{
   const float v[20] =
   {
      cx - sx, cy + sy, 0.0f,  0.0f, 0.0f,
      cx - sx, cy - sy, 0.0f,  0.0f, 1.0f,
      cx + sx, cy + sy, 0.0f,  1.0f, 0.0f,
      cx + sx, cy - sy, 0.0f,  1.0f, 1.0f
   };
   glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)0);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (const void *)(3 * sizeof(float)));
   glUniform4f(gl_u_rect, rect[0], rect[1], rect[2], rect[3]);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

enum vr_step
{
   VR_FLAT = 0,
   VR_ENTER,
   VR_TRACK,
   VR_WAIT,   /* in VR, HMD missing: repeat the last frame */
   VR_LEAVE
};

/* A video reinit restarts the headset session, and leaving VR changes the
 * geometry, which reinits again, so a lost headset is waited for. */
static enum vr_step hold_step(bool want, bool hmd)
{
   if (present_mode != VB_PRESENT_VR)
   {
      if (!want || !hmd)
         return VR_FLAT;
      hold_missing = 0;
      return VR_ENTER;
   }
   if (!want || (!hmd && ++hold_missing >= VB_VR_HOLD_FRAMES))
      return VR_LEAVE;
   if (hmd)
      hold_missing = 0;
   return hmd ? VR_TRACK : VR_WAIT;
}

/*
 * The core renders one packed framebuffer:
 *
 *   +----------------+----------------+
 *   |      LEFT      |      RIGHT     |
 *   +----------------+----------------+
 *
 * Each view is W x H, so the complete framebuffer is 2W x H.
 */
static bool send_tracked_map(unsigned ew, unsigned eh, unsigned *rec_w, unsigned *rec_h)
{
   struct retro_video_views views;
   struct retro_video_view  view[2];

   memset(&views, 0, sizeof(views));
   memset(view, 0, sizeof(view));
   view[0].width         = ew;
   view[0].height        = eh;
   view[0].eye           = RETRO_VIDEO_VIEW_EYE_LEFT;
   view[1].x             = ew;
   view[1].width         = ew;
   view[1].height        = eh;
   view[1].eye           = RETRO_VIDEO_VIEW_EYE_RIGHT;
   views.views           = view;
   views.num_views       = 2;
   views.flags           = RETRO_VIDEO_VIEWS_FLAG_TRACKED;
   views.reference_space = RETRO_VR_REFERENCE_SPACE_LOCAL;

   if (!vr_environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views))
      return false;
   *rec_w = views.recommended_view_width;
   *rec_h = views.recommended_view_height;
   return true;
}

/* Only a sent map returns the recommended eye size, so the last size is
 * sent first and corrected. */
static bool request_eyes(void)
{
   unsigned ew = eye_w, eh = eye_h, rw = 0, rh = 0;

   if (!send_tracked_map(ew, eh, &rw, &rh))
      return false;
   if (rw && rh && (rw != ew || rh != eh))
   {
      ew = rw;
      eh = rh;
      if (!send_tracked_map(ew, eh, &rw, &rh))
         return false;
   }
   eye_w = ew;
   eye_h = eh;
   return true;
}

/* Returns true while the frame belongs to head-locked VR. */
static bool tracked_begin_frame(unsigned status, bool *av_changed)
{
   const bool hmd = (status & RETRO_VIDEO_VIEWS_STATUS_HMD) != 0;
   bool entered   = false, resized;

   frame_valid = false;
   if (!hmd || vr_option != VB_VR_HEAD_LOCKED)
      refused = false;

   switch (hold_step(vr_option == VB_VR_HEAD_LOCKED && !refused, hmd))
   {
      case VR_ENTER:
         if (!request_eyes())
         {
            clear_map();   /* a first map may have been accepted */
            stereo_map = false;
            refused    = true;
            return false;
         }
         stereo_map   = false;   /* the tracked map replaced it */
         present_mode = VB_PRESENT_VR;
         *av_changed  = true;
         VR_LOG(RETRO_LOG_INFO, "[Beetle VB] VR enabled, eye target %ux%u.\n",
               eye_w, eye_h);
         entered      = true;
         break;
      case VR_TRACK:
         break;
      case VR_WAIT:
         return true;
      case VR_LEAVE:
         clear_map();
         present_mode = VB_PRESENT_GL_FLAT;
         *av_changed  = true;
         VR_LOG(RETRO_LOG_INFO, "[Beetle VB] Headset gone, VR off.\n");
         return false;
      default:
         return false;
   }

   if (!vr_environ_cb(RETRO_ENVIRONMENT_GET_VR_FRAME_STATE, &frame_state))
      return true;

   resized = (frame_state.flags & RETRO_VR_FRAME_TARGET_RESIZED) != 0;
   /* The map every frame, as for the stereo screen. A new size written
    * back is a resize. */
   if (!resized && !entered)
   {
      unsigned rw = 0, rh = 0;
      resized = send_tracked_map(eye_w, eye_h, &rw, &rh)
            && rw && rh && (rw != eye_w || rh != eye_h);
   }
   if (resized)
   {
      if (request_eyes())
      {
         *av_changed = true;
         VR_LOG(RETRO_LOG_DEBUG,
               "[Beetle VB] VR target resized, eye target %ux%u.\n",
               eye_w, eye_h);
      }
      else
         VR_LOG(RETRO_LOG_WARN,
               "[Beetle VB] VR target resized but renegotiation failed.\n");
   }

   frame_valid = true;
   return true;
}

void vb_vr_get_geometry(unsigned *width, unsigned *height)
{
   *width  = eye_w * 2;
   *height = eye_h;
}

void vb_vr_set_screen(float distance_m, float width_m)
{
   if (distance_m < 0.5f)  distance_m = 0.5f;
   if (distance_m > 10.0f) distance_m = 10.0f;
   if (width_m < 0.2f)     width_m = 0.2f;
   if (width_m > 20.0f)    width_m = 20.0f;
   screen_distance = distance_m;
   screen_width    = width_m;
}

void vb_vr_present(retro_video_refresh_t video_cb, const uint32_t *pixels,
      unsigned pitch, unsigned surf_rows, unsigned width, unsigned height)
{
   const bool vr        = (present_mode == VB_PRESENT_VR);
   const unsigned out_w = vr ? eye_w * 2 : width;
   const unsigned out_h = vr ? eye_h : height;

   if (!video_cb)
      return;

   /* Not drawable this frame: ask the frontend to repeat the last one. */
   if (!gl_ready || !pixels || !pitch || !surf_rows || !width || !height ||
       (vr && !frame_valid))
   {
      video_cb(NULL, out_w, out_h, 0);
      return;
   }

   if (height > surf_rows)
      height = surf_rows;

   ensure_texture(pitch, surf_rows, vr ? GL_LINEAR : GL_NEAREST);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pitch, height,
         GL_RGBA, GL_UNSIGNED_BYTE, pixels);

   glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)hw_render.get_current_framebuffer());
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_BLEND);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_STENCIL_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

   glViewport(0, 0, out_w, out_h);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   glUseProgram(gl_prog);
   glUniform1i(gl_u_tex, 0);
   glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);
   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);

   if (vr)
   {
      /* The core renders side-by-side with no separation: left eye in
       * x=[0,ew), right eye in x=[ew,2ew). Each eye viewport gets its own
       * half, head-locked like the real display, letterboxed to the image
       * aspect so it isn't stretched to the headset FOV. */
      const unsigned ew      = VB_EYE_W;
      const unsigned right_x = (width > ew) ? width - ew : 0;  /* right image is the last 384 columns */
      const float img_a = (float)ew / (float)height;
      const float tgt_a = (float)eye_w / (float)eye_h;
      float ipd = 0.063f;
      float conv, hx_want;
      int eye;

      {
         const float *p0 = frame_state.eyes[0].position;
         const float *p1 = frame_state.eyes[1].position;
         const float dx = p1[0] - p0[0], dy = p1[1] - p0[1], dz = p1[2] - p0[2];
         const float m = sqrtf(dx * dx + dy * dy + dz * dz);
         if (m > 0.04f && m < 0.09f)
            ipd = m;
      }

      conv    = (ipd * 0.5f) / screen_distance;           /* tangent shift toward the nose */
      hx_want = (screen_width * 0.5f) / screen_distance;  /* half-width in tangent space */

      for (eye = 0; eye < 2; eye++)
      {
         const float *t = frame_state.eyes[eye].fov_tan;
         const float l = fabsf(t[0]), r = fabsf(t[1]);
         const float u = fabsf(t[2]), d = fabsf(t[3]);
         float rect[4], cx = 0.0f, cy = 0.0f, sx, sy;

         if (l + r > 1e-3f && u + d > 1e-3f)
         {
            float sh     = eye ? -conv : conv;   /* left eye shifts right, right eye left */
            float hx     = hx_want;
            float hy     = hx * (float)height / (float)ew;
            float hx_max = (r - sh < l + sh) ? (r - sh) : (l + sh);
            float hy_max = (u < d) ? u : d;
            float scale  = 1.0f;

            if (hx_max < 0.05f)                  /* can't converge inside the FOV */
            {
               sh     = 0.0f;
               hx_max = (l < r) ? l : r;
            }
            if (hx > hx_max)
               scale = hx_max / hx;
            if (hy * scale > hy_max)
               scale = hy_max / hy;
            hx *= scale;
            hy *= scale;

            cx = (l - r + 2.0f * sh) / (l + r);
            cy = (d - u) / (u + d);
            sx = 2.0f * hx / (l + r);
            sy = 2.0f * hy / (u + d);
         }
         else
         {
            sx = (img_a > tgt_a) ? 1.0f : img_a / tgt_a;
            sy = (img_a > tgt_a) ? tgt_a / img_a : 1.0f;
         }

         rect[0] = (float)(ew - 1)     / (float)pitch;
         rect[1] = (float)(height - 1) / (float)surf_rows;
         rect[2] = ((float)(eye ? right_x : 0) + 0.5f) / (float)pitch;
         rect[3] = 0.5f / (float)surf_rows;

         glViewport(eye ? (GLint)eye_w : 0, 0, eye_w, eye_h);
         draw_quad(cx, cy, sx, sy, rect);
      }
   }
   else
   {
      float rect[4];
      rect[0] = (float)width  / (float)pitch;
      rect[1] = (float)height / (float)surf_rows;
      rect[2] = 0.0f;
      rect[3] = 0.0f;
      draw_quad(0.0f, 0.0f, 1.0f, 1.0f, rect);
   }

   glDisableVertexAttribArray(0);
   glDisableVertexAttribArray(1);
   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   glUseProgram(0);

   video_cb(RETRO_HW_FRAME_BUFFER_VALID, out_w, out_h, 0);
}

#endif /* VB_VR_ENABLED */

void vb_vr_set_option(unsigned option)
{
   vr_option = option;
}

vb_present_mode vb_vr_negotiate(retro_environment_t env)
{
   vr_environ_cb      = env;
   present_mode       = VB_PRESENT_SOFTWARE;
   stereo_map         = false;
   status_unsupported = false;

#if VB_VR_ENABLED
   gl_ready      = false;
   frame_valid   = false;
   refused       = false;

   if (vr_option != VB_VR_HEAD_LOCKED)
      return present_mode;

   memset(&hw_render, 0, sizeof(hw_render));

#if defined(HAVE_OPENGLES2)
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES2;
#else
   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
#endif

   hw_render.context_reset      = context_reset;
   hw_render.context_destroy    = context_destroy;
   hw_render.bottom_left_origin = true;

   /* No HW context: stay on the software path. */
   if (env(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      present_mode = VB_PRESENT_GL_FLAT;
#endif

   return present_mode;
}

vb_present_mode vb_vr_mode(void)
{
   return present_mode;
}

bool vb_vr_begin_frame(bool *av_changed)
{
   const unsigned both = RETRO_VIDEO_VIEWS_STATUS_PRESENTS
                       | RETRO_VIDEO_VIEWS_STATUS_STEREO;
   unsigned status     = 0;

   *av_changed = false;
   if (!vr_environ_cb)
      return false;
   /* Asked once on a frontend without the call: it logs every refusal. */
   if (     !status_unsupported
         && !vr_environ_cb(RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS, &status))
   {
      status_unsupported = true;
      status             = 0;
   }

#if VB_VR_ENABLED
   if (present_mode != VB_PRESENT_SOFTWARE && tracked_begin_frame(status, av_changed))
      return true;
#endif

   set_stereo_map(vr_option != VB_VR_OFF && (status & both) == both);
   return stereo_map;   /* both eyes only with the map behind them */
}

void vb_vr_unload(void)
{
   if (vr_environ_cb && (stereo_map || present_mode == VB_PRESENT_VR))
      clear_map();

   present_mode = VB_PRESENT_SOFTWARE;
   stereo_map   = false;
#if VB_VR_ENABLED
   frame_valid  = false;
#endif
}
