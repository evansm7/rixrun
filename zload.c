/* rixrun RISCiX ZMAGIC loader
 *
 * Copyright (C) 2022 Matt Evans
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Portions are derived from QEMU's linux-user/{flatload.c,elfload.c}, which
 * are:
 *      Copyright (C) 2006 CodeSourcery.
 *	Copyright (C) 2000-2003 David McCullough <davidm@snapgear.com>
 *	Copyright (C) 2002 Greg Ungerer <gerg@snapgear.com>
 *	Copyright (C) 2002 SnapGear, by Paul Dale <pauli@snapgear.com>
 *	Copyright (C) 2000, 2001 Lineo, by David McCullough <davidm@lineo.com>
 * based heavily on:
 *
 * linux/fs/binfmt_aout.c:
 *      Copyright (C) 1991, 1992, 1996  Linus Torvalds
 * linux/fs/binfmt_flat.c for 2.0 kernel
 *	    Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>
 *	JAN/99 -- coded full program relocation (gerg@snapgear.com)
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>

#include "armdefs.h"
#include "rixrun.h"
#include "rix_os.h"
#include "zload.h"


#define DEBUG

#ifdef DEBUG
#define	DBG_ZM(...)	do { if (zmload_verbose) printf(__VA_ARGS__); } while(0)
#else
#define	DBG_ZM(...)
#endif

static int zmload_verbose = 0;


////////////////////////////////////////////////////////////////////////////////
// These routines are, largely, derived from those in QEMU's flatload.c/elfload.c

static void     memcpy_to_target(addr_t dest, void *src, unsigned int len)
{
        void *realdest = (void *)mem_base + (uintptr_t)dest;

        if ((uintptr_t)(dest + len) < MEM_SIZE) {
                memcpy(realdest, src, len);
        }
}

static uintptr_t copy_strings(addr_t p, unsigned int n, char **s)
{
        unsigned int len;

        while (n-- > 0) {
                len = strlen(s[n]) + 1;
                p -= len;
                memcpy_to_target(p, s[n], len);
        }

        return p;
}

static int      target_pread(int fd, addr_t ptr, unsigned int len,
                        unsigned int offset)
{
        if ((ptr + len) > MEM_SIZE) {
                return -EFAULT;
        }
        int r = pread(fd, mem_base + (uintptr_t)ptr, len, offset);
        if (r < 0) {
                return -errno;
        };
        return r;
}

static unsigned int     target_strlen(addr_t ptr)
{
        uint8_t *p = mem_base + (uintptr_t)ptr;
        unsigned int l = 0;

        while (*p++)
                l++;

        return l;
}

#define put_user_ual(val, addr) ((uint32_t *)mem_base)[(addr)/4] = (val)

/* Construct the envp and argv tables on the target stack.  */
static addr_t   loader_build_argptr(int envc, int argc, addr_t sp,
                                    addr_t stringp)
{
        addr_t      envp;
        addr_t      argv;

        DBG_ZM("BINFMT_ZMAGIC: Build args: sp %08x, strings start at %08x; argc %d, envc %d\n",
               sp, stringp, argc, envc);

        // Strings are already on the stack.  Reserve space for zero-terminated env array:
        sp -= 4;
        put_user_ual(0, sp);
        sp -= envc * 4;
        envp = sp;
        // Reserve space for zero-terminated argv array:
        sp -= 4;
        put_user_ual(0, sp);
        sp -= argc * 4;
        argv = sp;

        // Push argc:
        sp -= 4;
        put_user_ual(argc, sp);

        // Fill in the argv array:
        while (argc-- > 0) {
                put_user_ual(stringp, argv);
                argv += 4;
                stringp += target_strlen(stringp) + 1;
        }
        // Fill in the env array:
        while (envc-- > 0) {
                put_user_ual(stringp, envp);
                envp += 4;
                stringp += target_strlen(stringp) + 1;
        }

        return sp;
}


////////////////////////////////////////////////////////////////////////////////


