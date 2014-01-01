// eLua Module for generic DAC support

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "platform_conf.h"
#include "auxmods.h"

//Lua: init(id, [bits, [left_aligned]])
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

// Lua: putsamples( id, timer_id, rate, samples, [bytes_per_sample, [offset, [num_samples, [bias]]]] )
static int dac_putsamples(lua_State *L) {
  unsigned dac_id, timer_id, rate;
  int result;
  const char *samples;
  size_t byte_count;
  unsigned bytes_per_sample = 1;
  unsigned offset = 0;
  unsigned num_samples = 0;
  unsigned bias = 0;

  dac_id = luaL_checkinteger(L, 1);
  MOD_CHECK_ID( dac, dac_id );

  timer_id = luaL_checkinteger(L, 2);
  MOD_CHECK_TIMER( timer_id );

  rate = luaL_checkinteger(L, 3);
  if(rate == 0)
    luaL_error(L, "rate must be > 0");

  samples = luaL_checklstring(L, 4, &byte_count);
  if(!samples)
    return 0;

  bytes_per_sample = luaL_optinteger(L, 5, 1);
  offset = luaL_optinteger(L, 6, 0);
  if(offset > byte_count)
    luaL_error(L, "offset must be less than length of samples string");
  num_samples = luaL_optinteger(L, 7, 0);
  if(num_samples == 0)
    num_samples = (byte_count - offset) / bytes_per_sample;
  else if(num_samples * bytes_per_sample + offset > byte_count)
    luaL_error(L, "num_samples too large");
  bias = luaL_optinteger(L, 8, 0);

  result = platform_dac_putsamples(dac_id, timer_id, rate, samples + offset, bytes_per_sample, num_samples, bias);
  if(result)
    luaL_error(L, "Failed to put %d samples to DAC %d (%d)", num_samples, dac_id, result);

  return 0;
}

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"  

// Module function map
const LUA_REG_TYPE dac_map[] = { 
  { LSTRKEY("init"),  LFUNCVAL(dac_init) },
  { LSTRKEY("putsample"),  LFUNCVAL(dac_putsample) },
  { LSTRKEY("putsamples"),  LFUNCVAL(dac_putsamples) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_dac(lua_State *L) {
  LREGISTER(L, AUXLIB_ENC, dac_map);
}  

