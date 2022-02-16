#ifndef RIXRUN_H
#define RIXRUN_H

#include "armdefs.h"

// Config
#define MEM_SIZE        32*1024*1024

// Globals/types:
extern uint8_t          *mem_base;
typedef uint32_t        addr_t;

////////////////////////////////////////////////////////////////////////////////

void    dump_state(ARMul_State *state);

#endif