static int get_hdr(char *path, char *newpath, struct exec_hdr *hdr, int rel_path)
{
        static int have_whined = 0;
        char *rootpath = getenv("RIX_ROOT"); // haxlolz
        int fd;

        if (!rootpath) {
                if (!have_whined) {
                        fprintf(stderr, "**** The environment variable RIX_ROOT needs to be set\n"
                                "**** to the path of a RISCiX install, so that shared libraries\n"
                                "**** can be located.  Continuing ...\n\n");
                        have_whined = 1;
                }
                rootpath = "";
        }
        if (rel_path) {
                snprintf(newpath, PATH_MAX, "%s/%s", rootpath, path);
                fd = open(newpath, O_RDONLY);
        } else {
                fd = open(path, O_RDONLY);
        }

        if (fd < 0) {
                return fd;
        }

        int r = read(fd, hdr, sizeof(struct exec_hdr));

        if (r < 0) {
                perror("Can't read lib header");
                goto err;
        } else if (r < sizeof(struct exec_hdr)) {
                DBG_ZM("BINFMT_ZMAGIC: Header read too short, %d vs %ld",
                       r, sizeof(struct exec_hdr));
                r = -1;
                goto err;
        }

        r = 0;
err:
        close(fd);
        return r;
}

static int load_zm_file(struct exec_hdr *hdr,
                        int fd, char *filename,
                        uint32_t *entrypoint)
{
        static addr_t   current_tseg_base = RX_MAP_START_ADDR;

        addr_t textpos = 0, datapos = 0;
        int result;
        addr_t text_len, data_len, bss_len, entry_addr;
        uint32_t magic;
        unsigned int fpos;

        text_len  = hdr->a_exec.a_text; // FIXME LE!
        data_len  = hdr->a_exec.a_data;
        bss_len   = hdr->a_exec.a_bss;
        magic     = hdr->a_exec.a_magic;
        entry_addr = hdr->a_exec.a_entry;   // OR data addr for a lib!

        DBG_ZM("BINFMT_ZMAGIC: Loading file: %s\n", filename);

        if (magic != SPZMAGIC && magic != SLZMAGIC && magic != SLPZMAGIC) {
                fprintf(stderr, "BINFMT_ZMAGIC: bad magic/rev (0x%x)\n", magic);
                return -ENOEXEC;
        }
        int is_lib = magic == SLZMAGIC || magic == SLPZMAGIC;

        DBG_ZM("BINFMT_ZMAGIC: Text segment mapped to %x (len %x)\n",
               current_tseg_base, text_len);

        textpos = current_tseg_base;
        result = target_pread(fd, textpos, text_len, RX_ZM_TEXT_OFFS);
        if (result < 0) {
                fprintf(stderr, "BINFMT_ZMAGIC: Unable to read process text\n");
                return result;
        }
        current_tseg_base += text_len;

        if (data_len) {
                fpos = RX_ZM_TEXT_OFFS + text_len;

                if (is_lib) {
                        // Copy data into data mapping (already mapped outside):
                        DBG_ZM("BINFMT_ZMAGIC: Copying data (%d bytes) from file offset 0x%x "
                               "to data seg at 0x%x\n",
                               (int)data_len, (int)fpos, entry_addr /* AKA a_sldatabase */);

                        result = target_pread(fd, entry_addr,
                                              data_len,
                                              fpos);
                        if (result < 0) {
                                fprintf(stderr, "BINFMT_ZMAGIC: Unable to read data\n");
                                return result;
                        }
                } else {
                        // Real binary: read it to the next spot after last mapping:
                        datapos = current_tseg_base;
                        DBG_ZM("BINFMT_ZMAGIC: Mapping data (%d bytes) from file offset 0x%x "
                               "at end of text seg, 0x%x\n",
                               (int)data_len, (int)fpos, datapos);
                        result = target_pread(fd, datapos,
                                              data_len,
                                              fpos);
                        if (result < 0) {
                                fprintf(stderr, "BINFMT_ZMAGIC: Unable to read process data\n");
                                return result;
                        }
                }
        }
        if (bss_len)
                fprintf(stderr, "BINFMT_ZMAGIC: WARNING: bss_len is non-zero, 0x%x\n", bss_len);

        DBG_ZM("BINFMT_ZMAGIC: Text mapped at 0x%x, entry point is 0x%x, data mapped at 0x%x\n",
               (int)textpos, entry_addr, datapos);


        DBG_ZM("BINFMT_ZMAGIC: Load %s: TEXT=%x-%x DATA=%x-%x BSS=%x-%x\n",
               filename,
               (int) textpos, (int) (textpos + text_len),
               (int) datapos, (int) (datapos + data_len),
               (int) (datapos + data_len),
               (int) (((datapos + data_len + bss_len) + 3) & ~3));

        if (entrypoint)
                *entrypoint = entry_addr;

        return 0;
}


