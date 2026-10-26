/* vp_util.c - strings and number formatting. No I/O. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "vp_util.h"

size_t vp_strlcpy(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (size) {
        size_t m = n < size - 1 ? n : size - 1;
        memcpy(dst, src, m);
        dst[m] = '\0';
    }
    return n;
}

int vp_strcasecmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = toupper((unsigned char) *a), y = toupper((unsigned char) *b);
        if (x != y) return x - y;
        if (!x) return 0;
    }
}

int vp_abbrev(const char *a, const char *word, size_t min)
{
    size_t n = strlen(a), i;
    if (n < min || n > strlen(word)) return 0;
    for (i = 0; i < n; i++)
        if (toupper((unsigned char) a[i]) != toupper((unsigned char) word[i])) return 0;
    return 1;
}

void vp_fmt_bytes(char *out, size_t size, double v)
{
    static const char unit[] = "BKMGTP";
    int u = 0;
    if (v < 0) v = 0;
    while (v >= 1024.0 && u < 5) { v /= 1024.0; u++; }
    if (u == 0) snprintf(out, size, "%.0fB", v);
    else if (v < 9.95) snprintf(out, size, "%.1f%c", v, unit[u]);
    else if (v < 1023.5) snprintf(out, size, "%.0f%c", v, unit[u]);
    else snprintf(out, size, "%.1f%c", v / 1024.0, unit[u < 5 ? u + 1 : u]);
}

void vp_fmt_uptime(char *out, size_t size, int64_t secs)
{
    long d, h, m, s;
    if (secs < 0) secs = 0;
    d = (long) (secs / 86400);
    h = (long) (secs % 86400 / 3600);
    m = (long) (secs % 3600 / 60);
    s = (long) (secs % 60);
    if (d) snprintf(out, size, "%ldd %02ld:%02ld", d, h, m);
    else snprintf(out, size, "%02ld:%02ld:%02ld", h, m, s);
}

void vp_fmt_pct(char *out, size_t size, double pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    snprintf(out, size, "%3.0f%%", pct);
}

int vp_parse_cpr(const char *s, int *row, int *col)
{
    const char *e = strrchr(s, '\033');
    int r = 0, c = 0, n = 0;
    if (!e || sscanf(e, "\033[%d;%dR%n", &r, &c, &n) != 2 || n == 0) return 0;
    if (r < 1 || c < 1) return 0;
    *row = r;
    *col = c;
    return 1;
}
