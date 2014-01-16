// eLua Module for simple DAC support

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "lrotable.h"
#include "platform_conf.h"
#include "auxmods.h"

#include "stm32f4xx_conf.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define NUM_DAC 2

// *****************************************************************************
// The DAC functions

// DAC common functions
int platform_dac_exists(unsigned id) {
  return id < NUM_DAC;
}

// DAC platform specific functions

#define DAC_INIT_OK                   0
#define DAC_INIT_BAD_ID              -1
#define DAC_INIT_BAD_BITS_PER_SAMPLE -2
#define DAC_INIT_BAD_OPTION          -3

int  platform_dac_init( unsigned id, unsigned bits_per_sample, unsigned options );
void platform_dac_put_sample( unsigned channel_mask, unsigned *data );
int  platform_dac_check_timer_id( unsigned id, unsigned timer_id );

// ****************************************************************************
// platform DAC functions

static uint32_t alignments[NUM_DAC] = { DAC_Align_8b_R, DAC_Align_8b_R };

int platform_dac_init(unsigned id, unsigned bits_per_sample, unsigned options) {
  unsigned dac_channel = 0;
  GPIO_InitTypeDef GPIO_init_struct;
  GPIO_init_struct.GPIO_Mode = GPIO_Mode_AN;
  GPIO_init_struct.GPIO_PuPd = GPIO_PuPd_NOPULL;
  switch(id) {
  case 0:
    dac_channel = DAC_Channel_1;
    GPIO_init_struct.GPIO_Pin = GPIO_Pin_4;
    break;
  case 1:
    dac_channel = DAC_Channel_2;
    GPIO_init_struct.GPIO_Pin = GPIO_Pin_5;
    break;
  default:
    return DAC_INIT_BAD_ID;
  }
  GPIO_Init(GPIOA, &GPIO_init_struct);
  switch(bits_per_sample) {
  case 8:
    alignments[id] = DAC_Align_8b_R;
    break;
  case 12:
    alignments[id] = DAC_Align_12b_R;
    break;
  case 16:
    alignments[id] = DAC_Align_12b_L;
    break;
  default:
    return DAC_INIT_BAD_BITS_PER_SAMPLE;
  }
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
  DAC_DeInit();
  DAC_InitTypeDef dac_init_struct;
  DAC_StructInit(&dac_init_struct);
  DAC_Init(dac_channel, &dac_init_struct);
  DAC_Cmd(dac_channel, ENABLE);
  return 0;
}

void platform_dac_put_sample(unsigned channel_mask, unsigned *data) {
  switch(channel_mask & 3) {
  case 1:
    DAC_SetChannel1Data(alignments[0], data[0]);
    break;
  case 2:
    DAC_SetChannel2Data(alignments[1], data[0]);
    break;
  case 3:
    DAC_SetDualChannelData(alignments[0], data[1], data[0]);
    break;
  default:
    break;
  }
}

int platform_dac_check_timer_id(unsigned id, unsigned timer_id) {
  return timer_id >= 1 && timer_id <= 4;
}

// ****************************************************************************
// generic DAC functions

static unsigned bytes_per_sample[NUM_DAC];

// Lua: setup( dac_id, [bits_per_sample] )
static int dac_setup( lua_State *L )
{
  unsigned id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( dac, id );

  unsigned bits_per_sample = luaL_optinteger( L, 2, 8 );

  int result = platform_dac_init( id, bits_per_sample, 0 );
  if ( result != DAC_INIT_OK )
    return luaL_error( L, "DAC initialisation failed (%d)", result );

  bytes_per_sample[id] = (bits_per_sample + 7) / 8;

  return 0;
}

// Lua: putsample( dac_id, val, ... )
static int dac_putsample( lua_State  *L )
{
  unsigned dac_id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( dac, dac_id );

  unsigned channel_mask = 1 << dac_id;
  unsigned vals[NUM_DAC] = { luaL_checkinteger( L, 2 ) };

  unsigned argn;
  for ( argn = 3; argn <= lua_gettop( L ) && argn - 2 < NUM_DAC; ++argn )
  {
    channel_mask |= 1 << ++dac_id;
    vals[argn - 2] = luaL_checkinteger( L, argn );
  }

  platform_dac_put_sample( channel_mask, vals );

  return 0;
}

volatile static struct
{
  elua_int_c_handler prev_timer_int_handler;
  char *sample_buffer;            // circular buffer holding sample data
  unsigned sample_buffer_size;    // size of buffer in bytes
  unsigned next_out;              // offset of next byte in sample buffer to be output
  unsigned next_in;               // offset of next byte in sample buffer to be filled
  unsigned channels;              // number of channels
  unsigned stride;                // amount next_out is incremented by when a sample is consumed
  unsigned bias;                  // add this value to sample data before sending to the DAC
  unsigned dac_id;                // the id of the DAC
  unsigned timer_id;              // the id of the timer
  unsigned num_output;            // number of samples output
  unsigned num_underflows;        // number of times the buffer underflowed
} dac_state;