/****************************************************************************/

struct libstuff {
        struct exec_hdr hdr;
        int fd;
        char path[PATH_MAX];
        char realpath[PATH_MAX]; // Host path
};

static struct libstuff libi[MAX_SHARED_LIBS];


/* RISCiX binary loading
 *
 * The common case is for a binary to be shared, which means it points to
 * *one* shared library (see a_shlibname).  The magic number of the lib
 * identifies it as a shared library, or a shared library itself dependent on
 * another shared library.  Libraries are loaded into the address space at
 * 0x8000 upwards, starting with libc (the typical "primordial" library),
 * followed by the lib dependent on that, followed by the lib dependent on that,
 * etc. etc., followed by the original binary's text/initdata segment.
 *
 * Loading algorithm:
 *      open object
 *      find lib
 *      recurse until object doesn't import anything -- final lib (libc)
 *
 * (Final lib has a different magic, doesn't share!)
 *
 * Load libs in reverse order starting from 0x8000.  Use a_text to find end,
 * point at which next object is loaded.  Also point in file at which to load ROdata,
 * using a_data length.
 * For a lib, use a_sldatabase to copy from data seg to high-up place (somehow
 * do a bounds check? FIXME).  The stacktop then lands below the last-loaded
 * data chunk up in the 0x017xxxxx area.
 *
 * Example:                /usr/lib/c:9010241403.11
 *      a_text = 0x30000        (File offs 8000-37fff ends up at 8000-37fff)
 *      a_data = 0x1874
 *      a_bss = 0
 *      a_syms = 0
 *      a_sldatabase = 0x17f678c        (File offs 0x38000+a_data ends up here,
 * Note file is 0x39874 in size, so fits nicely.)
 * IDK how bss works.
 *
 * Layout of binary:
 * <header, padded to 32K>
 * <text>
 * <data, no gap>
 * <symbols>
 */

