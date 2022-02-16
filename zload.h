#ifndef ZLOAD_H
#define ZLOAD_H

#include <inttypes.h>
#include "rix_os.h"
#include "armdefs.h"

/* Functions */

int     load_zmagic_binary(struct ARMul_State *state, char *filename,
                           int verbose,
                           int argc, char *argv[],
                           int envc, char *envp[]);


#define RX_MAP_START_ADDR       0x8000
#define RX_ZM_TEXT_OFFS         0x8000 // Offset into file of first segment
#define RX_MAP_DATA_LEN         0x100000
#define RX_MAP_DATA_ADDR        (0x01800000-RX_MAP_DATA_LEN)

#define MAX_SHARED_LIBS 4

/* These structures & values mirror those documented in RISCiX's
 * /usr/include/sys/exec.h
 */

struct rix_exec {
        uint32_t   a_magic;
        uint32_t   a_text;      /* Text size */
        uint32_t   a_data;      /* Init data size */
        uint32_t   a_bss;       /* BSS size */
        uint32_t   a_syms;      /* Symbols size */
        uint32_t   a_entry;     /* Entry addr, or a_sldatabase -- data pos for shlibs */
        uint32_t   a_trsize;
        uint32_t   a_drsize;
};

struct rix_version {
        uint32_t        ids;    /* Version numbers */
        char            version[32];
};

/* a.out header */
struct exec_hdr {
        struct rix_exec         a_exec;
        struct rix_version      a_version;
        uint32_t                a_sq4items;
        uint32_t                a_sq3items;
        uint32_t                a_sq4size;
        uint32_t                a_sq3size;
        uint32_t                a_sq4last;
        uint32_t                a_sq3last;
        rix_time_t              a_timestamp;
        rix_time_t              a_shlibtime;
        char                    a_shlibname[60];        /* Path to shared lib */
};

/* We don't support old binary formats (like IMAGIC/OMAGIC/NMAGIC),
 * and don't support any squeezed binaries.  (Unsqueeze them first!)
 */
#define ZMAGIC          0413
#define MF_USES_SL      02000
#define MF_IS_SL        04000
#define SPZMAGIC        (MF_USES_SL|ZMAGIC)     /* Consumes shared library */
#define SLZMAGIC        (MF_IS_SL|ZMAGIC)       /* Primordial shared libary (e.g. libc) */
#define SLPZMAGIC       (MF_USES_SL|SLZMAGIC)   /* Shared lib itself with shared lib */

#endif
