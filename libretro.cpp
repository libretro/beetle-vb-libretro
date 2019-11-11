#include <stdarg.h>

#include <libretro.h>

#include "libretro_core_options.h"


// ====================================================


#include "mednafen/mednafen.h"
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"

#include "mednafen/vb/vb.h"
#include "mednafen/vb/input.h"


#define MAX_PLAYERS 1
#define MAX_BUTTONS 14

#define STICK_DEADZONE 0x4000
#define RIGHT_DPAD_LEFT 0x1000
#define RIGHT_DPAD_RIGHT 0x0020
#define RIGHT_DPAD_UP 0x0010
#define RIGHT_DPAD_DOWN 0x2000


extern void DoSimpleCommand(int cmd);
extern void SetInput(unsigned port, const char *type, uint8 *ptr);
extern void Emulate(EmulateSpecStruct *espec);
extern void SettingChanged(const char *name);

extern MDFNGI *MDFNI_LoadGame(const uint8_t *data, size_t size);
extern void MDFNI_CloseGame(void);

extern void hookup_ports(bool force);

extern uint8 *WRAM;

extern uint8 *GPRAM;
extern uint32 GPRAM_Mask;


static uint16_t input_buf[MAX_PLAYERS];
static uint16_t battery_voltage = 0;  // normal, low

static bool initial_ports_hookup = false;

void hookup_ports(bool force)
{
   if (initial_ports_hookup && !force)
      return;

   // Possible endian bug ...
   VBINPUT_SetInput(0, "gamepad", (uint8*) &input_buf[0]);
   VBINPUT_SetInput(1, "misc", (uint8*) &battery_voltage);

   initial_ports_hookup = true;
}


static bool dirty_video = true;


// ====================================================


static MDFNGI *game;

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;
retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static bool overscan;
static double last_sound_rate;
static MDFN_PixelFormat last_pixel_format;

static MDFN_Surface *surf;

char retro_base_directory[1024];
std::string retro_base_name;
char retro_save_directory[1024];

static char error_message[1024];


#define MEDNAFEN_CORE_NAME_MODULE "vb"
#define MEDNAFEN_CORE_NAME "Beetle VB"
#define MEDNAFEN_CORE_VERSION "v1.23.0.0"
#define MEDNAFEN_CORE_EXTENSIONS "vb|vboy|bin"
#define MEDNAFEN_CORE_TIMING_FPS 50.27
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 384 * 2
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 224 * 2
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (12.0 / 7.0)
#define FB_WIDTH 384 * 2
#define FB_HEIGHT 224 * 2


#define FB_MAX_HEIGHT FB_HEIGHT

const char *mednafen_core_str = MEDNAFEN_CORE_NAME;