/* Main loader function */
int load_zmagic_binary(struct ARMul_State *state, char *filename,
                       int verbose,
                       int argc, char *argv[],
                       int envc, char *envp[])
{
        addr_t                  stack_len;
        addr_t                  start_addr;
        addr_t                  sp, p;
        int                     res;
        struct exec_hdr         hdr;
        int                     fd;
        char                    realpath[PATH_MAX]; // huge!

        zmload_verbose = verbose;

        DBG_ZM("BINFMT_ZMAGIC: Loading file: %s\n", filename);

        /* This is all a bit hacky, currently depending on all user
         * memory being mapped.  It would be better to use mmap
         * (e.g. MAP_FIXED over a reserved VMA).
         */

        sp = RX_MAP_DATA_ADDR + RX_MAP_DATA_LEN;

        if (get_hdr(filename, realpath, &hdr, 0 /* Linux path */) < 0) {
                fprintf(stderr, "Can't open %s\n", filename);
                return -1;
        }
        uint32_t magic = hdr.a_exec.a_magic;

        /* Assume originally-given binary is shared (could skip lib search if not).
         * FIXME: Easy to support non-shared binaries, but I haven't run into any
         * I need.
         */
        if (magic != SPZMAGIC) {
                fprintf(stderr, "BINFMT_ZMAGIC: bad magic/rev (0x%x, need 0x%x)\n",
                        magic, (int) SPZMAGIC);
                return -ENOEXEC;
        }
        DBG_ZM("BINFMT_ZMAGIC: Uses shared lib %s\n", hdr.a_shlibname);

        unsigned int lnum = 0;
        /* Follow library chain:
         * (if) Initial binary is SPZMAGIC, follow path to lib.
         * If lib is SLPZMAGIC, follow path to next lib, else
         * if lib is SLZMAGIC it's the last one (likely libc), loaded first.
         */
        char *new_lib = hdr.a_shlibname;
        do {
                strncpy(libi[lnum].path, new_lib, PATH_MAX);

                if (get_hdr(libi[lnum].path, libi[lnum].realpath, &libi[lnum].hdr, 1) < 0)
                        return -1;

                uint32_t lmagic = libi[lnum].hdr.a_exec.a_magic;
                if (lmagic == SLZMAGIC) {
                        DBG_ZM("BINFMT_ZMAGIC: Reached final lib\n");
                        new_lib = 0;
                } else if (lmagic == SLPZMAGIC) {
                        new_lib = libi[lnum].hdr.a_shlibname;
                        DBG_ZM("BINFMT_ZMAGIC: Reached shared lib using shared lib %s\n", new_lib);
                } else {
                        fprintf(stderr, "BINFMT_ZMAGIC: Unrecognised magic 0x%x in library %s\n",
                                lmagic, new_lib);
                        return -1;
                }
                lnum++;
                if (lnum == (MAX_SHARED_LIBS-1) && new_lib) {
                        fprintf(stderr, "BINFMT_ZMAGIC: Too many libs, increase max?\n");
                        return -1;
                }
        } while (new_lib != 0);

        // Now, load libs in reverse order from bottom up:
        for (int i = lnum-1; i >= 0; i--) {
                DBG_ZM("BINFMT_ZMAGIC: Loading lib %s\n", libi[i].realpath);

                fd = open(libi[i].realpath, O_RDONLY);
                if (fd < 0) {
                        perror("BINFMT_ZMAGIC: Library open:");
                        return fd;
                }
                addr_t data_addr = ~0;
                res = load_zm_file(&libi[i].hdr, fd, libi[i].path, &data_addr);
                close(fd);
                if (res < 0) {
                        return res;
                }
                // For a library, this is where data was loaded; move SP below it:
                if (sp >= data_addr)
                        sp = data_addr - 4;
        }

        // Finally, load the initial binary
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
                perror("BINFMT_ZMAGIC: Main binary open:");
                return fd;
        }
        res = load_zm_file(&hdr, fd, filename, &start_addr);
        close(fd);
        if (res < 0) {
                return res;
        }

        /* Now set up initial stack contents -- args and environment strings. */
        DBG_ZM("BINFMT_ZMAGIC: Stack top 0x%x\n", sp);
        addr_t stack_top = sp;
        addr_t env_start, arg_start;
        /* Copy argv/envp.  */
        env_start = copy_strings(sp, envc, envp);
        arg_start = copy_strings(env_start, argc, argv);
        /* Align stack. (Word is OK.) */
        sp = arg_start & ~3;
        sp = loader_build_argptr(envc, argc, sp, arg_start);

        DBG_ZM("BINFMT_ZMAGIC: Final SP 0x%x, entry point 0x%x\n", sp, start_addr);

#if 0
        // Cheeky dump stack:
        for (addr_t dp = sp; dp < stack_top; dp += 4) {
                DBG_ZM("%08x:  %08x\n", dp, *(uint32_t *)&mem_base[dp]);
        }
#endif

        // Set up ARM regs:
        ARMul_SetPC(state, start_addr);
        ARMul_SetReg(state, state->Mode, 13, sp);

        return 0;
}
