/*  armvirt.c -- ARMulator virtual memory interace:  ARM6 Instruction Emulator.
    Copyright (C) 1994 Advanced RISC Machines Ltd.
 
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. */

/* This file contains a complete ARMulator memory model, modelling a
"virtual memory" system. A much simpler model can be found in armfast.c,
and that model goes faster too, but has a fixed amount of memory. This
model's memory has 64K pages, allocated on demand from a 64K entry page
table. The routines PutWord and GetWord implement this. Pages are never
freed as they might be needed again. A single area of memory may be
defined to generate aborts. */

#include "armopts.h"
#include "armdefs.h"
#include "ansidecl.h"

#ifdef VALIDATE			/* for running the validate suite */
#define TUBE 48 * 1024 * 1024	/* write a char on the screen */
#define ABORTS 1
#endif

// #define ABORTS

#ifdef ABORTS			/* the memory system will abort */
/* For the old test suite Abort between 32 Kbytes and 32 Mbytes
   For the new test suite Abort between 8 Mbytes and 26 Mbytes */
/* #define LOWABORT 32 * 1024
#define HIGHABORT 32 * 1024 * 1024 */
#define LOWABORT 8 * 1024 * 1024
#define HIGHABORT 26 * 1024 * 1024

#endif

/* ME: removed memory init, "virtual memory" table lookup stuff,
 * exported GetWord/PutWord to my code.
 */
extern ARMword  GetWord(ARMul_State *state, ARMword address);
extern void     PutWord(ARMul_State *state, ARMword address, ARMword data);


/***************************************************************************\
*                   ReLoad Instruction                                     *
\***************************************************************************/

ARMword
ARMul_ReLoadInstr (ARMul_State * state, ARMword address, ARMword isize)
{
#ifdef ABORTS
  if (address >= LOWABORT && address < HIGHABORT)
    {
      ARMul_PREFETCHABORT (address);
      return ARMul_ABORTWORD;
    }
  else
    {
      ARMul_CLEARABORT;
    }
#endif

  if ((isize == 2) && (address & 0x2))
    {
      /* We return the next two halfwords: */
      ARMword lo = GetWord (state, address);
      ARMword hi = GetWord (state, address + 4);

      if (state->bigendSig == HIGH)
	return (lo << 16) | (hi >> 16);
      else
	return ((hi & 0xFFFF) << 16) | (lo >> 16);
    }

  return GetWord (state, address);
}

/***************************************************************************\
*                   Load Instruction, Sequential Cycle                      *
\***************************************************************************/

ARMword ARMul_LoadInstrS (ARMul_State * state, ARMword address, ARMword isize)
{
  state->NumScycles++;

#ifdef HOURGLASS
  if ((state->NumScycles & HOURGLASS_RATE) == 0)
    {
      HOURGLASS;
    }
#endif

  return ARMul_ReLoadInstr (state, address, isize);
}

/***************************************************************************\
*                 Load Instruction, Non Sequential Cycle                    *
\***************************************************************************/

ARMword ARMul_LoadInstrN (ARMul_State * state, ARMword address, ARMword isize)
{
  state->NumNcycles++;

  return ARMul_ReLoadInstr (state, address, isize);
}

/***************************************************************************\
*                      Read Word (but don't tell anyone!)                   *
\***************************************************************************/

ARMword ARMul_ReadWord (ARMul_State * state, ARMword address)
{
#ifdef ABORTS
  if (address >= LOWABORT && address < HIGHABORT)
    {
      ARMul_DATAABORT (address);
      return ARMul_ABORTWORD;
    }
  else
    {
      ARMul_CLEARABORT;
    }
#endif

  return GetWord (state, address);
}

/***************************************************************************\
*                        Load Word, Sequential Cycle                        *
\***************************************************************************/

ARMword ARMul_LoadWordS (ARMul_State * state, ARMword address)
{
  state->NumScycles++;

  return ARMul_ReadWord (state, address);
}

/***************************************************************************\
*                      Load Word, Non Sequential Cycle                      *
\***************************************************************************/

ARMword ARMul_LoadWordN (ARMul_State * state, ARMword address)
{
  state->NumNcycles++;

  return ARMul_ReadWord (state, address);
}

/***************************************************************************\
*                     Load Halfword, (Non Sequential Cycle)                 *
\***************************************************************************/

ARMword ARMul_LoadHalfWord (ARMul_State * state, ARMword address)
{
  ARMword temp, offset;

  state->NumNcycles++;

  temp = ARMul_ReadWord (state, address);
  offset = (((ARMword) state->bigendSig * 2) ^ (address & 2)) << 3;	/* bit offset into the word */

  return (temp >> offset) & 0xffff;
}