static void dac_timer_int_handler( elua_int_resnum resnum )
{
  if ( resnum == dac_state.timer_id )
  {
    // the interrupt is for our timer
    int available = dac_state.next_in - dac_state.next_out;
    if ( available < 0 )
      available += dac_state.sample_buffer_size;
    if ( available >= dac_state.stride )
    {
      unsigned dac_vals[NUM_DAC];
      unsigned dac_mask = 0;
      int i;
      for ( i = 0; i < dac_state.channels; ++i )
      {
        dac_mask |= 1 << (dac_state.dac_id + i);
        if( bytes_per_sample[dac_state.dac_id + i] == 1 )
        {
          dac_vals[i] = dac_state.bias + dac_state.sample_buffer[dac_state.next_out];
          ++dac_state.next_out;
        }
        else
        {
          dac_vals[i] = dac_state.bias + *(s16 *)(dac_state.sample_buffer + dac_state.next_out);
          dac_state.next_out += 2;
        }
        if ( dac_state.next_out >= dac_state.sample_buffer_size )
          dac_state.next_out -= dac_state.sample_buffer_size;
      }
      platform_dac_put_sample( dac_mask, dac_vals );
      ++dac_state.num_output;
    }
    else
      ++dac_state.num_underflows;
  }
  else
  {
    // the interrupt is for another timer
    if ( dac_state.prev_timer_int_handler )
      dac_state.prev_timer_int_handler(resnum);
  }
}

