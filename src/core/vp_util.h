/* vp_util.h - strings and number formatting. No I/O. */
#ifndef VP_UTIL_H
#define VP_UTIL_H

#include <stddef.h>
#include <stdint.h>

size_t vp_strlcpy(char *dst, const char *src, size_t size);
int vp_strcasecmp(const char *a, const char *b);
/* a is an abbreviation of word, at least min characters long ("/GRA" of "/GRAPH") */
int vp_abbrev(const char *a, const char *word, size_t min);

/* 1023 -> "1023B", 1536 -> "1.5K", 8589934592 -> "8.0G": binary units, at most 5 chars */
void vp_fmt_bytes(char *out, size_t size, double v);
/* seconds -> "12d 04:10", "04:10:22" */
void vp_fmt_uptime(char *out, size_t size, int64_t secs);
/* 0..100 -> "  0%".."100%" (4 characters) */
void vp_fmt_pct(char *out, size_t size, double pct);

/* a cursor position report "ESC [ row ; col R" (DSR 6's answer), maybe with other
   bytes before it; 1 and the numbers, or 0 */
int vp_parse_cpr(const char *s, int *row, int *col);

#endif
