/* p_keys.c - read 5 keystrokes with SMG$READ_KEYSTROKE and print each code.
   Driven by PTYDRIVE (probes/p_keys.txt) to see whether arrow-key escape
   sequences arrive as one SMG$K_TRM_* code or as separate characters. */
#include <stdio.h>
#include <smgdef.h>
#include <smg$routines.h>
int main(void)
{
    unsigned int kb = 0, st;
    unsigned short term;
    int i, secs = 10;
    st = smg$create_virtual_keyboard(&kb, 0, 0, 0, 0);
    if (!(st & 1)) { printf("create %08X\n", st); return st; }
    for (i = 0; i < 5; i++) {
        term = 0;
        st = smg$read_keystroke(&kb, &term, 0, &secs, 0, 0, 0);
        printf("KEY %d status %08X term %u%s\n", i, st, term,
               term == SMG$K_TRM_RIGHT ? " (RIGHT)" : term == SMG$K_TRM_LEFT ? " (LEFT)" : "");
    }
    return 1;
}
