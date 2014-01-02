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
#include <stdlib.h>

// Lua: init( id, [bits_per_sample, [options]] )
static int dac_init(lua_State *L) {
  
  unsigned id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID(dac, id);

  unsigned bits_per_sample = luaL_optinteger(L, 2, 8);

  unsigned options = luaL_optinteger(L, 3, 0);

  int result = platform_dac_init(id, bits_per_sample, options);
  if(result != DAC_INIT_OK)
    luaL_error(L, "DAC initialisation failed (%d)", result);

  return 0;
}

// Lua: putsample( id, [val] )
static int dac_putsample(lua_State  *L) {
  
  unsigned id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID(dac, id);

  u16 val = luaL_optinteger(L, 2, 0);

  platform_dac_put_sample(1 << id, &val);

  return 0;
}

static struct {
  elua_int_c_handler prev_timer_int_handler;
  char *sample_buffer;            // circular buffer holding sample data
  unsigned sample_buffer_size;    // size of buffer in bytes
  volatile unsigned num_samples;  // total number of samples to output
  volatile unsigned next_out;     // offset of next byte in sample buffer to be output
  unsigned next_in;               // offset of next byte in sample buffer to be filled
  unsigned bytes_per_sample;      // number of bytes for each sample (per channel)
  unsigned stride;                // amount next_out is incremented by when a sample is consumed
  unsigned bias;                  // add this value to sample data before sending to the DAC
  unsigned dac_id;                // the id of the DAC
  unsigned timer_id;              // the id of the timer
} dac_state;

static void dac_timer_int_handler(elua_int_resnum resnum) {
  if(resnum == dac_state.timer_id) {
    // the interrupt is for our timer
    int available = dac_state.next_in - dac_state.next_out;
    if(available < 0)
      available += dac_state.sample_buffer_size;
    if(available >= dac_state.stride) {
      u16 sample;
      if(dac_state.bytes_per_sample == 1) {
        sample = dac_state.bias + dac_state.sample_buffer[dac_state.next_out];
      }
      else {
        sample = dac_state.bias + *(int16_t *)(dac_state.sample_buffer + dac_state.next_out);
      }
      platform_dac_put_sample(1 << dac_state.dac_id, &sample);
      dac_state.next_out += dac_state.stride;
      if(dac_state.next_out >= dac_state.sample_buffer_size)
        dac_state.next_out -= dac_state.sample_buffer_size;
      --dac_state.num_samples;
    }
  }
  else {
    // the interrupt is for another timer
    if(dac_state.prev_timer_int_handler)
      dac_state.prev_timer_int_handler(resnum);
  }
}

// Lua: putsamples( id, samples, rate, [timer_id, [bytes_per_sample, [bias, [offset, [num_samples]]]]] )
static int dac_putsamples(lua_State *L) {

  dac_state.dac_id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID(dac, dac_state.dac_id);

  size_t byte_count;
  const char *samples = luaL_checklstring(L, 2, &byte_count);
  if(!samples)
    return 0;

  unsigned rate = luaL_checkinteger(L, 3);
  if(rate == 0)
    luaL_error(L, "rate must be > 0");

  unsigned default_timer_id = 0;
  while(default_timer_id < NUM_TIMER && !platform_dac_check_timer_id(dac_state.dac_id, default_timer_id))
    ++default_timer_id;
  dac_state.timer_id = luaL_optinteger(L, 4, default_timer_id);
  MOD_CHECK_TIMER(dac_state.timer_id);
  MOD_CHECK_RES_ID(dac, dac_state.dac_id, timer, dac_state.timer_id);

  dac_state.bytes_per_sample = luaL_optinteger(L, 5, 1);
  dac_state.stride = dac_state.bytes_per_sample;
  dac_state.bias = luaL_optinteger(L, 6, 0);
  unsigned offset = luaL_optinteger(L, 7, 0);
  if(offset > byte_count)
    luaL_error(L, "offset must be less than length of samples string");
  dac_state.num_samples = luaL_optinteger(L, 8, (byte_count - offset) / dac_state.bytes_per_sample);
  if(dac_state.num_samples * dac_state.bytes_per_sample + offset > byte_count)
    luaL_error(L, "num_samples too large");

  dac_state.sample_buffer = (char *)samples + offset;
  dac_state.sample_buffer_size = dac_state.num_samples * dac_state.bytes_per_sample;
  dac_state.next_in = dac_state.sample_buffer_size;
  dac_state.next_out = 0;

  // install our timer interrupt handler
  dac_state.prev_timer_int_handler = elua_int_set_c_handler(INT_TMR_MATCH, dac_timer_int_handler);
  // start cyclic timer interrupts
  int result = platform_timer_set_match_int(dac_state.timer_id, 1000000 / rate, PLATFORM_TIMER_INT_CYCLIC);
  if(result != PLATFORM_TIMER_INT_OK)
    return luaL_error(L, "Failed to start DAC timer (%d)", result);

  while(dac_state.num_samples != 0) {
    // twiddle thumbs
  }

  // stop timer interrupts
  platform_timer_set_match_int(dac_state.timer_id, 0, PLATFORM_TIMER_INT_CYCLIC);
  // possibly restore original timer interrupt handler
  if(dac_state.prev_timer_int_handler)
    elua_int_set_c_handler(INT_TMR_MATCH, dac_state.prev_timer_int_handler);

  return 0;
}

