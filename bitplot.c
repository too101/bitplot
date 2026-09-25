/*
 * bitplot.c — BitPlot for Linux (X11)
 *
 * View any file as a bitmap of its bits: 1 byte = 8 points (MSB leftmost).
 * Bytes fill downward; when a column reaches the bottom of the window,
 * plotting continues at the top of the next byte-column (8 points plus a
 * 1-point gap), like text flowing down newspaper columns.
 *
 * Hover a point and the header shows the file address (decimal / hex) of the
 * byte that point belongs to. The header always shows the address of the
 * top-left visible byte.
 *
 * Build:  gcc -O2 -Wall -o bitplot bitplot.c -lX11        (or just: make)
 * Needs:  libX11 at runtime (preinstalled on every Linux desktop; on Wayland
 *         it runs through XWayland). Only libx11-dev is needed to build.
 *
 * Usage:  ./bitplot [file]     (or press O, type a path, press Enter)
 * Keys:   O = open file, + / - = zoom, Left / Right or wheel = scroll,
 *         Ctrl+wheel = zoom, Home / End = start / end, Esc or q = quit
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;

static Display *dpy;
static int scr;
static Window win;
static Pixmap back;
static GC gc_win, gc_back;
static int win_w = 900, win_h = 620;
static int have_back = 0;

static u8 *data = NULL;
static long long len = 0;
static const char *fname = NULL;

static int cell = 6;   /* Linux default: 3 zoom levels below the 12 px Windows default */
static const int BITS = 8;
static const int GAP = 1;
static const int MIN_CELL = 4, MAX_CELL = 40;
static const int SCROLL_H = 12;

static XFontStruct *font;
static unsigned long col_white, col_black, col_unset, col_hi, col_red, col_gray;

static long long scroll_x = 0;
static long long hover_byte = -1;
static int hover_bit = -1;
static int dragging = 0;
static int lsb_first = 0;   /* 0 = MSB leftmost (normal), 1 = mirrored (LSB-first) font */

static char path_buf[4096];
static int path_len = 0;
static int typing = 0;
static char notice[300] = "";

static int header_h(void);

static long long rows(void)
{
    long long r = (win_h - header_h() - SCROLL_H) / cell;
    return r < 1 ? 1 : r;
}
static long long stride(void) { return (BITS + GAP) * (long long)cell; }
static long long cols(void)
{
    if (!data || len == 0) return 0;
    return (len + rows() - 1) / rows();
}
static long long content_w(void) { return cols() * stride(); }
static long long max_scroll(void)
{
    long long m = content_w() - win_w;
    return m > 0 ? m : 0;
}
static void clamp_scroll(void)
{
    long long m = max_scroll();
    if (scroll_x > m) scroll_x = m;
    if (scroll_x < 0) scroll_x = 0;
}
static int header_h(void) { return font->ascent + font->descent + 12; }

static unsigned long alloc_color(const char *name)
{
    XColor c, exact;
    if (!XAllocNamedColor(dpy, DefaultColormap(dpy, scr), name, &c, &exact))
        return BlackPixel(dpy, scr);
    return c.pixel;
}

static int load_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    rewind(f);
    u8 *buf = malloc(sz > 0 ? (size_t)sz : 1);
    if (!buf) { fclose(f); return 0; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz)
        { free(buf); fclose(f); return 0; }
    fclose(f);
    free(data);
    data = buf;
    len = sz;
    {
        const char *base = strrchr(path, '/');
        if (!base) base = strrchr(path, '\\');
        fname = base ? base + 1 : path;   /* show only the file name, not the path */
    }
    hover_byte = -1;
    hover_bit = -1;
    scroll_x = 0;
    clamp_scroll();
    {
        char t[600];
        snprintf(t, sizeof t, "BitPlot - %s (%lld bytes)", fname, len);
        XStoreName(dpy, win, t);
    }
    return 1;
}

static void draw_point(int x, int y, int set)
{
    int pw = cell - 1;
    int us = cell / 4 > 0 ? cell / 4 : 1;
    if (set) {
        XSetForeground(dpy, gc_back, col_black);
        XFillRectangle(dpy, back, gc_back, x, y, pw, pw);
    } else {
        XSetForeground(dpy, gc_back, col_unset);
        XFillRectangle(dpy, back, gc_back, x, y, us, us);
    }
}