static void check_system_specs(void)
{
   unsigned level = 0;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init(void)
{
   struct retro_log_callback log;
#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   enum retro_pixel_format rgb565 = RETRO_PIXEL_FORMAT_RGB565;
#endif
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else 
      log_cb = NULL;

   MDFNI_InitializeModule();

#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   if (environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565) && log_cb)
      log_cb(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
#endif

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   check_system_specs();
}

void retro_reset(void)
{
   DoSimpleCommand(MDFN_MSC_RESET);
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

static void set_volume (uint32_t *ptr, unsigned number)
{
   switch(number)
   {
      default:
         *ptr = number;
         break;
   }
}

static void check_variables(void)
{
   struct retro_variable var = {0};

   var.key = "vb_3dmode";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_3dmode = setting_vb_3dmode;

      if (strcmp(var.value, "anaglyph") == 0)
         setting_vb_3dmode = VB3DMODE_ANAGLYPH;
      else if (strcmp(var.value, "cyberscope") == 0)
         setting_vb_3dmode = VB3DMODE_CSCOPE;
      else if (strcmp(var.value, "side-by-side") == 0)
         setting_vb_3dmode = VB3DMODE_SIDEBYSIDE;
      else if (strcmp(var.value, "vli") == 0)
         setting_vb_3dmode = VB3DMODE_VLI;
      else if (strcmp(var.value, "hli") == 0)
         setting_vb_3dmode = VB3DMODE_HLI;

      if (old_3dmode != setting_vb_3dmode)
      {
         SettingChanged("vb.3dmode");

         log_cb(RETRO_LOG_INFO, "[%s]: 3D mode changed: %s .\n", mednafen_core_str, var.value);  
      }
   }

   var.key = "vb_anaglyph_preset";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_preset = setting_vb_anaglyph_preset;

      if (strcmp(var.value, "disabled") == 0)
         setting_vb_anaglyph_preset = 0;
      else if (strcmp(var.value, "red & blue") == 0)
         setting_vb_anaglyph_preset = 1;
      else if (strcmp(var.value, "red & cyan") == 0)
         setting_vb_anaglyph_preset = 2;
      else if (strcmp(var.value, "red & electric cyan") == 0)    
         setting_vb_anaglyph_preset = 3;
      else if (strcmp(var.value, "red & green") == 0)
         setting_vb_anaglyph_preset = 4;
      else if (strcmp(var.value, "green & magenta") == 0)
         setting_vb_anaglyph_preset = 5;
      else if (strcmp(var.value, "yellow & blue") == 0)
         setting_vb_anaglyph_preset = 6;

      if (old_preset != setting_vb_anaglyph_preset)
      {
         SettingChanged("vb.anaglyph.preset");

         log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
      }
   }

   var.key = "vb_color_mode";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      unsigned old_color = setting_vb_default_color;

      if (strcmp(var.value, "black & red") == 0)
      {
         setting_vb_lcolor = 0xFF0000;
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & white") == 0)
      {
         setting_vb_lcolor = 0xFFFFFF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & blue") == 0)
      {
         setting_vb_lcolor = 0x0000FF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & cyan") == 0)
      {
         setting_vb_lcolor = 0x00B7EB;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & electric cyan") == 0)
      {
         setting_vb_lcolor = 0x00FFFF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & green") == 0)
      {
         setting_vb_lcolor = 0x00FF00;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & magenta") == 0)
      {
         setting_vb_lcolor = 0xFF00FF;      
         setting_vb_rcolor = 0x000000;
      }
      else if (strcmp(var.value, "black & yellow") == 0)
      {
         setting_vb_lcolor = 0xFFFF00;      
         setting_vb_rcolor = 0x000000;
      }
      setting_vb_default_color = setting_vb_lcolor;

      if (old_color != setting_vb_default_color)
      {
         SettingChanged("vb.default_color");

         log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
      }
   }   

   var.key = "vb_right_analog_to_digital";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         setting_vb_right_analog_to_digital = false;
      else if (strcmp(var.value, "enabled") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = false;
         setting_vb_right_invert_y = false;
      }
      else if (strcmp(var.value, "invert x") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = true;
         setting_vb_right_invert_y = false;
      }
      else if (strcmp(var.value, "invert y") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = false;
         setting_vb_right_invert_y = true;
      }
      else if (strcmp(var.value, "invert both") == 0)
      {
         setting_vb_right_analog_to_digital = true;
         setting_vb_right_invert_x = true;
         setting_vb_right_invert_y = true;
      }
      else
         setting_vb_right_analog_to_digital = false;
   }

   var.key = "vb_cpu_emulation";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      setting_vb_cpu_emulation = !strcmp(var.value, "accurate") ? V810_EMU_MODE_ACCURATE : V810_EMU_MODE_FAST;
   }

   var.key = "vb_ledonscale";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      float old_value = setting_vb_ledonscale;

      setting_vb_ledonscale = atof(var.value);

      if (old_value != setting_vb_ledonscale)
      {
         SettingChanged("vb.ledonscale");
		 dirty_video = true;
      }
   }
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (!info)
      return false;

   static struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Left D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Left D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Left D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "L" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "R" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "Right D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "Right D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Right D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "Right D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Start" },

      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right D-Pad X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right D-Pad Y" },
      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

#ifdef WANT_32BPP
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "Pixel format XRGB8888 not supported by platform, cannot use %s.\n", MEDNAFEN_CORE_NAME);
      return false;
   }
#endif

   overscan = false;
   environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &overscan);

   check_variables();

   game = MDFNI_LoadGame((const uint8_t*)info->data, info->size);
   if (!game)
      return false;

   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

   surf = new MDFN_Surface(NULL, FB_WIDTH, FB_HEIGHT, FB_WIDTH, pix_fmt);

   hookup_ports(true);

   check_variables();

   return game;
}

