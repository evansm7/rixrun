/* rixrun OS emulation routines
 *
 * This "fuse" provides a trivial RISCiX syscall emulation on Linux.
 * There are many shortcuts/hacks and lazinesses within, this is not
 * Good Code (TM)!
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

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "utils.h"
#include "armdefs.h"
#include "armemu.h"
#include "rixrun.h"
#include "rix_os.h"

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#else
#include <endian.h>
#endif

#define SC_TRACE
#ifdef SC_TRACE
#define SYSTRACE(x...)          do { if (sc_trace) fprintf(stderr, "SC: " x); } while(0)
#define SYSTRACE_OUT(x...)      do { if (sc_trace) fprintf(stderr, x); } while(0)
#define SDBG(x...)              do { if (sc_trace) fprintf(stderr, x); } while(0)
#else
#define SYSTRACE(x...)          do { } while(0);
#define SYSTRACE_OUT(x...)      do { } while(0);
#define SDBG(x...)              do { } while(0);
#endif

#define SC_1ARG                                                 \
        uint32_t a0 = ARMul_GetReg(state, state->Mode, 0)
#define SC_2ARG                                                 \
        uint32_t a0 = ARMul_GetReg(state, state->Mode, 0);      \
        uint32_t a1 = ARMul_GetReg(state, state->Mode, 1)
#define SC_3ARG                                                 \
        uint32_t a0 = ARMul_GetReg(state, state->Mode, 0);      \
        uint32_t a1 = ARMul_GetReg(state, state->Mode, 1);      \
        uint32_t a2 = ARMul_GetReg(state, state->Mode, 2)
#define SC_4ARG                                                 \
        uint32_t a0 = ARMul_GetReg(state, state->Mode, 0);      \
        uint32_t a1 = ARMul_GetReg(state, state->Mode, 1);      \
        uint32_t a2 = ARMul_GetReg(state, state->Mode, 2);      \
        uint32_t a3 = ARMul_GetReg(state, state->Mode, 3)

#define SC_RET_VAL(fmt, v) do {                                 \
                SYSTRACE_OUT(" = " fmt "\n", v);                \
                ARMul_SetReg(state, state->Mode, 0, v);         \
                CLEARC;                                         \
        } while(0)

#define SC_RET_ERROR(e) do {                                    \
                SYSTRACE_OUT(" = ERR(%d)\n", e);                \
                ARMul_SetReg(state, state->Mode, 0, (e));       \
                SETC;                                           \
        } while(0)

#define SC_RET_NONE()   do {                                    \
                CLEARC;                                         \
        } while(0)


static  char    *path_to_rixrun;
static  int     sc_trace = 0;

static ARMul_State state_vfork_backup;
static int vfork_ret_status = 0;

////////////////////////////////////////////////////////////////////////////////
// Mappings of stuff

static int      host_to_rix_errno(int e)
{
        if (e < 35) {
                return e;
        } else {
                fprintf(stderr, "rixrun: errno %d not mapped, FIXME!\n", e);
                return e;
        }
}

static int      rix_to_host_openflags(int f)
{
        int o = f & 3; // ACCMODE is easy

        if (f & (1<<9))         o |= O_CREAT;
        if (f & (1<<11))        o |= O_EXCL;
        if (f & (1<<12))        o |= O_NOCTTY;
        if (f & (1<<10))        o |= O_TRUNC;
        if (f & (1<<3))         o |= O_APPEND;
        if (f & (1<<14))        o |= O_NONBLOCK;
        if (f & (1<<13))        o |= O_SYNC;

        return o;
}

static unsigned int     host_to_rix_mode(unsigned int im)
{
        unsigned int om = im;

        // S_IFMT is in the same place, and values the same.
        return om;
}

static void     host_to_rix_stat(struct rix_stat *osb, struct stat *isb)
{
        // FIXME: massive endianness crimes committed here!  (Assumes LE :( )
        osb->st_dev = htole16(0x0101);           // Who cares?
        osb->st_ino = htole32(isb->st_ino);      // trunc!
        osb->st_mode = htole16(host_to_rix_mode(isb->st_mode));
        osb->st_nlink = htole16(isb->st_nlink);
        osb->st_uid = htole16(isb->st_uid);      // XXX truncates!
        osb->st_gid = htole16(isb->st_gid);
        osb->st_rdev = htole16(0x0101);
        if (isb->st_size > 0xffffffff) {
                fprintf(stderr, "rixrun: stat size %ld can't be represented, truncating\n",
                        (long)isb->st_size);
        }
        osb->st_size = htole32(isb->st_size);
#ifdef __APPLE__
        osb->st_a_time = htole32(isb->st_atimespec.tv_sec);
        osb->st_m_time = htole32(isb->st_mtimespec.tv_sec);
        osb->st_c_time = htole32(isb->st_ctimespec.tv_sec);
#else
        osb->st_a_time = htole32(isb->st_atim.tv_sec);
        osb->st_m_time = htole32(isb->st_mtim.tv_sec);
        osb->st_c_time = htole32(isb->st_ctim.tv_sec);
#endif
        osb->st_blksize = htole32(isb->st_blksize);
	osb->st_blocks = htole32(isb->st_blocks); // Convert to 4K?
        /* SDBG("Stat %04x %08x %03x %04x %04x %04x %04x\n" */
        /*      "   %08x %08x:%08x:%08x %08x %08x\n", */
        /*      osb->st_dev, osb->st_ino, osb->st_mode, osb->st_nlink, */
        /*      osb->st_uid, osb->st_gid, osb->st_rdev, */
        /*      osb->st_size, osb->st_a_time, osb->st_m_time, osb->st_c_time, */
        /*      osb->st_blksize, osb->st_blocks); */
}

