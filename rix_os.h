#ifndef RIX_OS_H
#define RIX_OS_H

#include <inttypes.h>
#include "armdefs.h"

void    os_init(ARMul_State *state, char *me_realpath, int verbose);

/* RISCiX syscall interface structures/definitions */

typedef int16_t         rix_dev_t;
typedef uint32_t        rix_ino_t;
typedef uint16_t        rix_mode_t;
typedef uint16_t        rix_nlink_t;
typedef uint16_t        rix_uid_t;
typedef uint16_t        rix_gid_t;
typedef int32_t         rix_off_t;
typedef int32_t         rix_time_t;

struct rix_stat // FIXME, packed
{
        rix_dev_t       st_dev;
        uint8_t         __pad[2];
        rix_ino_t       st_ino;
        rix_mode_t      st_mode;
        rix_nlink_t     st_nlink;
        rix_uid_t       st_uid;
        rix_gid_t       st_gid;
        rix_dev_t       st_rdev;
        uint8_t         __pad1[2];
        rix_off_t       st_size;
        rix_time_t      st_a_time;
        int32_t         st_spare1;
        rix_time_t      st_m_time;
        int32_t         st_spare2;
        rix_time_t      st_c_time;
        int32_t         st_spare3;
        int32_t         st_blksize;
        int32_t         st_blocks;
        int32_t         st_spare4[2];
};

#endif
