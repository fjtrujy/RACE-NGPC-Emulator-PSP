#include "libretro.h"
#include "log.h"
#include <string.h>
#include <fstream>

#include "../StdAfx.h"
#include "../state.h"
#include "../neopopsound.h"
#include "../input.h"
#include "../flash.h"
#include "../tlcs900h.h"
#include "../memory.h"
#include "../graphics.h"

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

#define RACE_NAME_MODULE "race"
#define RACE_NAME "Race"
#define RACE_VERSION "v1.23.0.0"
#define RACE_EXTENSIONS "ngp|ngc|ngpc|npc"
#define RACE_TIMING_FPS 60.25
#define RACE_GEOMETRY_BASE_W 160 
#define RACE_GEOMETRY_BASE_H 152
#define RACE_GEOMETRY_MAX_W 160
#define RACE_GEOMETRY_MAX_H 152
#define RACE_GEOMETRY_ASPECT_RATIO 1.05

#define FB_WIDTH 160
#define FB_HEIGHT 152

// core options
static int RETRO_SAMPLE_RATE = 44100;

static int RETRO_PIX_BYTES = 2;
static int RETRO_PIX_DEPTH = 16;

ngp_screen* screen;
int setting_ngp_language;
int gfx_hacks;
int tipo_consola;

static bool newFrame = false;
static bool initialized = false;

char retro_save_directory[2048];

struct map { unsigned retro; unsigned ngp; };

static map btn_map[] = {
   { RETRO_DEVICE_ID_JOYPAD_A, 0x10 },
   { RETRO_DEVICE_ID_JOYPAD_B, 0x20 },
   { RETRO_DEVICE_ID_JOYPAD_RIGHT, 0x08 },
   { RETRO_DEVICE_ID_JOYPAD_LEFT, 0x04 },
   { RETRO_DEVICE_ID_JOYPAD_UP, 0x01 },
   { RETRO_DEVICE_ID_JOYPAD_DOWN, 0x02 },
   { RETRO_DEVICE_ID_JOYPAD_SELECT, 0x40 },
};

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

static void update_geometry()
{
   struct retro_system_av_info info;

   retro_get_system_av_info(&info);
   environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &info);
}

void graphics_paint()
{
   if(RETRO_PIX_BYTES == 2)
      video_cb(screen->pixels, screen->w, screen->h, FB_WIDTH * RETRO_PIX_BYTES);
   else if(RETRO_PIX_BYTES == 4)
      video_cb(screen->pixels, screen->w, screen->h, FB_WIDTH * RETRO_PIX_BYTES);
}

static void check_variables(void)
{
   struct retro_variable var = {0};

   var.key = "ngp_language";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      // user must manually restart core for change to happen
      if (!strcmp(var.value, "japanese"))
         setting_ngp_language = 0;
      else if (!strcmp(var.value, "english"))
         setting_ngp_language = 1;
   }

   var.key = "ngp_sound_sample_rate";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      static bool once = false;

      if(!once)
      {
         RETRO_SAMPLE_RATE = atoi(var.value);
         once = true;
      }
   }

   RETRO_PIX_BYTES = 2;
   RETRO_PIX_DEPTH = 16;
   /*
   var.key = "ngp_gfx_colors";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      static bool once = false;

      if(!once)
      {
         if (strcmp(var.value, "16bit") == 0)
         {
            RETRO_PIX_BYTES = 2;
            RETRO_PIX_DEPTH = 16;
         }
         else if (strcmp(var.value, "24bit") == 0)
         {
            RETRO_PIX_BYTES = 4;
            RETRO_PIX_DEPTH = 24;
         }
         once = true;
      }
   }*/
}
void retro_init(void)
{
   /* set up some logging */
   init_log(environ_cb);

   char *dir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
      sprintf(retro_save_directory, "%s%c", dir, path_default_slash_c());

   printf("%s\n",retro_save_directory);

   check_variables();

   if(RETRO_PIX_BYTES == 4) {
      enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
      if(!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
         if(log_cb)
            log_cb(RETRO_LOG_ERROR, "[could not set RGB8888]\n");
         RETRO_PIX_BYTES = 2;
      }
   }

   if(RETRO_PIX_BYTES == 2) {
      enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
      if(!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt) && log_cb) {
         log_cb(RETRO_LOG_ERROR, "[could not set RGB565]\n");
      }
   }


   uint64_t serialization_quirks = RETRO_SERIALIZATION_QUIRK_SINGLE_SESSION;
   environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks);
}