static  uint32_t        read32(addr_t a)
{
        return le32toh(*(uint32_t *)(mem_base + a));
}

static  uint8_t         read8(addr_t a)
{
        return *(uint8_t *)(mem_base + a);
}

static  void        	write32(addr_t a, uint32_t data)
{
        *(uint32_t *)(mem_base + a) = htole32(data);
}

////////////////////////////////////////////////////////////////////////////////

void    rix_sc_exit(ARMul_State *state)
{
        SC_1ARG;
        SYSTRACE("exit(%d)", a0);

        exit(a0);
}

void    rix_sc_read(ARMul_State *state)
{
        SC_3ARG;
        SYSTRACE("read(%d, %08x, %08x)", a0, a1, a2);
        int r = read(a0, mem_base + a1, a2);
        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_write(ARMul_State *state)
{
        SC_3ARG;
        SYSTRACE("write(%d, %08x, %08x)", a0, a1, a2);
        int r = write(a0, mem_base + a1, a2);
        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_close(ARMul_State *state)
{
        SC_1ARG;
        SYSTRACE("close(%d)", a0);
        int r;
        if (a0 > 2)
                r = close(a0);
        else
                r = 0;

        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_creat(ARMul_State *state)
{
        SC_2ARG;
        SYSTRACE("creat(\"%s\", %08x)", mem_base + a0, a1);
        int r = creat((char *)(mem_base + a0), a1);
        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_link(ARMul_State *state)
{
        SC_2ARG;
        SYSTRACE("link(\"%s\", \"%s\")", mem_base + a0, mem_base + a1);
        int r = link((char *)(mem_base + a0), (char *)(mem_base + a1));
        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_unlink(ARMul_State *state)
{
        SC_1ARG;
        SYSTRACE("unlink(\"%s\")", mem_base + a0);
        int r = unlink((char *)(mem_base + a0));
        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_waitpid(ARMul_State *state)
{
        SC_3ARG;
        SYSTRACE("waitpid(%d, 0x%x, 0x%x)", a0, a1, a2);
        int r = -1;

        if (a0 < 1 || a0 == 1234) {       /* magic vfork PID */
                if (a1) {
                        write32(a1, vfork_ret_status);
                }
                // Fake success
                r = 1234;
        }

        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_sbreak(ARMul_State *state)
{
        static addr_t current_sbrk = 0;
        SC_1ARG;
        SYSTRACE("sbreak(%08x)", a0);
        addr_t old_sbrk = current_sbrk;
        current_sbrk = (int)a0; // FIXME: check for ludicrous values (or negative..)
        SC_RET_VAL("%08x", 0); // FIXME: Check mem limit, return error (ENOMEM?)
}

void    rix_sc_lseek(ARMul_State *state)
{
        SC_3ARG;
        SYSTRACE("lseek(%d, %08x, %d)", a0, a1, a2);
        int r = lseek(a0, a1, a2);
        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_getpid(ARMul_State *state)
{
        // Need a 16b PID, waah!  This is broken on modern systems.
        SYSTRACE("getpid()");
        SC_RET_VAL("%d", getpid());
}

void    rix_sc_open(ARMul_State *state)
{
        SC_3ARG;
        char *pathname = (char *)(mem_base + a0); // Note, not remapped
        SYSTRACE("open(\"%s\", %08x, %08x)", pathname, a1, a2);
        int r = open(pathname, rix_to_host_openflags(a1), a2);

        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_access(ARMul_State *state)
{
        SC_2ARG;
        char *pathname = (char *)(mem_base + a0); // Note, not remapped
        SYSTRACE("access(\"%s\", %08x)", pathname, a1);
        int r = access(pathname, a1); // Note flags same on Linux :p

        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

int     rix_execve_handler(ARMul_State *state, uint32_t a1, uint32_t a2);

void    rix_sc_execve(ARMul_State *state)
{
        SC_3ARG;
        SYSTRACE("execve(\"%s\", %08x, %08x)\n", mem_base + a0, a1, a2);

        int r = rix_execve_handler(state, a1, a2);

        if (r) {
                // Dump args/env:
                addr_t p = read32(a1);

                int i = 0;
                while (p) {
                        printf("Arg[%d]@%08x=%08x='%s'\n", i++, a1, p, mem_base + p);
                        a1 += 4;
                        p = read32(a1);
                }
                p = read32(a2);
                i = 0;
                while (p) {
                        printf("Env[%d]@%08x=%08x='%s'\n", i++, a2, p, mem_base + p);
                        a2 += 4;
                        p = read32(a2);
                }
                fprintf(stderr, "rix_sc_execve: not implemented!\n");
                SC_RET_ERROR(ENOENT);
        }
        /* Otherwise, the handler has munged CPU state to "return in the parent"
         * with a PID.
         */
}

void    rix_sc_fstat(ARMul_State *state)
{
        SC_2ARG;
        SYSTRACE("fstat(%d, %08x)", a0, a1);
        struct stat sb;
        int r = fstat(a0, &sb);
        if (r < 0) {
                SC_RET_ERROR(host_to_rix_errno(errno));
        } else {
                host_to_rix_stat((struct rix_stat *)(mem_base + a1), &sb);
                SC_RET_VAL("%d", r);
        }
}

void    rix_sc_getpagesize(ARMul_State *state)
{
        SYSTRACE("getpagesize()");
        SC_RET_VAL("%d", 32768);
}

void    rix_sc_vfork(ARMul_State *state)
{
        /* Here lies gross hack #28.  This "implementation" supports the
         * pattern of some RISCiX utils using system(), i.e. invoking a
         * UNIX command and waiting for it to complete.
         *
         * We stash CPU thread state here, and return as though we're
         * the child process (without making a new process).  execve
         * does the command, and when complete that syscall restores
         * the stashed state fixed up as though returning in the parent
         * (returns a PID).  The PID is fake, and wait() just consumes
         * it.  This is all horribly broken except in the one case I
         * care about when using unsqueeze/cc/etc.
         */
        SYSTRACE("vfork()");
        memcpy(&state_vfork_backup, state, sizeof(*state));
        SC_RET_VAL("%d", 0);
}

void    rix_sc_getdtablesize(ARMul_State *state)
{
        // Dunno what this is on RISCiX.
        SYSTRACE("getdtablesize()");
        SC_RET_VAL("%d", 512);
}

void    rix_sc_gettimeofday(ARMul_State *state)
{
        SC_2ARG;
        SYSTRACE("gettimeofday(%08x, %08x)", a0, a1);

        // TZ is ignored!
        struct timeval tv;
        gettimeofday(&tv, NULL);
        // Do this by proxy to protect against 64b time_t.
        write32(a0, tv.tv_sec);
        write32(a0 + 4, tv.tv_usec);
        SC_RET_VAL("%d", 0);
}

void    rix_sc_getrusage(ARMul_State *state)
{
        SC_2ARG;
        SYSTRACE("getrusage(%d, %08x)", a0, a1);

        // Basically a NOP, but at least write to the output structure...
        bzero(mem_base + a1, 8 + // timeval
              8 + // timeval
              14*4);

        SC_RET_VAL("%d", 0);
}

void    rix_sc_ftruncate(ARMul_State *state)
{
        SC_2ARG;
        SYSTRACE("ftruncate(%d, %08x)", a0, a1);

        int r = ftruncate(a0, a1);

        if (r < 0)
                SC_RET_ERROR(host_to_rix_errno(errno));
        else
                SC_RET_VAL("%d", r);
}

void    rix_sc_NOP(ARMul_State *state, char *name)
{
        SC_4ARG;
        SYSTRACE("%s(%08x, %08x, %08x, %08x)",
                 name, a0, a1, a2, a3);
        SC_RET_VAL("%d", 0);
}


////////////////////////////////////////////////////////////////////////////////

// From GDB's sim/arm/armos.h:
#define FPESTART        0x2000L
#define FPEEND          0x8000L
#define FPEOLDVECT      FPESTART + 0x100L + 8L * 16L + 4L    /* stack + 8 regs + fpsr */
#define FPENEWVECT(addr) 0xea000000L + ((addr) >> 2) - 3L       /* branch from 4 to 0x2400 */

#include "armfpe.h"

void    os_init(ARMul_State *state, char *me_realpath, int verbose)
{
        path_to_rixrun = me_realpath;
        sc_trace = verbose;

        // Install FPE (based on GDB's armulator's armos.c)
        int i;
        for (i = 0; i < fpesize; i += 4)      /* copy the code */
                write32(FPESTART + i, fpecode[i >> 2]);
        // Find entry ptr:
        for (i = FPESTART + fpesize;; i -= 4) {
                unsigned int j;
                if ((j = read32(i)) == 0xffffffff)
                        break;
        }
        /* install new vector */
        write32(4, FPENEWVECT(read32(i - 4)));

        // Set up SVC stack below FPE:
        ARMul_SetReg(state, SVC26MODE, 13, FPESTART - 4);

        // Change to user mode for the running program:
        SETABORT(0, USER26MODE);
        ARMul_CPSRAltered(state);
}

unsigned int    ARMul_OSHandleSWI(ARMul_State *state, ARMword number)
{
        /* printf("Got SWI 0x%x at PC %08x\n", number, ARMul_GetPC(state)); */
        unsigned int scnum = number & 0xfffff;

        switch(scnum) {
        case 1:         /* exit         */      rix_sc_exit(state);             break;
        case 3:         /* read         */      rix_sc_read(state);             break;
        case 4:         /* write        */      rix_sc_write(state);            break;
        case 6:         /* close        */      rix_sc_close(state);            break;
        case 8:         /* creat        */      rix_sc_creat(state);            break;
        case 9:         /* link         */      rix_sc_link(state);             break;
        case 10:        /* unlink       */      rix_sc_unlink(state);           break;
        case 11:        /* waitpid      */      rix_sc_waitpid(state);          break;
        case 15:        /* chmod        */      rix_sc_NOP(state, "chmod");     break;
        case 16:        /* chown        */      rix_sc_NOP(state, "chown");     break;
        case 17:        /* sbreak       */      rix_sc_sbreak(state);           break;
        case 19:        /* lseek        */      rix_sc_lseek(state);            break;
        case 20:        /* getpid       */      rix_sc_getpid(state);           break;
        case 28:        /* open         */      rix_sc_open(state);             break;
        case 34:        /* access       */      rix_sc_access(state);           break;
        case 54:        /* ioctl        */      rix_sc_NOP(state, "ioctl");     break;
        case 59:        /* execve       */      rix_sc_execve(state);           break;
        case 60:        /* umask        */      rix_sc_NOP(state, "umask");     break;
        case 62:        /* fstat        */      rix_sc_fstat(state);            break;
        case 64:        /* getpagesize  */      rix_sc_getpagesize(state);      break;
        case 66:        /* vfork        */      rix_sc_vfork(state);            break;
        case 89:        /* getdtablesize*/      rix_sc_getdtablesize(state);    break;
        case 108:       /* sigvec       */      rix_sc_NOP(state, "sigvec");    break;
        case 109:       /* sigblock     */      rix_sc_NOP(state, "sigblock");  break;
        case 110:       /* sigsetmask   */      rix_sc_NOP(state, "sigsetmask");break;
        case 112:       /* sigstack     */      rix_sc_NOP(state, "sigstack");  break;
        case 116:       /* gettimeofday */      rix_sc_gettimeofday(state);     break;
        case 117:       /* getrusage    */      rix_sc_getrusage(state);        break;
        case 130:       /* ftruncate    */      rix_sc_ftruncate(state);        break;

        default:
                panic("*** Unhandled syscall %d at PC %08lx\n", scnum, ARMul_GetPC(state));
        }
        return 1;
}

void dump_state(ARMul_State *state)
{
        for (int i = 0; i < 15; i++) {
                printf("R%02d %08x      ", i, ARMul_GetReg(state, state->Mode, i));
                if ((i & 3) == 3)
                        printf("\n");
        }
        printf("R15 %08x, mode %d\n", ARMul_GetPC(state), state->Mode);
}

unsigned int    ARMul_OSException(ARMul_State * state, ARMword vector,
                                  ARMword pc)
{
        if (vector == 0x4) {    /* Undefined, assume FPE! */
                if (state->verbose > 1) {
                        printf("*** UNK exception @PC%08x, calling vector\n", pc);
                        dump_state(state);
                }
                return 0;       // Go to exception vector
        } else {
                panic("Got exception (vector 0x%lx), PC %08x\n",
                      vector, pc);
        }

        return 1; // Don't do exception vectors, etc.
}

static char cmd_buff[4096];

int     rix_execve_handler(ARMul_State *state, uint32_t a1, uint32_t a2)
{
        const int args_max = 16;
        char *args[args_max];

        /* Try to spot certain common patterns of system() invocations,
         * and deal with those alone.
         *
         * a1->argv, a2->envv
         * First, convert args:
         */
        int i = 0;
        addr_t arg = read32(a1);
        do {
                if (arg != 0) {
                        args[i] = mem_base + arg;
                } else {
                        args[i] = 0;
                        break;
                }
                i++;
                a1 += 4;
                arg = read32(a1);
        } while(i < args_max);
        if (i == args_max)
                return 1;

        /* OK, try to match common 'executions' via system(). */
        if (!strcmp(args[0], "sh") && !strcmp(args[1], "-c")) {
                if (!strncmp(args[2], "/sbin/cp ", 9)) {
                        char *cp_args = (char *)(args[2] + 9);
                        snprintf(cmd_buff, 4096, "cp %s", cp_args);
                        vfork_ret_status = system(cmd_buff);
                        SDBG("execve handler: %s (-> %d)\n", cmd_buff, vfork_ret_status);
                        goto handled_return;
                }

                /* FIXME:
                 * Extend this with the ability to re-invoke rixrun with commands
                 * relative to RIX_ROOT!
                 */
        }

fail:
        return 1;       /* Unhandled */

handled_return:
        // Assumes preceeded by one vfork!!!1
        memcpy(state, &state_vfork_backup, sizeof(*state));
        /* Returns the process to the state at the vfork; now make it look like
         * the child was exec'd, and was process 1234.  It's complete of course,
         * which we'll say via wait() if asked.
         */
        SC_RET_VAL("%d", 1234);
        return 0;       /* Unhandled */
}