void retro_unload_game()
{
   if (!game)
      return;

   MDFNI_CloseGame();
}


static void update_input(void)
{
   input_buf[0] = 0;

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_L2, //right d-pad UP
      RETRO_DEVICE_ID_JOYPAD_R3, //right d-pad RIGHT
      RETRO_DEVICE_ID_JOYPAD_RIGHT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_LEFT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_DOWN, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_UP, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_R2, //right d-pad LEFT
      RETRO_DEVICE_ID_JOYPAD_L3, //right d-pad DOWN
   };

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= map[i] != -1u &&
            input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

      if (setting_vb_right_analog_to_digital)
      {
         int16_t analog_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
         int16_t analog_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);

         if (abs(analog_x) > STICK_DEADZONE)
            input_buf[j] |= (analog_x < 0) ^ !setting_vb_right_invert_x ? RIGHT_DPAD_RIGHT : RIGHT_DPAD_LEFT;
         if (abs(analog_y) > STICK_DEADZONE)
            input_buf[j] |= (analog_y < 0) ^ !setting_vb_right_invert_y ? RIGHT_DPAD_DOWN : RIGHT_DPAD_UP;
      }

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#endif
   }
}

static void update_geometry(unsigned width, unsigned height)
{
   struct retro_system_av_info info;

   memset(&info, 0, sizeof(info));
   info.timing.fps            = MEDNAFEN_CORE_TIMING_FPS;
   info.timing.sample_rate    = 44100;
   info.geometry.base_width   = width;
   info.geometry.base_height  = height;
   info.geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   info.geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   info.geometry.aspect_ratio = (float) width / (float) height;

   environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info);
}

static uint64_t video_frames, audio_frames;

void retro_run(void)
{
   MDFNGI *curgame = game;

   input_poll_cb();

   update_input();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[FB_MAX_HEIGHT];
   static unsigned width = 0, height = 0;
   bool resolution_changed = false;
   rects[0].w = ~0;

   EmulateSpecStruct spec = {0};
   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.SoundBuf = sound_buf;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
   spec.SoundVolume = 1.0;
   spec.soundmultiplier = 1.0;
   spec.SoundBufSize = 0;
   spec.VideoFormatChanged = false;
   spec.SoundFormatChanged = false;

   if (dirty_video || memcmp(&last_pixel_format, &spec.surface->format, sizeof(MDFN_PixelFormat)))
   {
      spec.VideoFormatChanged = true;

      last_pixel_format = spec.surface->format;

      dirty_video = false;
   }

   if (spec.SoundRate != last_sound_rate)
   {
      spec.SoundFormatChanged = true;
      last_sound_rate = spec.SoundRate;
   }

   Emulate(&spec);

   int16 *const SoundBuf = spec.SoundBuf + spec.SoundBufSizeALMS * curgame->soundchan;
   int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
   const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;

   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;

   if (width != spec.DisplayRect.w || height != spec.DisplayRect.h)
      resolution_changed = true;

   width  = spec.DisplayRect.w;
   height = spec.DisplayRect.h;

#if defined(WANT_32BPP)
   const uint32_t *pix = surf->pixels;
   video_cb(pix, width, height, FB_WIDTH << 2);
#elif defined(WANT_16BPP)
   const uint16_t *pix = surf->pixels16;
   video_cb(pix, width, height, FB_WIDTH << 1);
#endif

   video_frames++;
   audio_frames += spec.SoundBufSize;

   audio_batch_cb(spec.SoundBuf, spec.SoundBufSize);

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();

   if (resolution_changed)
      update_geometry(width, height);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = MEDNAFEN_CORE_NAME;
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = MEDNAFEN_CORE_VERSION GIT_VERSION;
   info->need_fullpath    = false;
   info->valid_extensions = MEDNAFEN_CORE_EXTENSIONS;
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = MEDNAFEN_CORE_TIMING_FPS;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   info->geometry.aspect_ratio = MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO;
}

