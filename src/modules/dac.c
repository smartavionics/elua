// eLua Module for generic DAC support

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "platform_conf.h"
#include "auxmods.h"
#include "elua_int.h"
//#include "dac.h"

//static elua_int_c_handler prev_handler;
//static elua_int_resnum index_resnum;
//static int index_tmr_id;
//static u16 index_count;
//static void index_handler( elua_int_resnum resnum );

//Lua: init(id, [bits, [left_aligned]])
static int dac_init( lua_State *L )
{
  unsigned id;
  unsigned bits = 8;
  unsigned left_aligned = 0;
  
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( dac, id );

  if ( lua_isnumber(L, 2) == 1 )
    bits = lua_tointeger(L, 2);

  if ( lua_isnumber(L, 3) == 1 )
    left_aligned = lua_toboolean(L, 3);

  platform_dac_init( id, bits, left_aligned );
  return 0;
}

// Lua: putsample( id, val )
static int dac_putsample( lua_State* L )
{
  unsigned id;
  u16 val = 0;
  
  id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( adc, id );

  if ( lua_isnumber(L, 2) == 1 )
    val = ( u16 )lua_tointeger(L, 2);

  platform_dac_putsample( id, val );

  return 0;
}

////Lua: setcounter(id, count)
//static int enc_set_counter( lua_State *L )
//{
  //unsigned id, count;

  //id = luaL_checkinteger( L, 1 );
  //MOD_CHECK_ID( timer, id );
  //count = luaL_checkinteger( L, 2 );

  //stm32_enc_set_counter( id, count );
  //return 0;
//}

//Lua: setidxtrig( id, resnum, tmr_id, count )
//static int enc_set_index_handler( lua_State *L )
//{
  //elua_int_id id;
  
  //id = ( elua_int_id )luaL_checkinteger( L, 1 );
  //if( id < ELUA_INT_FIRST_ID || id > INT_ELUA_LAST )
    //return luaL_error( L, "invalid interrupt ID" );
  //index_resnum = ( elua_int_resnum )luaL_checkinteger( L, 2 );
  //index_tmr_id = luaL_checkinteger( L, 3 );
  //MOD_CHECK_ID( timer, index_tmr_id );
  //index_count = ( u16 )luaL_checkinteger( L, 4 );

  //platform_cpu_set_interrupt( id, index_resnum, PLATFORM_CPU_ENABLE );
  //prev_handler = elua_int_set_c_handler( id, index_handler );
//}

//static void index_handler( elua_int_resnum resnum )
//{
  //if( prev_handler )
    //prev_handler;

  //if( resnum != index_resnum )
    //return;

  //stm32_enc_set_counter( index_tmr_id, index_count );
//}


#define MIN_OPT_LEVEL 2
#include "lrodefs.h"  

// Module function map
const LUA_REG_TYPE dac_map[] =
{ 
  { LSTRKEY( "init" ),  LFUNCVAL( dac_init ) },
  { LSTRKEY( "putsample" ),  LFUNCVAL( dac_putsample ) },
//  { LSTRKEY( "setcounter" ),  LFUNCVAL( enc_set_counter ) },
//  { LSTRKEY( "setidxtrig" ),  LFUNCVAL( enc_set_index_handler ) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_dac( lua_State *L )
{
  LREGISTER( L, AUXLIB_ENC, dac_map );
}  