static void draw_string_right(Drawable d, int y, const char *s, unsigned long fg)
{
    int tw = XTextWidth(font, s, (int)strlen(s));
    XSetForeground(dpy, gc_back, fg);
    XDrawString(dpy, d, gc_back, win_w - tw - 8, y, s, (int)strlen(s));
}

static void redraw(void)
{
    if (!have_back) return;
    int hh = header_h();
    long long rw = rows(), st = stride(), nc = cols();
    int ty = hh / 2 + (font->ascent - font->descent) / 2;

    XSetForeground(dpy, gc_back, col_white);
    XFillRectangle(dpy, back, gc_back, 0, 0, win_w, win_h);

    if (data && len > 0) {
        int first_col = (int)(scroll_x / st);
        int last_col = (int)((scroll_x + win_w) / st) + 1;
        if (last_col > (int)nc - 1) last_col = (int)nc - 1;
        int bits_w = BITS * cell;

        for (int col = first_col; col <= last_col; col++) {
            long long fb = (long long)col * rw;
            long long lb = len < fb + rw ? len : fb + rw;
            int x0 = (int)((long long)col * st - scroll_x);
            int hov_here = hover_byte >= fb && hover_byte < lb;

            if (hov_here) {
                XSetForeground(dpy, gc_back, col_hi);
                XFillRectangle(dpy, back, gc_back, x0,
                               hh + (int)(hover_byte - fb) * cell, bits_w, cell);
            }
            for (long long b = fb; b < lb; b++) {
                int y = hh + (int)(b - fb) * cell;
                u8 v = data[b];
                for (int bit = 0; bit < BITS; bit++) {
                    int bi = lsb_first ? bit : 7 - bit;
                    draw_point(x0 + bit * cell, y, (v >> bi) & 1);
                }
            }
            if (hov_here && hover_bit >= 0) {
                XSetForeground(dpy, gc_back, col_red);
                XDrawRectangle(dpy, back, gc_back, x0 + hover_bit * cell,
                               hh + (int)(hover_byte - fb) * cell, cell, cell);
            }
        }
    } else {
        const char *msg = data ? "File is empty."
                               : "Run:  bitplot <file>      (or press O, type a path, Enter)";
        int tw = XTextWidth(font, msg, (int)strlen(msg));
        XSetForeground(dpy, gc_back, col_gray);
        XDrawString(dpy, back, gc_back, (win_w - tw) / 2,
                    hh + (win_h - hh - SCROLL_H + font->ascent) / 2, msg, (int)strlen(msg));
        if (notice[0]) {
            int tw2 = XTextWidth(font, notice, (int)strlen(notice));
            XSetForeground(dpy, gc_back, col_red);
            XDrawString(dpy, back, gc_back, (win_w - tw2) / 2,
                        hh + (win_h - hh - SCROLL_H + font->ascent) / 2 + font->ascent + font->descent + 6,
                        notice, (int)strlen(notice));
        }
    }

    /* header */
    XSetForeground(dpy, gc_back, col_white);
    XFillRectangle(dpy, back, gc_back, 0, 0, win_w, hh);
    XSetForeground(dpy, gc_back, col_gray);
    XDrawLine(dpy, back, gc_back, 0, hh - 1, win_w, hh - 1);
    if (typing) {
        char buf[4200];
        snprintf(buf, sizeof buf, "Open: %s_", path_buf);
        XSetForeground(dpy, gc_back, col_black);
        XDrawString(dpy, back, gc_back, 8, ty, buf, (int)strlen(buf));
    } else if (data && len > 0) {
        char buf[96];
        long long addr = hover_byte >= 0 ? hover_byte : (scroll_x / st) * rw;
        int n = snprintf(buf, sizeof buf, "Address: %lld (%llXH)", addr, (unsigned long long)addr);
        if (addr >= 0 && addr < len)
            snprintf(buf + n, sizeof buf - n, "   Data: %02XH", data[addr]);
        XSetForeground(dpy, gc_back, col_black);
        XDrawString(dpy, back, gc_back, 8, ty, buf, (int)strlen(buf));
        char right[64];
        snprintf(right, sizeof right, "[+][-] Zoom   [B] %s", lsb_first ? "LSB" : "MSB");
        draw_string_right(back, ty, right, col_gray);
    } else {
        XSetForeground(dpy, gc_back, col_black);
        XDrawString(dpy, back, gc_back, 8, ty, "BitPlot", 7);
    }

    /* scrollbar */
    long long m = max_scroll();
    if (m > 0) {
        int sy = win_h - SCROLL_H;
        double frac = (double)win_w / (double)content_w();
        if (frac > 1) frac = 1;
        int tw = (int)(win_w * frac);
        if (tw < 20) tw = 20;
        int tx = (int)((double)scroll_x / (double)m * (win_w - tw));
        XSetForeground(dpy, gc_back, col_gray);
        XDrawLine(dpy, back, gc_back, 0, sy + SCROLL_H / 2, win_w, sy + SCROLL_H / 2);
        XSetForeground(dpy, gc_back, col_unset);
        XFillRectangle(dpy, back, gc_back, tx, sy + 1, tw, SCROLL_H - 2);
    }

    XCopyArea(dpy, back, win, gc_win, 0, 0, win_w, win_h, 0, 0);
}

