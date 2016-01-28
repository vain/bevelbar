#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct BarWindow
{
    Window win;
    int mx, my, mw, mh;
    Pixmap pm;
    int dw, dh;
    GC gc;
};

static Display *dpy;
static Window root;
static int screen;
static struct BarWindow *bars;
static int numbars;
static char *inputbuf = NULL;
static size_t inputbuf_len = 0;
static XftColor basic_colors[3], *styles = NULL;
static int numstyles;
static XftFont *font;
static int font_height, font_baseline, horiz_margin;
static double font_height_extra = 0.5, seg_spacing, seg_spacing_empty;
static int horiz_padding, verti_padding;
static int horiz_pos, verti_pos;
static int bs_global, bs_inner;

static char *
handle_stdin(char *existing, size_t *fill)
{
    char *buf = NULL;
    size_t len = 0;

    if (existing)
        free(existing);

    *fill = 0;

    for (;;)
    {
        if (*fill == len)
        {
            len += 16;
            buf = realloc(buf, len);
            if (buf == NULL)
            {
                perror(__NAME__": realloc");
                return NULL;
            }
        }

        if (read(STDIN_FILENO, buf + *fill, 1) != 1)
        {
            fprintf(stderr, __NAME__": Expected to read 1 byte, failed\n");
            return NULL;
        }
        (*fill)++;

        if (*fill >= 3 &&
            buf[*fill - 3] == '\n' &&
            buf[*fill - 2] == 'f' &&
            buf[*fill - 1] == '\n')
        {
            return buf;
        }
    }
}

static int
compare_barwindows(const void *a, const void *b)
{
    struct BarWindow *bwa, *bwb;

    bwa = (struct BarWindow *)a;
    bwb = (struct BarWindow *)b;

    if (bwa->mx < bwb->mx || bwa->my + bwa->mh <= bwb->my)
        return -1;

    if (bwa->mx > bwb->mx || bwa->my + bwa->mh > bwb->my)
        return 1;

    return 0;
}

static struct BarWindow *
create_bars(void)
{
    int c, i;
    XRRCrtcInfo *ci;
    XRRScreenResources *sr;
    struct BarWindow *bars;
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ExposureMask,
    };

    sr = XRRGetScreenResources(dpy, root);
    if (sr->ncrtc <= 0)
    {
        fprintf(stderr, __NAME__": No XRandR screens found\n");
        return NULL;
    }

    numbars = 0;
    for (c = 0; c < sr->ncrtc; c++)
    {
        ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);
        if (ci == NULL || ci->noutput == 0 || ci->mode == None)
            continue;
        numbars++;
    }

    bars = calloc(numbars, sizeof (struct BarWindow));
    if (bars == NULL)
    {
        fprintf(stderr, __NAME__": Could not allocate memory for pointer "
                "bars\n");
        return NULL;
    }

    i = 0;
    for (c = 0; c < sr->ncrtc; c++)
    {
        ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);
        if (ci == NULL || ci->noutput == 0 || ci->mode == None)
            continue;

        bars[i].mx = ci->x;
        bars[i].my = ci->y;
        bars[i].mw = ci->width;
        bars[i].mh = ci->height;
        i++;
    }

    qsort(bars, numbars, sizeof (struct BarWindow), compare_barwindows);

    for (i = 0; i < numbars; i++)
    {
        bars[i].win = XCreateWindow(
                dpy, root, -10, -10, 5, 5, 0,
                DefaultDepth(dpy, screen),
                CopyFromParent, DefaultVisual(dpy, screen),
                CWOverrideRedirect | CWBackPixmap | CWEventMask,
                &wa
        );
        XMapRaised(dpy, bars[i].win);
    }

    return bars;
}

static void
draw_init_pixmaps(void)
{
    int i;

    for (i = 0; i < numbars; i++)
    {
        if (bars[i].pm == 0)
        {
            bars[i].pm = XCreatePixmap(dpy, root, bars[i].mw, bars[i].mh,
                                       DefaultDepth(dpy, screen));
            bars[i].gc = XCreateGC(dpy, root, 0, NULL);
        }

        XSetForeground(dpy, bars[i].gc, basic_colors[0].pixel);
        XFillRectangle(dpy, bars[i].pm, bars[i].gc, 0, 0, bars[i].mw, bars[i].mh);

        /* Make room for global bevel border (on the left) */
        bars[i].dw = bs_global;

        /* Make room for actual font + borders */
        bars[i].dh = 2 * bs_global + 2 * bs_inner + font_height;
    }
}

static void
draw_show(void)
{
    int i, x, y;

    if (inputbuf_len <= 0)
        return;

    for (i = 0; i < numbars; i++)
    {
        XCopyArea(dpy, bars[i].pm, bars[i].win, bars[i].gc, 0, 0,
                  bars[i].dw, bars[i].dh, 0, 0);

        if (horiz_pos == -1)
            x = bars[i].mx + horiz_padding;
        else if (horiz_pos == 0)
            x = bars[i].mx + 0.5 * (bars[i].mw - bars[i].dw);
        else
            x = bars[i].mx + bars[i].mw - bars[i].dw - horiz_padding;

        if (verti_pos == -1)
            y = bars[i].my + verti_padding;
        else
            y = bars[i].my + bars[i].mh - bars[i].dh - verti_padding;

        XMoveResizeWindow(dpy, bars[i].win, x, y, bars[i].dw, bars[i].dh);
    }
}