void retro_deinit()
{
   delete surf;
   surf = NULL;

   if (log_cb)
   {
      log_cb(RETRO_LOG_INFO, "[%s]: Samples / Frame: %.5f\n",
            mednafen_core_str, (double)audio_frames / video_frames);
      log_cb(RETRO_LOG_INFO, "[%s]: Estimated FPS: %.5f\n",
            mednafen_core_str, (double)video_frames * 44100 / audio_frames);
   }
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC; // FIXME: Regions for other cores.
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
   libretro_set_core_options(environ_cb);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static size_t serialize_size;

size_t retro_serialize_size(void)
{
   StateMem st;

   st.data           = NULL;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = 0;
   st.initial_malloc = 0;

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
      return 0;

   free(st.data);
   return serialize_size = st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   bool ret          = false;
   uint8_t *_dat     = (uint8_t*)malloc(size);

   if (!_dat)
      return false;

   /* Mednafen can realloc the buffer so we need to ensure this is safe. */
   st.data           = _dat;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = size;
   st.initial_malloc = 0;

   ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);

   memcpy(data, st.data, size);
   free(st.data);

   return ret;
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;
   st.initial_malloc = 0;

   return MDFNSS_LoadSM(&st, 0, 0);
}

void *retro_get_memory_data(unsigned type)
{
   switch(type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return WRAM;
      case RETRO_MEMORY_SAVE_RAM:
         return GPRAM;
      default:
         return NULL;
   }
}

size_t retro_get_memory_size(unsigned type)
{
   switch(type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return 0x10000;
      case RETRO_MEMORY_SAVE_RAM:
         return GPRAM_Mask + 1;
      default:
         return 0;
   }
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned, bool, const char *)
{}

void MDFND_DispMessage(unsigned char *str)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "%s\n", str);
}

void MDFND_Message(const char *str)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "%s\n", str);
}

void MDFND_MidSync(const EmulateSpecStruct *)
{}

void MDFN_MidLineUpdate(EmulateSpecStruct *espec, int y)
{
 //MDFND_MidLineUpdate(espec, y);
}


void MDFND_PrintError(const char* err)
{
   if (log_cb)
      log_cb(RETRO_LOG_ERROR, "%s", err);
}

void MDFN_PrintError(char const *format,...)
{
   if (log_cb)
   {
      va_list args;
      va_start (args, format);
      vsnprintf (error_message, 1024-1, format, args);

      log_cb(RETRO_LOG_ERROR, "%s", error_message);

      va_end (args);
   }
}

void MDFN_Error(char const *format,...)
{
   if (log_cb)
   {
      va_list args;
      va_start (args, format);
      vsnprintf (error_message, 1024-1, format, args);

      log_cb(RETRO_LOG_ERROR, "%s", error_message);

      va_end (args);
   }
}

void MDFN_Notify(char const *format,...)
{
   if (log_cb)
   {
      va_list args;
      va_start (args, format);
      vsnprintf (error_message, 1024-1, format, args);

      log_cb(RETRO_LOG_INFO, "%s", error_message);

      va_end (args);
   }
}

void MDFN_printf(char const *format,...)
{
   if (log_cb)
   {
      va_list args;
      va_start (args, format);
      vsnprintf (error_message, 1024-1, format, args);

      log_cb(RETRO_LOG_INFO, "%s", error_message);

      va_end (args);
   }
}

#ifdef _WIN32
static void sanitize_path(std::string &path)
{
   size_t size = path.size();
   for (size_t i = 0; i < size; i++)
      if (path[i] == '/')
         path[i] = '\\';
}
#endif

// Use a simpler approach to make sure that things go right for libretro.
std::string MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
   char slash;
#ifdef _WIN32
   slash = '\\';
#else
   slash = '/';
#endif
   std::string ret;
   switch (type)
   {
      case MDFNMKF_SAV:
         ret = std::string(retro_save_directory) + slash + std::string(retro_base_name) +
            std::string(".") + std::string(cd1);
         break;
      case MDFNMKF_FIRMWARE:
         ret = std::string(retro_base_directory) + slash + std::string(cd1);
#ifdef _WIN32
   sanitize_path(ret); // Because Windows path handling is mongoloid.
#endif
         break;
      default:	  
         break;
   }

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "MDFN_MakeFName: %s\n", ret.c_str());
   return ret;
}
