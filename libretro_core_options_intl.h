#ifndef LIBRETRO_CORE_OPTIONS_INTL_H__
#define LIBRETRO_CORE_OPTIONS_INTL_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1500 && _MSC_VER < 1900)
/* https://support.microsoft.com/en-us/kb/980263 */
#pragma execution_character_set("utf-8")
#pragma warning(disable:4566)
#endif

#include <libretro.h>

/*
 ********************************
 * VERSION: 1.3
 ********************************
 *
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_JAPANESE */

/* RETRO_LANGUAGE_FRENCH */

/* RETRO_LANGUAGE_SPANISH */

/* RETRO_LANGUAGE_GERMAN */

/* RETRO_LANGUAGE_ITALIAN */

/* RETRO_LANGUAGE_DUTCH */

/* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */

/* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */

/* RETRO_LANGUAGE_RUSSIAN */

/* RETRO_LANGUAGE_KOREAN */

/* RETRO_LANGUAGE_CHINESE_TRADITIONAL */

/* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */

/* RETRO_LANGUAGE_ESPERANTO */

/* RETRO_LANGUAGE_POLISH */

/* RETRO_LANGUAGE_VIETNAMESE */

/* RETRO_LANGUAGE_ARABIC */

/* RETRO_LANGUAGE_GREEK */

/* RETRO_LANGUAGE_TURKISH */

struct retro_core_option_definition option_defs_tr[] = {
   {
      "vb_3dmode",
      "3B modu",
      "3B modunu seçin. Anaglif - klasik çift lens renkli camlarla birlikte kullanılır. Cyberscope - CyberScope ile kullanılmak üzere tasarlanan 3B cihaz. sidebyside - sol göz resmi solda ve sağ göz resmi sağda görüntülenir. vli - Dikey çizgiler sol ve sağ görünüm arasında değişir. hli - Yatay çizgiler sol ve sağ görünüm arasında değişir.",
      {
         { "anaglyph",  "Anaglif" },
         { "cyberscope",  NULL },
         { "side-by-side",  NULL },
         { "vli", NULL},
         { "hli", NULL},
         { NULL, NULL },
      },
      "anaglyph",
   },
   {
      "vb_anaglyph_preset",
      "Anaglif Ön ayarı",
      "Anaglif önceden ayarlanmış renkler.",
      {
         { "disabled",     "devre dışı" },
         { "red & blue",     NULL },
         { "red & cyan",     NULL },
         { "red & electric cyan",     NULL },
         { "green & magenta",     NULL },
         { "yellow & blue",     NULL },
         { NULL, NULL},
      },
      "disabled",
   },
   {
      "vb_color_mode",
      "Palet",
      "",
      {
         { "black & red", NULL },
         { "black & white",  NULL },
         { "black & blue",  NULL },
         { "black & cyan",  NULL },
         { "black & electric cyan",  NULL },
         { "black & green",  NULL },
         { "black & magenta",  NULL },
         { "black & yellow",  NULL },
         { NULL, NULL},
      },
      "black & red",
   },
   {
      "vb_right_analog_to_digital",
      "Dijital sağ analog",
      "",
      {
         { "disabled",  "devre dışı" },
         { "enabled",  "etkin" },
         { "invert x",  NULL },
         { "invert y",  NULL },
         { "invert both",  NULL },
         { NULL, NULL },
      },
      "disabled",
   },
   {
      "vb_cpu_emulation",
      "CPU emülasyonu (Yeniden başlatma gerekir)",
      "Daha hızlı ve doğru (daha yavaş) emülasyon arasında seçim yapın.",
      {
         { "accurate",      "doğru" },
         { "fast",      "hızlı" },
         { NULL, NULL},
      },
      "disabled",
   },
   { NULL, NULL, NULL, { NULL, NULL }, NULL },
};

#ifdef __cplusplus
}
#endif

#endif