static void
draw_empty(int monitor)
{
    int i;

    /* An "empty segment" just shifts the offset for the next segment */

    for (i = 0; i < numbars; i++)
        if (i == monitor || monitor == -1)
            bars[i].dw += (int)(seg_spacing_empty * font_height);
}

static void
draw_discard_last_intersegment_spacing(int monitor)
{
    int i;

    for (i = 0; i < numbars; i++)
        if (i == monitor || monitor == -1)
            bars[i].dw -= (int)(seg_spacing * font_height);
}

static void
draw_text(int monitor, int style, size_t from, size_t len)
{
    int i, j, w;
    XftDraw *xd;
    XGlyphInfo ext;

    for (i = 0; i < numbars; i++)
    {
        if (i == monitor || monitor == -1)
        {
            /* Get width of the rendered text. Add a little margin on
             * both sides. */
            XftTextExtentsUtf8(dpy, font, (XftChar8 *)&inputbuf[from], len, &ext);
            w = horiz_margin + ext.xOff + horiz_margin;

            /* Background fill */
            XSetForeground(dpy, bars[i].gc, styles[style * 4].pixel);
            XFillRectangle(dpy, bars[i].pm, bars[i].gc,
                           bars[i].dw, bs_global,
                           w + 2 * bs_inner, font_height + 2 * bs_inner);

            /* The text itself */
            xd = XftDrawCreate(dpy, bars[i].pm, DefaultVisual(dpy, screen),
                               DefaultColormap(dpy, screen));
            XftDrawStringUtf8(xd, &styles[style * 4 + 1], font,
                              bars[i].dw + bs_inner + horiz_margin,
                              font_baseline + bs_global + bs_inner,
                              (XftChar8 *)&inputbuf[from], len);
            XftDrawDestroy(xd);

            /* Dark bevel, parts of this will be overdrawn by the bright
             * bevel border (for the sake of simplicity) */
            XSetForeground(dpy, bars[i].gc, styles[style * 4 + 3].pixel);
            XFillRectangle(dpy, bars[i].pm, bars[i].gc,
                           bars[i].dw, bs_global + bs_inner + font_height,
                           w + 2 * bs_inner, bs_inner);
            XFillRectangle(dpy, bars[i].pm, bars[i].gc,
                           bars[i].dw + bs_inner + w, bs_global,
                           bs_inner, font_height + bs_inner);

            /* Bright bevel */
            XSetForeground(dpy, bars[i].gc, styles[style * 4 + 2].pixel);
            for (j = 0; j < bs_inner; j++)
            {
                XDrawLine(dpy, bars[i].pm, bars[i].gc,
                          bars[i].dw + j, bs_global,
                          bars[i].dw + j, bs_global + 2 * bs_inner + font_height
                                          - 1 - j);
                XDrawLine(dpy, bars[i].pm, bars[i].gc,
                          bars[i].dw, bs_global + j,
                          bars[i].dw + w + 2 * bs_inner - 1 - j, bs_global + j);
            }

            bars[i].dw += w + 2 * bs_inner;

            /* Inter-segment spacing. Note that this might get
             * subtracted later on if this was the very last segment to
             * draw. */
            bars[i].dw += (int)(seg_spacing * font_height);
        }
    }
}

static void
draw_global_border(void)
{
    int i, j;

    for (i = 0; i < numbars; i++)
    {
        /* Dark bevel, parts of this will be overdrawn by the bright
         * bevel border (for the sake of simplicity) */
        XSetForeground(dpy, bars[i].gc, basic_colors[2].pixel);
        XFillRectangle(dpy, bars[i].pm, bars[i].gc,
                       0, bars[i].dh - bs_global,
                       bars[i].dw, bs_global);
        XFillRectangle(dpy, bars[i].pm, bars[i].gc,
                       bars[i].dw, 0,
                       bs_global, bars[i].dh);

        /* Bright bevel */
        XSetForeground(dpy, bars[i].gc, basic_colors[1].pixel);
        for (j = 0; j < bs_global; j++)
        {
            XDrawLine(dpy, bars[i].pm, bars[i].gc, j, 0, j, bars[i].dh - 1 - j);
            XDrawLine(dpy, bars[i].pm, bars[i].gc,
                      0, j,
                      bars[i].dw + bs_global - 1 - j, j);
        }

        bars[i].dw += bs_global;
    }
}

