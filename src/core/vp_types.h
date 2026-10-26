/* vp_types.h - what the platform reports about the system, and key codes.

   Plain numbers and strings, no VMS types (D3). Cumulative counters only grow,
   except across a reset (a reboot, a CPU set change) that the model detects. */
#ifndef VP_TYPES_H
#define VP_TYPES_H

#include <stdint.h>

#define VP_MAX_CPU  64
#define VP_MAX_DISK 32
#define VP_MAX_NIC  8
#define VP_NAME     32

/* processor modes, as RMI$_MODES has them (beszel N3); compatibility counts as user */
enum { VP_M_INT, VP_M_MPSYNC, VP_M_KERNEL, VP_M_EXEC, VP_M_SUPER, VP_M_USER, VP_M_IDLE,
       VP_NMODES };

typedef struct { uint64_t t[VP_NMODES]; } vp_modes;      /* ticks, any fixed unit */

typedef struct {
    char name[VP_NAME];         /* "DKA0:", "DSA1:" */
    char label[16];             /* volume label, "" if none */
    uint64_t total, free;       /* bytes */
    uint32_t ops;               /* DVI$_OPCNT: operations since boot, wraps at 2^32 (N4) */
    uint32_t errors;
    int mntver, offline, shadow;
} vp_disk;

typedef struct {
    char name[VP_NAME];         /* "EWA0" */
    uint64_t rx, tx;            /* bytes since boot */
} vp_nic;

typedef struct {
    uint64_t ms;                /* when it was taken, monotonic milliseconds */
    char clock[12];             /* local time "14:02:33" */
    int64_t uptime;             /* seconds */
    int ncpu;
    vp_modes cpu[VP_MAX_CPU];   /* per CPU, cumulative */
    uint64_t mem_total, mem_free, mem_modified;     /* bytes */
    uint64_t page_total, page_free, swap_total, swap_free;
    int processes;
    double compute_queue;       /* processes waiting for a CPU */
    int ndisk;
    vp_disk disk[VP_MAX_DISK];
    int nnic;
    vp_nic nic[VP_MAX_NIC];
} vp_sample;

typedef struct {
    char node[16];              /* "X86VMS" */
    char version[32];           /* "OpenVMS E9.2-4" */
    char model[96];
    char arch[16];              /* "x86_64", "IA64" */
} vp_identity;

/* one process from a $GETJPI scan (N1, N2) */
typedef struct {
    uint32_t pid, owner;        /* owner: the creator's PID, 0 for none */
    uint32_t uic;
    int state;                  /* the SCH$C_ code: 14 CUR, 7 HIB, 5 LEF (N2) */
    int pri, prib;              /* current and base priority */
    int mode;                   /* 0 other, 1 network, 2 batch, 3 interactive */
    int threads;
    uint32_t cputim;            /* 10 ms ticks */
    uint64_t mem;               /* working set in use, bytes */
    uint32_t faults, dirio, bufio;
    char name[16], user[13], image[40], term[9];    /* image: the name part only */
} vp_proc;

enum { VP_SCOPE_OWN, VP_SCOPE_GROUP, VP_SCOPE_WORLD };

typedef struct {
    int total;                  /* processes the scan could show (more than fitted?) */
    int hidden;                 /* SS$_NOPRIV: not the caller's to see (D5) */
    int scope;                  /* VP_SCOPE_* from the caller's privileges */
    uint32_t self;              /* VTOP's own PID */
} vp_scan;

typedef struct { char name[12]; uint32_t used, limit; int bytes; } vp_quota;  /* bytes: in bytes */

/* what the details page shows beyond vp_proc */
typedef struct {
    uint32_t pid;
    char image[256];            /* the full file spec, "" at DCL level */
    char login[24];             /* "26-OCT-2026 13:02:11" */
    char account[9];
    char privs[400];            /* current privileges, "TMPMBX NETMBX" */
    uint32_t wssize, wsquota, wsextent;     /* bytes */
    int nquota;
    vp_quota quota[12];
} vp_proc_detail;

/* process actions (D4) */
enum { VP_ACT_NONE, VP_ACT_SUSPEND, VP_ACT_RESUME, VP_ACT_DELETE, VP_ACT_PRIORITY };

/* keys: characters are themselves (case kept); the rest from 256 */
enum {
    VK_TIMEOUT = -2, VK_NONE = -1,
    VK_CTRL_H = 8, VK_CTRL_U = 21, VK_CTRL_W = 23, VK_CTRL_Z = 26, VK_RETURN = 13,
    VK_ESC = 27, VK_DEL = 127,
    VK_UP = 256, VK_DOWN, VK_LEFT, VK_RIGHT, VK_PREV, VK_NEXT,
    VK_F10, VK_HELP, VK_DO, VK_PF1, VK_PF2, VK_PF3, VK_PF4,
    VK_QUIT,                    /* Ctrl/C or Ctrl/Y: leave now */
    VK_OTHER
};

/* graph and colour modes (D6, D7) */
enum { VP_G_BRAILLE, VP_G_BLOCK, VP_G_VT, VP_NGRAPH };
enum { VP_C_24, VP_C_256, VP_C_8, VP_C_MONO, VP_NCOLOUR };

#endif