// Lua: num_samples_output, num_underflows = putsamples( dac_id, data_source, rate, [bits_per_sample, [channels, [bias, [timer_id]]]] )
static int dac_putsamples( lua_State *L )
{
  dac_state.dac_id = luaL_checkinteger( L, 1 );
  MOD_CHECK_ID( dac, dac_state.dac_id );

  unsigned data_source_type = lua_type( L, 2 );
  if ( data_source_type != LUA_TSTRING && data_source_type != LUA_TTABLE && data_source_type != LUA_TFUNCTION )
    return luaL_error( L, "data_source must either be an array of integers, a string or a function that returns a string" );

  unsigned rate = luaL_checkinteger( L, 3 );
  if( rate == 0 )
    return luaL_error( L, "rate must be > 0" );

  unsigned bits_per_sample = luaL_optinteger( L, 4, 8 );

  dac_state.channels = luaL_optinteger( L, 5, 1 );
  if ( dac_state.channels < 1 || (dac_state.dac_id + dac_state.channels > NUM_DAC) )
    return luaL_error( L, "channels must be between 1 and %d", NUM_DAC - dac_state.dac_id );
  dac_state.bias = luaL_optinteger( L, 6, 0 );

  unsigned default_timer_id = 0;
  while ( default_timer_id < NUM_TIMER && !platform_dac_check_timer_id( dac_state.dac_id, default_timer_id ) )
    ++default_timer_id;
  dac_state.timer_id = luaL_optinteger( L, 7, default_timer_id );
  MOD_CHECK_TIMER( dac_state.timer_id );
  MOD_CHECK_RES_ID( dac, dac_state.dac_id, timer, dac_state.timer_id );

  {
    int i;
    for ( i = 0; i < dac_state.channels; ++i )
    {
      int result = platform_dac_init( dac_state.dac_id + i, bits_per_sample, 0 );
      if ( result != DAC_INIT_OK )
        return luaL_error( L, "DAC initialisation failed (%d)", result );
      bytes_per_sample[dac_state.dac_id] = (bits_per_sample + 7) / 8;
    }
  }

  dac_state.stride = bytes_per_sample[dac_state.dac_id] * dac_state.channels;

  dac_state.next_in = 0;
  dac_state.next_out = 0;
  dac_state.num_output = 0;

  // install our timer interrupt handler
  dac_state.prev_timer_int_handler = elua_int_set_c_handler( INT_TMR_MATCH, dac_timer_int_handler );
  // start cyclic timer interrupts
  int result = platform_timer_set_match_int( dac_state.timer_id, 1000000 / rate, PLATFORM_TIMER_INT_CYCLIC );
  if( result != PLATFORM_TIMER_INT_OK )
    return luaL_error( L, "Failed to start DAC timer (%d)", result );

  if( data_source_type == LUA_TTABLE )
  {
    // transfer the values in the table into a small circular buffer
    int num_vals_in_table = lua_objlen( L, 2 );
    char small_buffer[16];
    dac_state.sample_buffer = small_buffer;
    dac_state.sample_buffer_size = sizeof( small_buffer );
    // copy values from table into circular buffer
    int n = 1;
    while ( n <= num_vals_in_table )
    {
      int space = dac_state.next_out - dac_state.next_in;
      if ( space <= 0 )
        space += dac_state.sample_buffer_size;
      if ( space > dac_state.stride )
      {
        if ( n == 1 )
          dac_state.num_underflows = 0;
        lua_rawgeti( L, 2, n++ );
        unsigned val = luaL_checkinteger( L, -1 );
        lua_pop( L, 1 );
        int i;
        for ( i = 0; i < dac_state.stride; ++i )
        {
          // FIXME - cope with big endian format?
          dac_state.sample_buffer[dac_state.next_in] = val;
          val >>= 8;
          if ( ++dac_state.next_in >= dac_state.sample_buffer_size )
            dac_state.next_in -= dac_state.sample_buffer_size;
        }
      }
    }
  }
  else if ( data_source_type == LUA_TFUNCTION )
  {
    // the function is expected to return a string of data each time it is called
    // until there is no more data and then the function should return nil or an empty string
    dac_state.sample_buffer = 0;
    for (;;)
    {
      // call function to get next chunk of data
      lua_pushvalue( L, 2 );
      lua_call( L, 0, 1 );
      if ( lua_isnoneornil( L, -1 ) )
      {
        // function returned nil
        lua_pop( L, 1 );
        break;
      }
      size_t byte_count = 0;
      char *chunk = (char *)luaL_checklstring( L, -1, &byte_count );
      if ( byte_count == 0 )
      {
        // function returned empty string
        lua_pop( L, 1 );
        break;
      }
      if ( !dac_state.sample_buffer )
      {
        // handle the first chunk of data by allocating a buffer of the same
        // size and copying all the data into the buffer
        dac_state.sample_buffer_size = byte_count;
        dac_state.sample_buffer = malloc( byte_count );
        if ( !dac_state.sample_buffer )
        {
          // discard function result
          lua_pop( L, 1 );
          luaL_where( L, 1 );
          fprintf( stderr, "%sFailed to allocate %d byte sample buffer\n", luaL_checklstring( L, -1, 0 ), byte_count );
          lua_pop( L, 1 );
          break;
        }
        memcpy( dac_state.sample_buffer, chunk, byte_count );
        // set next_in to the last byte in buffer so that the
        // interrupt handler starts consuming data
        dac_state.next_in = byte_count - 1;
        // reset underflow counter
        dac_state.num_underflows = 0;
        while ( dac_state.next_out == 0 )
        {
          // wait for some data to be consumed
        }
        // now set next_in to zero again which is the correct
        // value as we completely filled the buffer
        dac_state.next_in = 0;
      }
      else
      {
        // top up circular buffer byte by byte
        int i;
        for ( i = 0; i < byte_count; )
        {
          int space = dac_state.next_out - dac_state.next_in;
          if ( space <= 0 )
            space += dac_state.sample_buffer_size;
          if ( space > 1 )
          {
            //if ( i == 0 )
            //  printf( "Buffer space now %d\n", space );
            dac_state.sample_buffer[dac_state.next_in] = chunk[i++];
            if ( ++dac_state.next_in >= dac_state.sample_buffer_size )
              dac_state.next_in -= dac_state.sample_buffer_size;
          }
        }
      }
      // discard function result
      lua_pop( L, 1 );
    }

    while ( dac_state.next_in != dac_state.next_out )
    {
    // wait for buffer to drain
    }

    free(dac_state.sample_buffer);
  }
  else
  {
    // LUA_TSTRING
    // trivial case, just use the string directly
    size_t byte_count = 0;
    dac_state.sample_buffer = (char *)luaL_checklstring( L, 2, &byte_count );
    dac_state.sample_buffer_size = byte_count + 1;
    dac_state.next_in = byte_count;
    dac_state.num_underflows = 0;
  }

  while ( dac_state.next_in != dac_state.next_out )
  {
    // wait for buffer to drain
  }

  // stop timer interrupts
  platform_timer_set_match_int( dac_state.timer_id, 0, PLATFORM_TIMER_INT_CYCLIC );
  // possibly restore original timer interrupt handler
  if ( dac_state.prev_timer_int_handler )
    elua_int_set_c_handler( INT_TMR_MATCH, dac_state.prev_timer_int_handler );

  lua_pushinteger( L, dac_state.num_output );
  lua_pushinteger( L, dac_state.num_underflows );

  return 2;
}

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"  

// Module function map
const LUA_REG_TYPE sa_dac_map[] = { 
  { LSTRKEY("setup"),  LFUNCVAL( dac_setup ) },
  { LSTRKEY("putsample"),  LFUNCVAL( dac_putsample ) },
  { LSTRKEY("putsamples"),  LFUNCVAL( dac_putsamples ) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_sa_dac( lua_State *L )
{
  LREGISTER( L, AUXLIB_ENC, sa_dac_map );
}  