static void
draw_everything(void)
{
    size_t i = 0, start, len;
    int monitor, style;
    int state = 0;

    draw_init_pixmaps();

    /* Here be dragons */

    while (i < inputbuf_len)
    {
        switch (state)
        {
            case 0:
                if (inputbuf[i] == 'f')
                {
                    /* End of input reached, jump over following \n */
                    i++;
                }
                else if (inputbuf[i] == 'a')
                {
                    /* We are to draw on all monitors, jump over
                     * following \n */
                    monitor = -1;
                    state = 1;
                    i++;
                }
                else
                {
                    monitor = inputbuf[i] - '0';
                    if (monitor < 0 || monitor >= numbars)
                    {
                        fprintf(stderr, __NAME__": Input is garbage, aborting\n");
                        return;
                    }
                    /* We're on a valid monitor, jump over following \n */
                    state = 1;
                    i++;
                }
                break;

            case 1:
                if (inputbuf[i] == 'e')
                {
                    /* We're finished with this monitor */
                    draw_discard_last_intersegment_spacing(monitor);

                    /* Go back to: Check for next monitor or end of
                     * input. */
                    state = 0;
                    i++;
                }
                else if (inputbuf[i] == '-')
                {
                    draw_empty(monitor);
                    i++;
                }
                else
                {
                    /* Styled input follows. Stay at the current
                     * position (i-- cancels out with i++ below) but
                     * switch to state 2. */
                    state = 2;
                    i--;
                }
                break;

            case 2:
                style = inputbuf[i] - '0';
                if (style < 0 || style >= numstyles)
                {
                    fprintf(stderr, __NAME__": Input is garbage, aborting\n");
                    return;
                }
                start = i + 1;
                len = 0;
                state = 3;
                break;

            case 3:
                if (inputbuf[i] == '\n')
                {
                    draw_text(monitor, style, start, len);
                    state = 1;
                }
                else
                    len++;
                break;
        }

        i++;
    }

    draw_global_border();
    draw_show();
}

static bool
evaulate_args(int argc, char **argv)
{
    int i, b;

    if (argc < 11)
    {
        fprintf(stderr, __NAME__": No basic color/font arguments found\n");
        return false;
    }

    if (strncmp(argv[1], "left", strlen("left")) == 0)
        horiz_pos = -1;
    else if (strncmp(argv[1], "center", strlen("center")) == 0)
        horiz_pos = 0;
    else
        horiz_pos = 1;

    if (strncmp(argv[2], "top", strlen("top")) == 0)
        verti_pos = -1;
    else
        verti_pos = 1;

    horiz_padding = atoi(argv[3]);
    verti_padding = atoi(argv[4]);

    bs_global = atoi(argv[5]);
    bs_inner = atoi(argv[6]);

    seg_spacing = atof(argv[7]);
    seg_spacing_empty = atof(argv[8]);

    font = XftFontOpenName(dpy, screen, argv[9]);
    if (!font)
    {
        fprintf(stderr, __NAME__": Cannot open font '%s'\n", argv[9]);
        return false;
    }

    /* http://lists.schmorp.de/pipermail/rxvt-unicode/2012q4/001674.html */
    font_height = MAX(font->ascent + font->descent, font->height);

    /* See common baseline definition */
    font_baseline = font_height - font->descent;

    /* We don't want to use the exact height but add a little margin.
     * Similarly, in x direction, there shall be some kind of margin. */
    font_baseline += (int)(0.5 * font_height_extra * font_height);
    font_height += (int)(font_height_extra * font_height);
    horiz_margin = 0.25 * font_height;

    for (b = 0, i = 10; i <= 12; i++, b++)
    {
        if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                               DefaultColormap(dpy, screen), argv[i],
                               &basic_colors[b]))
        {
            fprintf(stderr, __NAME__": Cannot load color '%s'\n", argv[i]);
            return false;
        }
    }

    numstyles = (argc - 13) / 4;
    if (numstyles > 0)
    {
        styles = calloc(numstyles, sizeof (XftColor) * 4);
        if (styles == NULL)
        {
            fprintf(stderr, __NAME__": Calloc for styles failed\n");
            return false;
        }

        for (b = 0, i = 13; i < argc; i++, b++)
        {
            if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                                   DefaultColormap(dpy, screen), argv[i],
                                   &styles[b]))
            {
                fprintf(stderr, __NAME__": Cannot load color '%s'\n", argv[i]);
                return false;
            }
        }
    }

    return true;
}

int
main(int argc, char **argv)
{
    XEvent ev;
    fd_set fds;
    int xfd;

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        fprintf(stderr, __NAME__": Cannot open display\n");
        exit(EXIT_FAILURE);
    }

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    bars = create_bars();
    if (bars == NULL)
        exit(EXIT_FAILURE);

    if (!evaulate_args(argc, argv))
        exit(EXIT_FAILURE);

    /* The xlib docs say: On a POSIX system, the connection number is
     * the file descriptor associated with the connection. */
    xfd = ConnectionNumber(dpy);

    XSync(dpy, False);
    for (;;)
    {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(xfd, &fds);

        if (select(xfd + 1, &fds, NULL, NULL, NULL) == -1)
        {
            perror(__NAME__": select() returned with error");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(STDIN_FILENO, &fds))
        {
            if ((inputbuf = handle_stdin(inputbuf, &inputbuf_len)) == NULL)
                exit(EXIT_FAILURE);

            draw_everything();
        }

        while (XPending(dpy))
        {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose)
                draw_everything();
        }
    }

    exit(EXIT_SUCCESS);
}
