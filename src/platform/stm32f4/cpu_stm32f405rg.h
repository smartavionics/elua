// CPU definition file for STM32F405RG

#ifndef __CPU_STM32F405RG_H__
#define __CPU_STM32F405RG_H__

// Start from the definition of STM32F407VG, modify what's neeeded

#include "cpu_stm32f407vg.h"

#undef NUM_PIO
#define NUM_PIO               4
#undef NUM_ADC
#define NUM_ADC               16

#endif // #ifndef __CPU_STM32F405RG_H__