/***************************************************************************\
*                      Read Byte (but don't tell anyone!)                   *
\***************************************************************************/

ARMword ARMul_ReadByte (ARMul_State * state, ARMword address)
{
  ARMword temp, offset;

  temp = ARMul_ReadWord (state, address);
  offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;	/* bit offset into the word */

  return (temp >> offset & 0xffL);
}

/***************************************************************************\
*                     Load Byte, (Non Sequential Cycle)                     *
\***************************************************************************/

ARMword ARMul_LoadByte (ARMul_State * state, ARMword address)
{
  state->NumNcycles++;

  return ARMul_ReadByte (state, address);
}

/***************************************************************************\
*                     Write Word (but don't tell anyone!)                   *
\***************************************************************************/

void
ARMul_WriteWord (ARMul_State * state, ARMword address, ARMword data)
{
#ifdef ABORTS
  if (address >= LOWABORT && address < HIGHABORT)
    {
      ARMul_DATAABORT (address);
      return;
    }
  else
    {
      ARMul_CLEARABORT;
    }
#endif

  PutWord (state, address, data);
}

/***************************************************************************\
*                       Store Word, Sequential Cycle                        *
\***************************************************************************/

void
ARMul_StoreWordS (ARMul_State * state, ARMword address, ARMword data)
{
  state->NumScycles++;

  ARMul_WriteWord (state, address, data);
}

/***************************************************************************\
*                       Store Word, Non Sequential Cycle                        *
\***************************************************************************/

void
ARMul_StoreWordN (ARMul_State * state, ARMword address, ARMword data)
{
  state->NumNcycles++;

  ARMul_WriteWord (state, address, data);
}

/***************************************************************************\
*                    Store HalfWord, (Non Sequential Cycle)                 *
\***************************************************************************/

void
ARMul_StoreHalfWord (ARMul_State * state, ARMword address, ARMword data)
{
  ARMword temp, offset;

  state->NumNcycles++;

#ifdef VALIDATE
  if (address == TUBE)
    {
      if (data == 4)
	state->Emulate = FALSE;
      else
	(void) putc ((char) data, stderr);	/* Write Char */
      return;
    }
#endif

  temp = ARMul_ReadWord (state, address);
  offset = (((ARMword) state->bigendSig * 2) ^ (address & 2)) << 3;	/* bit offset into the word */

  PutWord (state, address,
	   (temp & ~(0xffffL << offset)) | ((data & 0xffffL) << offset));
}

/***************************************************************************\
*                     Write Byte (but don't tell anyone!)                   *
\***************************************************************************/

void
ARMul_WriteByte (ARMul_State * state, ARMword address, ARMword data)
{
  ARMword temp, offset;

  temp = ARMul_ReadWord (state, address);
  offset = (((ARMword) state->bigendSig * 3) ^ (address & 3)) << 3;	/* bit offset into the word */

  PutWord (state, address,
	   (temp & ~(0xffL << offset)) | ((data & 0xffL) << offset));
}

/***************************************************************************\
*                    Store Byte, (Non Sequential Cycle)                     *
\***************************************************************************/

void
ARMul_StoreByte (ARMul_State * state, ARMword address, ARMword data)
{
  state->NumNcycles++;

#ifdef VALIDATE
  if (address == TUBE)
    {
      if (data == 4)
	state->Emulate = FALSE;
      else
	(void) putc ((char) data, stderr);	/* Write Char */
      return;
    }
#endif

  ARMul_WriteByte (state, address, data);
}

/***************************************************************************\
*                   Swap Word, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapWord (ARMul_State * state, ARMword address, ARMword data)
{
  ARMword temp;

  state->NumNcycles++;

  temp = ARMul_ReadWord (state, address);

  state->NumNcycles++;

  PutWord (state, address, data);

  return temp;
}

/***************************************************************************\
*                   Swap Byte, (Two Non Sequential Cycles)                  *
\***************************************************************************/

ARMword ARMul_SwapByte (ARMul_State * state, ARMword address, ARMword data)
{
  ARMword temp;

  temp = ARMul_LoadByte (state, address);
  ARMul_StoreByte (state, address, data);

  return temp;
}

/***************************************************************************\
*                             Count I Cycles                                *
\***************************************************************************/

void
ARMul_Icycles (ARMul_State * state, unsigned number, ARMword address ATTRIBUTE_UNUSED)
{
  state->NumIcycles += number;
  ARMul_CLEARABORT;
}

/***************************************************************************\
*                             Count C Cycles                                *
\***************************************************************************/

void
ARMul_Ccycles (ARMul_State * state, unsigned number, ARMword address ATTRIBUTE_UNUSED)
{
  state->NumCcycles += number;
  ARMul_CLEARABORT;
}
