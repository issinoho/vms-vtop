/* probe_common.h - helpers shared by the Phase 0 probes (VSI C, 32-bit pointers).

   ILE3 item lists and string descriptors are built by hand, as in vms-lighttpd's
   vms_privs.c, so the probes do not depend on which of <iledef.h>/<descrip.h>'s
   spellings this compiler version provides. */
#ifndef PROBE_COMMON_H
#define PROBE_COMMON_H

#include <stdio.h>
#include <string.h>
#include <descrip.h>
#include <starlet.h>

typedef struct { unsigned short buflen, code; void *buf; unsigned short *retlen; } ile3;

static void ile3_set(ile3 *it, unsigned short code, void *buf, unsigned short len,
                     unsigned short *retlen)
{
    it->buflen = len; it->code = code; it->buf = buf; it->retlen = retlen;
}

static void ile3_end(ile3 *it) { memset(it, 0, sizeof *it); }

static void desc_set(struct dsc$descriptor_s *d, const char *s, unsigned short len)
{
    d->dsc$w_length = len;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = (char *) s;
}

/* The $GETMSG text of a condition value, e.g. "%SYSTEM-F-NOPRIV, ...". */
static const char *msg(unsigned int cond)
{
    static char buf[257];
    unsigned short len = 0;
    struct dsc$descriptor_s d;
    desc_set(&d, buf, sizeof buf - 1);
    if (!(sys$getmsg(cond, &len, &d, 15, 0) & 1)) { sprintf(buf, "%%X%08X", cond); return buf; }
    buf[len] = '\0';
    return buf;
}

static void report(const char *what, unsigned int st)
{
    printf("  %-34s %s %s\n", what, (st & 1) ? "OK  " : "FAIL", msg(st));
}

/* Trailing blanks off a space-padded field. */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n != 0 && s[n - 1] == ' ') s[--n] = '\0';
}

#endif
