/* vp_theme.h - the palette (D7): what each colour index in a cell means. No I/O. */
#ifndef VP_THEME_H
#define VP_THEME_H

typedef struct { unsigned char r, g, b; } vp_rgb;

enum {
    VP_K_DEFAULT = 0,       /* the terminal's own colour (fg or bg) */
    VP_K_TEXT, VP_K_DIM, VP_K_TITLE, VP_K_HI, VP_K_WARN,
    VP_K_CPU_BOX, VP_K_MEM_BOX, VP_K_NET_BOX, VP_K_PROC_BOX,
    VP_K_METER_OFF,         /* the unfilled part of a meter */
    VP_K_RX, VP_K_TX,
    VP_K_STATUS_BG,
    VP_K_VMS_BOX,
    VP_K_GRAD = 16,         /* VP_GRAD steps, green - yellow - red */
    VP_K_NETGRAD = VP_K_GRAD + 32,  /* VP_GRAD steps for the network graphs */
    VP_NPAL = VP_K_NETGRAD + 32
};
#define VP_GRAD 32

void vp_theme_init(vp_rgb pal[VP_NPAL]);
/* the gradient index for a fraction 0..1 */
int vp_grad(double f);
int vp_netgrad(double f);

#endif
