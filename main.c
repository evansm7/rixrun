/* rixrun main
 *
 * Rixrun is a trivialish RISCiX userspace emulator, able to run
 * unsqueezed demand-paged RISCiX binaries.
 *
 * The intended scope is very small; just enough to run the development
 * tools on a Linux box!  It won't come close to running X applications
 * yet (but os.c and friends could be extended to support this by
 * suitably-motivated folks).
 *
 * Uses ARM's ARMulator source (harvested from GDB 5.0) to execute
 * instructions.
 *
 * Copyright (C) 2022 Matt Evans
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include "armdefs.h"
#include "armemu.h"
#include "ansidecl.h"
#include "rixrun.h"
#include "rix_os.h"
#include "zload.h"


/* The memory "strategy" is currently extremely dumb.
 * Because there's a max of 32MB in the user address space, I've
 * just used a huge flat array (in BSS) rather than individually
 * mmapping() sections in from the RISCiX binary.  Ideally,
 * the loader (and sbreak) would at least mprotect() to catch
 * wild pointers, but "assuming no input binary bugs", this
 * is OK to get going with.  More fiddly implementations can
 * make things more robust in future.
 */
static uint8_t memory[MEM_SIZE];
uint8_t *mem_base = memory;
static int verbose = 0;        // 0, 1, 2
int stop_simulator = 0;


/* FIXME: get this from params, config, env */
char *their_envp[] = {
        "PATH=/usr/bin:/usr/sbin",
        0
};
int     their_envc;

static void usage(char *thisbin)
{
        printf("%s <filename>\n", thisbin);
}

// Magic debug variable:
#define MAGIC_DEBUG     "RIX_VERBOSE"

static void     check_debug(void)
{
        char *e = getenv(MAGIC_DEBUG);

        if (!e)
                return;
        verbose = atoi(e);
        if (verbose)
                printf("Verbose mode %d\n", verbose);
}

int     main(int argc, char *argv[])
{
        struct ARMul_State *state;

        check_debug();

        if (verbose)
                printf("Init armulator");

        ARMul_EmulateInit();
        state = ARMul_NewState();
        state->verbose = (verbose == 2);
        state->bigendSig = LOW;
        ARMul_CoProInit(state);
        os_init(state, realpath(argv[0], NULL), verbose);

        if (verbose)
                printf(".  Done.\n\n");

        if (argc < 2) {
                usage(argv[0]);
                return 0;
        }

        char   *fname = argv[1];
        char  **their_argv = &argv[1];
        int     their_argc = argc-1;

        for (their_envc = 0; their_envp[their_envc]; their_envc++);

        int r = load_zmagic_binary(state, fname, verbose,
                                   their_argc, their_argv,
                                   their_envc, their_envp);

        if (r < 0) {
                printf("Failed loading %s :(\n", fname);
                return 1;
        }
        if (verbose > 1)
                dump_state(state);
        ARMul_DoProg(state);
        return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Misc armulator rubbish:

void    ARMul_ConsolePrint(ARMul_State *state, const char *format, ...)
{
        va_list ap;
        if (state->verbose) {
                va_start (ap, format);
                vprintf (format, ap);
                va_end (ap);
        }
}

ARMword         ARMul_Debug(ARMul_State *state ATTRIBUTE_UNUSED,
                            ARMword pc ATTRIBUTE_UNUSED,
                            ARMword instr ATTRIBUTE_UNUSED)
{
        return 0;
}

ARMword GetWord(ARMul_State *state, ARMword address)
{
        return ((uint32_t *)memory)[address/4];
}

void    PutWord(ARMul_State *state, ARMword address, ARMword data)
{
        ((uint32_t *)memory)[address/4] = data;
}
