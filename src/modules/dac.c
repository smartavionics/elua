// eLua Module for generic DAC support

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "platform_conf.h"
#include "auxmods.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Lua: init( id, [bits, [left_aligned]] )
static int dac_init(lua_State *L) {
  unsigned id;
  unsigned bits = 8;
  unsigned left_aligned = 0;
  
  id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID(dac, id);

  if(lua_isnumber(L, 2) == 1)
    bits = lua_tointeger(L, 2);

  if (lua_isnumber(L, 3) == 1)
    left_aligned = lua_toboolean(L, 3);

  platform_dac_init(id, bits, left_aligned);

  return 0;
}

// Lua: putsample( id, val )
static int dac_putsample(lua_State  *L) {
  unsigned id;
  u16 val = 0;
  
  id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID(dac, id);

  if(lua_isnumber(L, 2) == 1)
    val = (u16)lua_tointeger(L, 2);

  platform_dac_putsample(id, val);

  return 0;
}

static elua_int_c_handler prev_timer_int_handler;
static const char *sample_buffer;
static unsigned sample_buffer_size;
static volatile unsigned num_samples;
static volatile unsigned samples_out;
static unsigned samples_in;
static unsigned bytes_per_sample;
static unsigned stride;
static unsigned bias;
static unsigned dac_ids[NUM_TIMER];

static void dac_timer_int_handler(elua_int_resnum resnum) {
  unsigned dac_id = dac_ids[resnum];
  if(dac_id) {
    --dac_id;
    if(samples_out != samples_in) {
      if(bytes_per_sample == 1) {
        platform_dac_putsample(dac_id, bias + sample_buffer[samples_out]);
      }
      else {
        platform_dac_putsample(dac_id, bias + *(int16_t *)(sample_buffer + samples_out));
      }
      samples_out += stride;
      if(samples_out >= sample_buffer_size)
        samples_out -= sample_buffer_size;
      --num_samples;
    }
  }
  else if(prev_timer_int_handler)
    prev_timer_int_handler(resnum);
}

// Lua: putsamples( id, timer_id, rate, samples, [bytes_per_sample, [offset, [num_samples, [bias]]]] )
static int dac_putsamples(lua_State *L) {
  unsigned dac_id, timer_id, rate;
  int result;
  const char *samples;
  size_t byte_count;
  unsigned offset = 0;
  bytes_per_sample = 1;
  bias = 0;

  dac_id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID( dac, dac_id );

  timer_id = luaL_checkinteger(L, 2);
  MOD_CHECK_TIMER( timer_id );
  MOD_CHECK_RES_ID( dac, dac_id, timer, timer_id );

  rate = luaL_checkinteger(L, 3);
  if(rate == 0)
    luaL_error(L, "rate must be > 0");

  samples = luaL_checklstring(L, 4, &byte_count);
  if(!samples)
    return 0;

  bytes_per_sample = luaL_optinteger(L, 5, 1);
  stride = bytes_per_sample;
  offset = luaL_optinteger(L, 6, 0);
  if(offset > byte_count)
    luaL_error(L, "offset must be less than length of samples string");
  num_samples = luaL_optinteger(L, 7, 0);
  if(num_samples == 0)
    num_samples = (byte_count - offset) / bytes_per_sample;
  else if(num_samples * bytes_per_sample + offset > byte_count)
    luaL_error(L, "num_samples too large");
  bias = luaL_optinteger(L, 8, 0);

  sample_buffer = samples + offset;
  sample_buffer_size = num_samples * bytes_per_sample;
  samples_in = sample_buffer_size;
  samples_out = 0;
  dac_ids[timer_id] = dac_id + 1;

  prev_timer_int_handler = elua_int_set_c_handler(INT_TMR_MATCH, dac_timer_int_handler);
  result = platform_timer_set_match_int(timer_id, 1000000 / rate, PLATFORM_TIMER_INT_CYCLIC);
  if(result != PLATFORM_TIMER_INT_OK)
    return luaL_error(L, "Failed to start DAC timer (%d)", result);

  while(num_samples != 0) {
    // relax
  }

  platform_timer_set_match_int(timer_id, 0, PLATFORM_TIMER_INT_CYCLIC);
  if(prev_timer_int_handler)
    elua_int_set_c_handler(INT_TMR_MATCH, prev_timer_int_handler);

  return 0;
}

