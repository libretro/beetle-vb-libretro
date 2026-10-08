#include "libretro_vr.h"

#if VB_VR_ENABLED

#include <math.h>
#include <string.h>

#include <glsym/glsym.h>

extern retro_log_printf_t log_cb;

#define VR_LOG(lvl, ...) do { if (log_cb) log_cb((lvl), __VA_ARGS__); } while (0)
#define VB_EYE_W 384   /* native VB eye image width */
#define VB_VR_FILL 0.5f   /* fraction of the narrower side's half-FOV the image fills */

static retro_environment_t vr_environ_cb;
static struct retro_hw_render_callback hw_render;
static vb_present_mode present_mode = VB_PRESENT_SOFTWARE;

static struct retro_vr_frame_state  frame_state;
static bool frame_valid;
static unsigned eye_w = 1024, eye_h = 1024;

static bool   gl_ready;
static GLuint gl_prog, gl_vbo, gl_tex;
static GLint  gl_u_rect, gl_u_tex;
static unsigned tex_w, tex_h;
static GLint  tex_filter;

static float screen_distance = 1.0f;
static float screen_width    = 1.1f;

extern bool vr_option_enabled;

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

vb_present_mode vb_vr_negotiate(retro_environment_t env, bool allow_vr)
{
   struct retro_video_views views;
   struct retro_video_view  view[2];
   unsigned ew;
   unsigned eh;

   vr_environ_cb = env;
   present_mode  = VB_PRESENT_SOFTWARE;
   gl_ready      = false;
   frame_valid   = false;

   if (!allow_vr)
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
   if (!env(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
      return present_mode;

   /*
    * SET_VIDEO_VIEWS replaces SET_VR_CONTENT_INFO.
    *
    * The core renders one packed framebuffer:
    *
    *   +----------------+----------------+
    *   |      LEFT      |      RIGHT     |
    *   +----------------+----------------+
    *
    * Each view is W x H, so the complete framebuffer is 2W x H.
    */
   ew = eye_w ? eye_w : 1024;
   eh = eye_h ? eye_h : 1024;

   memset(&views, 0, sizeof(views));
   memset(view, 0, sizeof(view));

   view[0].x            = 0;
   view[0].y            = 0;
   view[0].width        = ew;
   view[0].height       = eh;
   view[0].screen       = 0;
   view[0].eye          = RETRO_VIDEO_VIEW_EYE_LEFT;
   view[0].aspect_ratio = 0.0f;

   view[1].x            = ew;
   view[1].y            = 0;
   view[1].width        = ew;
   view[1].height       = eh;
   view[1].screen       = 0;
   view[1].eye          = RETRO_VIDEO_VIEW_EYE_RIGHT;
   view[1].aspect_ratio = 0.0f;

   views.views           = view;
   views.num_views       = 2;
   views.flags           = vr_option_enabled ? 0 : RETRO_VIDEO_VIEWS_FLAG_REQUEST_FLAT;
   views.reference_space = RETRO_VR_REFERENCE_SPACE_LOCAL;
   views.ipd_hint_m      = 0.0f;

   if (!env(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views))
   {
      present_mode = VB_PRESENT_GL_FLAT;

      VR_LOG(RETRO_LOG_INFO,
            "[Beetle VB] VR unavailable, presenting flat through GL.\n");

      return present_mode;
   }

   /*
    * Frontend returns the recommended size for one eye-tagged view.
    * Keep our existing dimensions if the frontend does not provide one.
    */
   eye_w = views.recommended_view_width
         ? views.recommended_view_width
         : ew;

   eye_h = views.recommended_view_height
         ? views.recommended_view_height
         : eh;

   present_mode = VB_PRESENT_VR;

   VR_LOG(RETRO_LOG_INFO,
         "[Beetle VB] VR enabled, eye target %ux%u.\n",
         eye_w, eye_h);

   return present_mode;
}

vb_present_mode vb_vr_mode(void)
{
   return present_mode;
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

void vb_vr_begin_frame(bool *av_changed)
{
   *av_changed = false;
   frame_valid = false;

   if (present_mode != VB_PRESENT_VR)
      return;

   if (!vr_environ_cb(RETRO_ENVIRONMENT_GET_VR_FRAME_STATE, &frame_state))
      return;

   if (frame_state.flags & RETRO_VR_FRAME_TARGET_RESIZED)
   {
      struct retro_video_views views;
      struct retro_video_view  view[2];
      unsigned ew;
      unsigned eh;

      ew = eye_w ? eye_w : 1024;
      eh = eye_h ? eye_h : 1024;

      memset(&views, 0, sizeof(views));
      memset(view, 0, sizeof(view));

      view[0].x            = 0;
      view[0].y            = 0;
      view[0].width        = ew;
      view[0].height       = eh;
      view[0].screen       = 0;
      view[0].eye          = RETRO_VIDEO_VIEW_EYE_LEFT;
      view[0].aspect_ratio = 0.0f;

      view[1].x            = ew;
      view[1].y            = 0;
      view[1].width        = ew;
      view[1].height       = eh;
      view[1].screen       = 0;
      view[1].eye          = RETRO_VIDEO_VIEW_EYE_RIGHT;
      view[1].aspect_ratio = 0.0f;

      views.views           = view;
      views.num_views       = 2;
      views.flags           = vr_option_enabled ? 0 : RETRO_VIDEO_VIEWS_FLAG_REQUEST_FLAT;
      views.reference_space = RETRO_VR_REFERENCE_SPACE_LOCAL;
      views.ipd_hint_m      = 0.0f;

      if (vr_environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, &views))
      {
         eye_w = views.recommended_view_width
               ? views.recommended_view_width
               : ew;

         eye_h = views.recommended_view_height
               ? views.recommended_view_height
               : eh;

         *av_changed = true;

         VR_LOG(RETRO_LOG_DEBUG,
               "[Beetle VB] VR target resized, eye target %ux%u.\n",
               eye_w, eye_h);
      }
      else
      {
         VR_LOG(RETRO_LOG_WARN,
               "[Beetle VB] VR target resized but renegotiation failed.\n");
      }
   }

   frame_valid = true;
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

void vb_vr_unload(void)
{
   if (present_mode == VB_PRESENT_VR && vr_environ_cb)
      vr_environ_cb(RETRO_ENVIRONMENT_SET_VIDEO_VIEWS, NULL);

   present_mode = VB_PRESENT_SOFTWARE;
   frame_valid  = false;
}

#endif /* VB_VR_ENABLED */
