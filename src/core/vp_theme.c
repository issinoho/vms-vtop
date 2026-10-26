/* vp_theme.c - VTOP's own colours, in btop's spirit (D7). No I/O. */
#include "vp_theme.h"

static vp_rgb rgb(int r, int g, int b)
{
    vp_rgb c;
    c.r = (unsigned char) r; c.g = (unsigned char) g; c.b = (unsigned char) b;
    return c;
}

static vp_rgb mix(vp_rgb a, vp_rgb b, double f)
{
    return rgb((int) (a.r + (b.r - a.r) * f + 0.5), (int) (a.g + (b.g - a.g) * f + 0.5),
               (int) (a.b + (b.b - a.b) * f + 0.5));
}

/* three-stop gradient */
static void gradient(vp_rgb *out, vp_rgb lo, vp_rgb mid, vp_rgb hi)
{
    int i;
    for (i = 0; i < VP_GRAD; i++) {
        double f = (double) i / (VP_GRAD - 1);
        out[i] = f < 0.5 ? mix(lo, mid, f * 2) : mix(mid, hi, (f - 0.5) * 2);
    }
}

void vp_theme_init(vp_rgb pal[VP_NPAL])
{
    int i;
    for (i = 0; i < VP_NPAL; i++) pal[i] = rgb(200, 200, 200);
    pal[VP_K_DEFAULT] = rgb(0, 0, 0);
    pal[VP_K_TEXT] = rgb(204, 204, 204);
    pal[VP_K_DIM] = rgb(110, 110, 110);
    pal[VP_K_TITLE] = rgb(238, 238, 238);
    pal[VP_K_HI] = rgb(120, 200, 255);
    pal[VP_K_WARN] = rgb(235, 80, 70);
    pal[VP_K_CPU_BOX] = rgb(90, 160, 100);
    pal[VP_K_MEM_BOX] = rgb(190, 150, 90);
    pal[VP_K_NET_BOX] = rgb(160, 110, 180);
    pal[VP_K_PROC_BOX] = rgb(90, 140, 190);
    pal[VP_K_METER_OFF] = rgb(60, 60, 60);
    pal[VP_K_RX] = rgb(170, 120, 230);
    pal[VP_K_TX] = rgb(230, 120, 170);
    pal[VP_K_STATUS_BG] = rgb(40, 40, 50);
    gradient(&pal[VP_K_GRAD], rgb(80, 200, 90), rgb(225, 205, 60), rgb(230, 70, 60));
    gradient(&pal[VP_K_NETGRAD], rgb(80, 90, 200), rgb(160, 100, 220), rgb(230, 110, 170));
}

static int step(double f)
{
    int i;
    if (!(f > 0)) return 0;     /* also NaN */
    if (f >= 1) return VP_GRAD - 1;
    i = (int) (f * (VP_GRAD - 1) + 0.5);
    return i;
}

int vp_grad(double f) { return VP_K_GRAD + step(f); }
int vp_netgrad(double f) { return VP_K_NETGRAD + step(f); }