// Lua: playwavfile( id, timer_id, wavfilename )
static int dac_playwavfile(lua_State *L) {
  unsigned dac_id, timer_id;
  int result;
  const char *wavfilename;

  dac_id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID( dac, dac_id );

  timer_id = luaL_checkinteger(L, 2);
  MOD_CHECK_TIMER( timer_id );
  MOD_CHECK_RES_ID( dac, dac_id, timer, timer_id );

  wavfilename = luaL_checklstring(L, 3, NULL);
  if(!wavfilename)
    return 0;

  FILE *wavfile = fopen(wavfilename, "rb");
  if(wavfile == NULL)
    return luaL_error(L, "Can't open %s for reading", wavfilename);

  uint8_t header[24];
  if(fread(header, 1, 12, wavfile) != 12) {
    fclose(wavfile);
    return luaL_error(L, "Failed to read header from %s", wavfilename);
  }

  if(memcmp(header+0, "RIFF", 4) ||
     memcmp(header+8, "WAVE", 4)) {
    fclose(wavfile);
    return luaL_error(L, "%s header format error 1", wavfilename);
  }

  if(fread(header, 1, 24, wavfile) != 24) {
    fclose(wavfile);
    return luaL_error(L, "Failed to read header from %s", wavfilename);
  }

  if(memcmp(header+0, "fmt ", 4)) {
    fclose(wavfile);
    return luaL_error(L, "%s header format error 2", wavfilename);
  }

  uint32_t chunk_size = header[4] + (header[5] << 8) + (header[6] << 16) + (header[7] << 24);
  uint32_t compression_code = header[8] + (header[9] << 8);
  uint32_t num_channels = header[10] + (header[11] << 8);
  uint32_t sample_rate = header[12] + (header[13] << 8) + (header[14] << 16) + (header[15] << 24);
  uint32_t block_align = header[20] + (header[21] << 8);

  if(chunk_size - 16)
    fread(header, 1, chunk_size - 16, wavfile);

  bytes_per_sample = block_align / num_channels;
  stride = block_align;

  if(compression_code != 1 || bytes_per_sample > 2) {
    // punt if data isn't 8 or 16 bit PCM
    fclose(wavfile);
    return luaL_error(L, "%s does not contain 8 or 16 bit PCM data", wavfilename);
  }

  if(fread(header, 1, 8, wavfile) != 8) {
    fclose(wavfile);
    return luaL_error(L, "Failed to read header from %s", wavfilename);
  }

  if(memcmp(header, "data", 4)) {
    fclose(wavfile);
    return luaL_error(L, "%s data format", wavfilename);
  }

  platform_dac_init(dac_id, bytes_per_sample == 1 ? 8 : 12, 1);

  uint32_t data_len = header[4] + (header[5] << 8) + (header[6] << 16) + (header[7] << 24);

  num_samples = data_len / (bytes_per_sample * num_channels);
  
  bias = 0x8000;

  static char buf[512];
  sample_buffer = buf;
  sample_buffer_size = sizeof(buf);
  samples_in = 0;
  samples_out = 0;
  dac_ids[timer_id] = dac_id + 1;

  samples_in = fread(buf, 1, sample_buffer_size - (bytes_per_sample * num_channels), wavfile);

  prev_timer_int_handler = elua_int_set_c_handler(INT_TMR_MATCH, dac_timer_int_handler);
  result = platform_timer_set_match_int(timer_id, 1000000 / sample_rate, PLATFORM_TIMER_INT_CYCLIC);
  if(result != PLATFORM_TIMER_INT_OK) {
    fclose(wavfile);
    return luaL_error(L, "Failed to start DAC timer (%d)", result);
  }

  while(num_samples != 0) {
    int space = samples_out - samples_in;
    if(space < 0)
      space += sample_buffer_size;
    if(space > 1) {
      buf[samples_in++] = feof(wavfile) ? 0 : getc(wavfile);
      if(samples_in >= sample_buffer_size)
        samples_in -= sample_buffer_size;
    }
  }

  platform_timer_set_match_int(timer_id, 0, PLATFORM_TIMER_INT_CYCLIC);
  if(prev_timer_int_handler)
    elua_int_set_c_handler(INT_TMR_MATCH, prev_timer_int_handler);

  fclose(wavfile);

  return 0;
}

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"  

// Module function map
const LUA_REG_TYPE dac_map[] = { 
  { LSTRKEY("init"),  LFUNCVAL(dac_init) },
  { LSTRKEY("putsample"),  LFUNCVAL(dac_putsample) },
  { LSTRKEY("putsamples"),  LFUNCVAL(dac_putsamples) },
  { LSTRKEY("playwavfile"),  LFUNCVAL(dac_playwavfile) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_dac(lua_State *L) {
  LREGISTER(L, AUXLIB_ENC, dac_map);
}  