static void make_back(void)
{
    if (have_back) XFreePixmap(dpy, back);
    back = XCreatePixmap(dpy, win, win_w, win_h, DefaultDepth(dpy, scr));
    have_back = 1;
}

static void zoom(int delta)
{
    int nv = cell + delta;
    if (nv < MIN_CELL) nv = MIN_CELL;
    if (nv > MAX_CELL) nv = MAX_CELL;
    if (nv == cell) return;
    cell = nv;
    clamp_scroll();
    redraw();
}

static void update_hover(int mx, int my)
{
    long long nb = -1;
    int nbit = -1;
    if (data && len > 0) {
        int hh = header_h();
        if (my >= hh && my < win_h - SCROLL_H) {
            long long x = (long long)mx + scroll_x;
            long long st = stride();
            long long incol = x % st;
            if (incol < BITS * (long long)cell) {
                long long row = (my - hh) / cell;
                long long rw = rows();
                if (row < rw) {
                    long long idx = (x / st) * rw + row;
                    if (idx < len) { nb = idx; nbit = (int)(incol / cell); }
                }
            }
        }
    }
    if (nb != hover_byte || nbit != hover_bit) {
        hover_byte = nb;
        hover_bit = nbit;
        redraw();
    }
}

static void scrollbar_to(int mx)
{
    long long m = max_scroll();
    if (m <= 0) return;
    double frac = (double)win_w / (double)content_w();
    if (frac > 1) frac = 1;
    int tw = (int)(win_w * frac);
    if (tw < 20) tw = 20;
    int tx = mx - tw / 2;
    if (tx < 0) tx = 0;
    if (tx > win_w - tw) tx = win_w - tw;
    scroll_x = (long long)((double)tx / (double)(win_w - tw) * (double)m);
    clamp_scroll();
    redraw();
}