void retro_reset(void)
{
   flashShutdown();
   system_sound_chipreset();
   mainemuinit();
}

void retro_deinit(void)
{
    flashShutdown();
}

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_variable vars[] = {
      { "ngp_language", "Language; japanese|english" },
      { NULL, NULL },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);

   environ_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
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

static unsigned get_race_input(void)
{
   unsigned i, res = 0;
   for (i = 0; i < sizeof(btn_map) / sizeof(map); ++i)
      res |= input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, btn_map[i].retro) ? btn_map[i].ngp : 0;
   return res;
}

static void race_input(void)
{
   ngpInputState = 0;
   input_poll_cb();
   ngpInputState = get_race_input();

}

static bool race_initialize_sound(void)
{
    system_sound_chipreset();
    return true;
}

static int file_exists(const char *path)
{
   FILE *dummy = fopen(path, "rb");

   if (!dummy)
      return 0;

   fclose(dummy);
   return 1;
}

static bool race_initialize_system(const char* gamepath)
{
   mainemuinit();

   if(!handleInputFile((char *)gamepath)){
      handle_error("ERROR handleInputFile");
      return false;
   }

   return true;
}

void retro_set_controller_port_device(unsigned, unsigned)
{
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = RACE_NAME;
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

   info->need_fullpath    = true;

   info->library_version  = RACE_VERSION GIT_VERSION;
   info->valid_extensions = RACE_EXTENSIONS;
   info->block_extract    = false;
}
void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = RACE_TIMING_FPS;
   info->timing.sample_rate    = RETRO_SAMPLE_RATE;
   info->geometry.base_width   = RACE_GEOMETRY_BASE_W;
   info->geometry.base_height  = RACE_GEOMETRY_BASE_H;
   info->geometry.max_width    = RACE_GEOMETRY_MAX_W;
   info->geometry.max_height   = RACE_GEOMETRY_MAX_H;
   info->geometry.aspect_ratio = RACE_GEOMETRY_ASPECT_RATIO;
}

void retro_run(void)
{
   race_input();

   tlcs_execute((6*1024*1024) / HOST_FPS);

   static int16_t sampleBuffer[2048];
   static int16_t stereoBuffer[2048];
   /* Get the number of samples in a frame */
   uint16_t samplesPerFrame = RETRO_SAMPLE_RATE
      / HOST_FPS;

   memset(sampleBuffer, 0, samplesPerFrame * sizeof(int16_t));

   sound_update(sampleBuffer, samplesPerFrame * sizeof(int16_t)); //Get sound data
   dac_update(sampleBuffer, samplesPerFrame * sizeof(int16_t));

   int16_t        *p        = stereoBuffer;
   for (int i = 0; i < samplesPerFrame; i++)
   {
      p[0] = sampleBuffer[i];
      p[1] = sampleBuffer[i];
      p += 2;
   }
   
   audio_batch_cb(stereoBuffer, samplesPerFrame);

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();
}

size_t retro_serialize_size(void)
{
   return state_get_size();
}

bool retro_serialize(void *data, size_t size)
{
   return state_store_mem(data);
}

bool retro_unserialize(const void *data, size_t size)
{
   return state_restore_mem(data);
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (!info)
      return false;

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Option" },

      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   screen = (ngp_screen*)calloc(1, sizeof(*screen));
   
   if (!screen)
      return false;
   
   screen->w = FB_WIDTH;
   screen->h = FB_HEIGHT;

   screen->pixels = calloc(1, FB_WIDTH * FB_HEIGHT * RETRO_PIX_BYTES);
      
   if (!screen->pixels)
   {
      free(screen);
      return false;
   }

   if (!race_initialize_system(info->path))
      return false;

   if (!race_initialize_sound())
      return false;

   check_variables();

   initialized = true;
   return true;
}

bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t)
{
   return false;
}

void retro_unload_game(void)
{
   initialized = false;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

unsigned retro_get_region()
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned type)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   return 0;
}