// Lua: playwavfile( id, wavfilename, [timer_id, [sample_buf_size]] )
static int dac_playwavfile(lua_State *L) {

  dac_state.dac_id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID(dac, dac_state.dac_id);

  const char *wavfilename = luaL_checklstring(L, 2, NULL);
  if(!wavfilename)
    return 0;

  unsigned default_timer_id = 0;
  while(default_timer_id < NUM_TIMER && !platform_dac_check_timer_id(dac_state.dac_id, default_timer_id))
    ++default_timer_id;
  dac_state.timer_id = luaL_optinteger(L, 3, default_timer_id);
  MOD_CHECK_TIMER(dac_state.timer_id);
  MOD_CHECK_RES_ID(dac, dac_state.dac_id, timer, dac_state.timer_id);

  dac_state.sample_buffer_size = luaL_optinteger(L, 4, 32);

  FILE *wavfile = fopen(wavfilename, "rb");
  if(wavfile == NULL)
    return luaL_error(L, "Can't open %s for reading", wavfilename);

  uint8_t header[24];
  if(fread(header, 1, 12, wavfile) != 12) {
    fclose(wavfile);
    return luaL_error(L, "Failed to read from %s", wavfilename);
  }

  if(memcmp(header+0, "RIFF", 4) ||
     memcmp(header+8, "WAVE", 4)) {
    fclose(wavfile);
    return luaL_error(L, "%s is not a WAV file", wavfilename);
  }

  if(fread(header, 1, 24, wavfile) != 24) {
    fclose(wavfile);
    return luaL_error(L, "Failed to read from %s", wavfilename);
  }

  if(memcmp(header+0, "fmt ", 4)) {
    fclose(wavfile);
    return luaL_error(L, "%s header does not contain format chunk", wavfilename);
  }

  uint32_t chunk_size = header[4] + (header[5] << 8) + (header[6] << 16) + (header[7] << 24);
  uint32_t compression_code = header[8] + (header[9] << 8);
  uint32_t num_channels = header[10] + (header[11] << 8);
  uint32_t sample_rate = header[12] + (header[13] << 8) + (header[14] << 16) + (header[15] << 24);
  uint32_t block_align = header[20] + (header[21] << 8);

  if(chunk_size > 16)
    fread(header, 1, chunk_size - 16, wavfile);

  dac_state.stride = block_align;
  dac_state.bytes_per_sample = block_align / num_channels;

  if(compression_code != 1) {
    // punt if data isn't uncompressed PCM
    fclose(wavfile);
    return luaL_error(L, "%s does not contain uncompressed PCM data", wavfilename);
  }

  if(dac_state.bytes_per_sample > 2) {
    // punt if data isn't 8 or 16 bit
    fclose(wavfile);
    return luaL_error(L, "%s does not contain 8 or 16 bit PCM data", wavfilename);
  }

  if(fread(header, 1, 8, wavfile) != 8) {
    fclose(wavfile);
    return luaL_error(L, "Failed to read from %s", wavfilename);
  }

  if(memcmp(header, "data", 4)) {
    fclose(wavfile);
    return luaL_error(L, "%s does not contain a data chunk", wavfilename);
  }

  int result = platform_dac_init(dac_state.dac_id, dac_state.bytes_per_sample * 8, 0);
  if(result != DAC_INIT_OK) {
    fclose(wavfile);
    return luaL_error(L, "DAC initialisation failed (%d)", result);
  }

  uint32_t data_len = header[4] + (header[5] << 8) + (header[6] << 16) + (header[7] << 24);

  dac_state.num_samples = data_len / dac_state.stride;
  
  dac_state.bias = dac_state.bytes_per_sample == 1 ? 0xffffff80 : 0xffff8000;

  dac_state.sample_buffer = malloc(dac_state.sample_buffer_size);
  if(!dac_state.sample_buffer) {
    fclose(wavfile);
    return luaL_error(L, "Failed to allocate %d byte buffer", dac_state.sample_buffer_size);
  }
  // (nearly) fill the buffer with sample data
  dac_state.next_in = fread(dac_state.sample_buffer, 1, dac_state.sample_buffer_size - dac_state.stride, wavfile);
  dac_state.next_out = 0;

  // install our timer interrupt handler
  dac_state.prev_timer_int_handler = elua_int_set_c_handler(INT_TMR_MATCH, dac_timer_int_handler);
  // start cyclic timer interrupts
  result = platform_timer_set_match_int(dac_state.timer_id, 1000000 / sample_rate, PLATFORM_TIMER_INT_CYCLIC);
  if(result != PLATFORM_TIMER_INT_OK) {
    free(dac_state.sample_buffer);
    fclose(wavfile);
    return luaL_error(L, "Failed to start DAC timer (%d)", result);
  }

  while(dac_state.num_samples != 0) {
    // as buffer drains, refill it a byte at a time
    int space = dac_state.next_out - dac_state.next_in;
    if(space < 0)
      space += dac_state.sample_buffer_size;
    // don't completely fill the buffer, leave 1 byte gap
    if(space > 1) {
      // if we run off the end of the file, just fill with zeroes
      dac_state.sample_buffer[dac_state.next_in++] = feof(wavfile) ? 0 : getc(wavfile);
      if(dac_state.next_in >= dac_state.sample_buffer_size)
        dac_state.next_in -= dac_state.sample_buffer_size;
    }
  }

  // stop timer interrupts
  platform_timer_set_match_int(dac_state.timer_id, 0, PLATFORM_TIMER_INT_CYCLIC);
  // possibly restore original timer interrupt handler
  if(dac_state.prev_timer_int_handler)
    elua_int_set_c_handler(INT_TMR_MATCH, dac_state.prev_timer_int_handler);

  free(dac_state.sample_buffer);
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