int main(int argc, char **argv)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "BitPlot: cannot open X display\n");
        return 1;
    }
    scr = DefaultScreen(dpy);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, win_w, win_h, 0,
                              BlackPixel(dpy, scr), WhitePixel(dpy, scr));
    XSelectInput(dpy, win, ExposureMask | PointerMotionMask | ButtonPressMask |
                            ButtonReleaseMask | KeyPressMask | StructureNotifyMask);
    XStoreName(dpy, win, "BitPlot");
    {
        Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(dpy, win, &del, 1);
    }

    font = XLoadQueryFont(dpy, "fixed");
    if (!font) font = XLoadQueryFont(dpy, "-*-*-*-*-*-*-14-*-*-*-*-*-*-*");
    if (!font) {
        fprintf(stderr, "BitPlot: cannot load a font\n");
        return 1;
    }

    gc_win = XCreateGC(dpy, win, 0, NULL);
    gc_back = XCreateGC(dpy, win, 0, NULL);
    col_white = alloc_color("white");
    col_black = alloc_color("black");
    col_unset = alloc_color("gray85");
    col_hi = alloc_color("#FFDAB9");
    col_red = alloc_color("red");
    col_gray = alloc_color("gray40");

    if (argc > 1 && !load_file(argv[1]))
        snprintf(notice, sizeof notice, "Cannot open: %s", argv[1]);

    /* request a maximized initial window (EWMH; minimal WMs may ignore this) */
    {
        Atom wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
        Atom maxv = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        Atom maxh = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        Atom state[2] = { maxv, maxh };
        XChangeProperty(dpy, win, wm_state, XA_ATOM, 32, PropModeReplace,
                        (unsigned char *)state, 2);
    }

    XMapWindow(dpy, win);
    make_back();

    for (;;) {
        XEvent e;
        XNextEvent(dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0) redraw();
            break;
        case ConfigureNotify:
            if (e.xconfigure.width != win_w || e.xconfigure.height != win_h) {
                win_w = e.xconfigure.width;
                win_h = e.xconfigure.height;
                make_back();
                clamp_scroll();
                redraw();
            }
            break;
        case MotionNotify:
            if (dragging) scrollbar_to(e.xmotion.x);
            else update_hover(e.xmotion.x, e.xmotion.y);
            break;
        case ButtonPress:
            if (e.xbutton.button == Button1) {
                if (data && len > 0 && max_scroll() > 0 &&
                    e.xbutton.y >= win_h - SCROLL_H) {
                    dragging = 1;
                    scrollbar_to(e.xbutton.x);
                }
            } else if (e.xbutton.button == Button4) {
                if (e.xbutton.state & ControlMask) zoom(2);
                else { scroll_x -= cell * 3; clamp_scroll(); redraw(); }
            } else if (e.xbutton.button == Button5) {
                if (e.xbutton.state & ControlMask) zoom(-2);
                else { scroll_x += cell * 3; clamp_scroll(); redraw(); }
            }
            break;
        case ButtonRelease:
            if (e.xbutton.button == Button1) dragging = 0;
            break;
        case KeyPress: {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (typing) {
                if (ks == XK_Return) {
                    if (path_len > 0) {
                        path_buf[path_len] = '\0';
                        if (load_file(path_buf)) { typing = 0; path_len = 0; notice[0] = '\0'; }
                        else snprintf(notice, sizeof notice, "Cannot open: %.250s", path_buf);
                    }
                    redraw();
                } else if (ks == XK_Escape) {
                    typing = 0;
                    path_len = 0;
                    redraw();
                } else if (ks == XK_BackSpace) {
                    if (path_len > 0) path_len--;
                    redraw();
                } else {
                    char tmp[8];
                    int n = XLookupString(&e.xkey, tmp, (int)sizeof tmp, NULL, NULL);
                    int changed = 0;
                    for (int i = 0; i < n; i++)
                        if ((unsigned char)tmp[i] >= 32 &&
                            path_len < (int)sizeof path_buf - 1) {
                            path_buf[path_len++] = tmp[i];
                            changed = 1;
                        }
                    if (changed) redraw();
                }
            } else {
                switch (ks) {
                case XK_o: case XK_O:
                    typing = 1;
                    path_len = 0;
                    redraw();
                    break;
                case XK_plus: case XK_equal: case XK_KP_Add:
                    zoom(2);
                    break;
                case XK_minus: case XK_KP_Subtract:
                    zoom(-2);
                    break;
                case XK_Left:
                    scroll_x -= cell * 3;
                    clamp_scroll();
                    redraw();
                    break;
                case XK_Right:
                    scroll_x += cell * 3;
                    clamp_scroll();
                    redraw();
                    break;
                case XK_Home:
                    scroll_x = 0;
                    redraw();
                    break;
                case XK_End:
                    scroll_x = max_scroll();
                    redraw();
                    break;
                case XK_b: case XK_B:
                    lsb_first = !lsb_first;
                    redraw();
                    break;
                case XK_Escape: case XK_q: case XK_Q:
                    goto done;
                }
            }
            break;
        }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == XInternAtom(dpy, "WM_DELETE_WINDOW", False))
                goto done;
            break;
        }
    }
done:
    XFreeGC(dpy, gc_win);
    XFreeGC(dpy, gc_back);
    if (have_back) XFreePixmap(dpy, back);
    if (font) XFreeFont(dpy, font);
    XCloseDisplay(dpy);
    return 0;
}
