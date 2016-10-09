#include <arpa/inet.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define NUM_STYLES 10
#define PER_STYLE 4

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
static XftColor basic_colors[3] = {0}, styles[NUM_STYLES * PER_STYLE] = {0};
static XftFont *font;
static int font_height, font_baseline, font_horiz_margin;
static double font_height_extra = 0.5;
static int horiz_margin, verti_margin;
static int horiz_pos, verti_pos;
static int bs_global, bs_inner;
static int seg_margin, seg_size_empty;

static char *
handle_stdin(size_t *fill)
{
    char *buf = NULL;
    size_t len = 0, maxlen, chunk;

    /* Allocate memory in chunks of 'chunk' bytes. Do not allocate more
     * than 'maxlen' bytes, tops. 65535 is the minimum value for
     * SIZE_MAX and it's more than enough for our purposes (this file
     * right here doesn't even hit 32k). We make 'maxlen' a little
     * smaller, so 'len' won't overflow even if SIZE_MAX of this system
     * actually is 65535. */
    chunk = 64;
    maxlen = 65535 - 2 * chunk;

    /* Note: We never free(buf) when returning from this function -- not
     * even if there were errors. That's because if this function
     * returns NULL, then the whole program will exit. */

    *fill = 0;

    for (;;)
    {
        if (*fill == len)
        {
            len += chunk;
            if (len >= maxlen)
            {
                fprintf(stderr, __NAME__": handle_stdin(): Buffer size: "
                        "maxlen exceeded, aborting\n");
                return NULL;
            }

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

bool
create_bars_is_duplicate(XRRCrtcInfo *ci, bool *chosen, XRRScreenResources *sr)
{
    XRRCrtcInfo *o;
    int i;

    for (i = 0; i < sr->ncrtc; i++)
    {
        if (chosen[i])
        {
            o = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i]);
            if (o->x == ci->x && o->y == ci->y &&
                o->width == ci->width && o->height == ci->height)
                return true;
        }
    }

    return false;
}

static struct BarWindow *
create_bars(void)
{
    int c, i;
    bool *chosen = NULL;
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
    chosen = calloc(sr->ncrtc, sizeof (bool));
    if (chosen == NULL)
    {
        fprintf(stderr, __NAME__": Could not allocate memory for pointer "
                "chosen\n");
        return NULL;
    }

    for (c = 0; c < sr->ncrtc; c++)
    {
        ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);
        if (ci == NULL || ci->noutput == 0 || ci->mode == None)
            continue;

        if (create_bars_is_duplicate(ci, chosen, sr))
            continue;

        chosen[c] = true;
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
        if (chosen[c])
        {
            ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[c]);

            bars[i].mx = ci->x;
            bars[i].my = ci->y;
            bars[i].mw = ci->width;
            bars[i].mh = ci->height;

            i++;
        }
    }
    free(chosen);

    qsort(bars, numbars, sizeof (struct BarWindow), compare_barwindows);

    for (i = 0; i < numbars; i++)
    {
        /* The initial window position doesn't matter, so we move it off
         * screen. Only when the user feeds us input on STDIN, we can
         * move the windows to their correct places. */
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

        /* Apply first horizontal inter-segment spacing */
        bars[i].dw += seg_margin;

        /* Make room for actual font + borders + segment margin */
        bars[i].dh = 2 * bs_global + 2 * bs_inner + font_height + 2 * seg_margin;
    }
}

static void
draw_show(void)
{
    int i, x, y, w;

    if (inputbuf_len <= 0)
        return;

    for (i = 0; i < numbars; i++)
    {
        /* Confine the bar to this monitor (minus margin). We don't do
         * this in Y direction because we don't expect the user to use a
         * font size that covers the whole screen. */
        w = bars[i].dw;
        w = MIN(bars[i].mw - 2 * horiz_margin, w);

        if (horiz_pos == -1)
            x = bars[i].mx + horiz_margin;
        else if (horiz_pos == 0)
            x = bars[i].mx + 0.5 * (bars[i].mw - w);
        else
            x = bars[i].mx + bars[i].mw - w - horiz_margin;

        if (verti_pos == -1)
            y = bars[i].my + verti_margin;
        else
            y = bars[i].my + bars[i].mh - bars[i].dh - verti_margin;

        XMoveResizeWindow(dpy, bars[i].win, x, y, w, bars[i].dh);

        XCopyArea(dpy, bars[i].pm, bars[i].win, bars[i].gc, 0, 0,
                  w, bars[i].dh, 0, 0);
    }
}

static void
draw_empty(int monitor)
{
    int i;

    /* An "empty segment" just shifts the offset for the next segment */

    for (i = 0; i < numbars; i++)
        if (i == monitor || monitor == -1)
            bars[i].dw += seg_size_empty;
}

static void
draw_inner_border(int monitor, int style, int width)
{
    int i;

    /* Dark bevel, parts of this will be overdrawn by the bright
     * bevel border (for the sake of simplicity) */
    XSetForeground(dpy, bars[monitor].gc, styles[style * PER_STYLE + 3].pixel);
    XFillRectangle(dpy, bars[monitor].pm, bars[monitor].gc,
                   bars[monitor].dw,
                   bs_global + bs_inner + font_height + seg_margin,
                   width + 2 * bs_inner, bs_inner);
    XFillRectangle(dpy, bars[monitor].pm, bars[monitor].gc,
                   bars[monitor].dw + bs_inner + width,
                   bs_global + seg_margin,
                   bs_inner, font_height + bs_inner);

    /* Bright bevel */
    XSetForeground(dpy, bars[monitor].gc, styles[style * PER_STYLE + 2].pixel);
    for (i = 0; i < bs_inner; i++)
    {
        XDrawLine(dpy, bars[monitor].pm, bars[monitor].gc,
                  bars[monitor].dw + i, bs_global + seg_margin,
                  bars[monitor].dw + i, bs_global + seg_margin
                                  + 2 * bs_inner + font_height - 1 - i);
        XDrawLine(dpy, bars[monitor].pm, bars[monitor].gc,
                  bars[monitor].dw, bs_global + seg_margin + i,
                  bars[monitor].dw + width + 2 * bs_inner - 1 - i,
                  bs_global + seg_margin + i);
    }

    bars[monitor].dw += width + 2 * bs_inner;
    bars[monitor].dw += seg_margin;
}

static void
draw_image(int monitor, int style, size_t from, size_t len)
{
    int i;
    char *path = NULL;
    FILE *fp = NULL;
    uint32_t hdr[4], width, height, *ximg_data = NULL, x, y;
    uint16_t *ffimg = NULL;
    XImage *ximg;

    path = calloc(len + 1, sizeof (char));
    if (!path)
    {
        fprintf(stderr, __NAME__": Could not allocate memory for image path\n");
        goto cleanout;
    }
    memmove(path, inputbuf + from, len);

    fp = fopen(path, "r");
    if (!fp)
    {
        fprintf(stderr, __NAME__": Could not open image file '%s'\n", path);
        goto cleanout;
    }

    if (fread(hdr, sizeof (uint32_t), 4, fp) != 4)
    {
        fprintf(stderr, __NAME__": Could not read farbfeld header from '%s'\n",
                path);
        goto cleanout;
    }

    if (memcmp("farbfeld", hdr, (sizeof "farbfeld") - 1) != 0)
    {
        fprintf(stderr, __NAME__": Magic number is not 'farbfeld', path '%s'\n",
                path);
        goto cleanout;
    }

    width = ntohl(hdr[2]);
    height = ntohl(hdr[3]);

    ffimg = calloc(width * height * 4, sizeof (uint16_t));
    if (!ffimg)
    {
        fprintf(stderr, __NAME__": Could not allocate memory, ffimg for '%s'\n",
                path);
        goto cleanout;
    }

    ximg_data = calloc(width * height, sizeof (uint32_t));
    if (!ximg_data)
    {
        fprintf(stderr, __NAME__": Could not allocate memory, ximg_data for '%s'\n",
                path);
        goto cleanout;
    }

    if (fread(ffimg, width * height * 4 * sizeof (uint16_t), 1, fp) != 1)
    {
        fprintf(stderr, __NAME__": Unexpected EOF when reading '%s'\n", path);
        goto cleanout;
    }

    /* Convert the farbfeld image to something XCreateImage can
     * understand. */
    for (y = 0; y < height; y++)
    {
        for (x = 0; x < width; x++)
        {
            ximg_data[y * width + x] =
                ((ntohs(ffimg[(y * width + x) * 4    ]) / 256) << 16) |
                ((ntohs(ffimg[(y * width + x) * 4 + 1]) / 256) << 8) |
                 (ntohs(ffimg[(y * width + x) * 4 + 2]) / 256);
        }
    }

    ximg = XCreateImage(dpy, DefaultVisual(dpy, screen), 24, ZPixmap, 0,
                        (char *)ximg_data, width, height, 32, 0);

    for (i = 0; i < numbars; i++)
    {
        if (i == monitor || monitor == -1)
        {
            XPutImage(dpy, bars[i].pm, bars[i].gc, ximg,
                      0, 0,
                      bars[i].dw + bs_inner, bs_global + bs_inner + seg_margin,
                      width, height);

            draw_inner_border(i, style, width);
        }
    }

    /* Note: XDestroyImage() also frees ximg_data, so we reset it to
     * NULL to avoid a double-free in cleanout. */
    XDestroyImage(ximg);
    ximg_data = NULL;

cleanout:
    if (ximg_data)
        free(ximg_data);

    if (ffimg)
        free(ffimg);

    if (fp)
        fclose(fp);

    if (path)
        free(path);
}

static void
draw_text(int monitor, int style, size_t from, size_t len)
{
    int i, w;
    XftDraw *xd;
    XGlyphInfo ext;

    for (i = 0; i < numbars; i++)
    {
        if (i == monitor || monitor == -1)
        {
            /* Get width of the rendered text. Add a little margin on
             * both sides. */
            XftTextExtentsUtf8(dpy, font, (XftChar8 *)&inputbuf[from], len, &ext);
            w = font_horiz_margin + ext.xOff + font_horiz_margin;

            /* Background fill */
            XSetForeground(dpy, bars[i].gc, styles[style * PER_STYLE].pixel);
            XFillRectangle(dpy, bars[i].pm, bars[i].gc,
                           bars[i].dw, bs_global + seg_margin,
                           w + 2 * bs_inner, font_height + 2 * bs_inner);

            /* The text itself */
            xd = XftDrawCreate(dpy, bars[i].pm, DefaultVisual(dpy, screen),
                               DefaultColormap(dpy, screen));
            XftDrawStringUtf8(xd, &styles[style * PER_STYLE + 1], font,
                              bars[i].dw + bs_inner + font_horiz_margin,
                              font_baseline + bs_global + bs_inner + seg_margin,
                              (XftChar8 *)&inputbuf[from], len);
            XftDrawDestroy(xd);

            draw_inner_border(i, style, w);
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
parse_input_and_draw(void)
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
                        fprintf(stderr, __NAME__": Invalid monitor '%c', aborting\n",
                                inputbuf[i]);
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
                    /* We're finished with this monitor. Go back to:
                     * Check for next monitor or end of input. */
                    state = 0;
                    i++;
                }
                else if (inputbuf[i] == '-')
                {
                    draw_empty(monitor);
                    i++;
                }
                else if (inputbuf[i] == 'i')
                {
                    /* "i" means "render an image file". What follows
                     * after the "i" is a style index because the image
                     * still needs an inner bevel. After that index the
                     * path to the file follows. */
                    state = 4;
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
                if (style < 0 || style >= NUM_STYLES)
                {
                    fprintf(stderr, __NAME__": Invalid style, aborting\n");
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

            case 4:
                style = inputbuf[i] - '0';
                if (style < 0 || style >= NUM_STYLES)
                {
                    fprintf(stderr, __NAME__": Invalid style, aborting\n");
                    return;
                }
                start = i + 1;
                len = 0;
                state = 5;
                break;

            case 5:
                if (inputbuf[i] == '\n')
                {
                    draw_image(monitor, style, start, len);
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
evaluate_args(int argc, char **argv)
{
    int i, j;
    int opt, style_selector = -1, color_selector = -1;
    char *horiz_pos_str, *verti_pos_str, *font_str, *basic_colors_str[3];
    char *styles_str[NUM_STYLES][PER_STYLE] = {
        { "#cbaa94", "#000000", "#d6d6d6", "#535353" },
        { "#a68b79", "#000000", "#535353", "#d6d6d6" },
        { "#a0a0a0", "#000000", "#d6d6d6", "#535353" },
        { "#828282", "#000000", "#535353", "#d6d6d6" },
        { "#ff0000", "#ffffff", "#d6d6d6", "#535353" },
        { "#ff0000", "#ffffff", "#535353", "#d6d6d6" },
    };

    /* Default values (see above for default values of styles_str) */
    horiz_pos_str = "center";
    verti_pos_str = "bottom";
    horiz_margin = 5;
    verti_margin = 5;
    bs_global = 2;
    bs_inner = 2;
    seg_margin = 1;
    seg_size_empty = 7;
    font_str = "fixed:pixelsize=13:style=bold";
    basic_colors_str[0] = "#a0a0a0";
    basic_colors_str[1] = "#d6d6d6";
    basic_colors_str[2] = "#535353";

    while ((opt = getopt(argc, argv, "h:v:H:V:b:B:m:e:f:p:o:O:s:c:")) != -1)
    {
        switch (opt)
        {
            case 'h': horiz_pos_str = optarg; break;
            case 'v': verti_pos_str = optarg; break;
            case 'H': horiz_margin = atoi(optarg); break;
            case 'V': verti_margin = atoi(optarg); break;
            case 'b': bs_global = atoi(optarg); break;
            case 'B': bs_inner = atoi(optarg); break;
            case 'm': seg_margin = atoi(optarg); break;
            case 'e': seg_size_empty = atoi(optarg); break;
            case 'f': font_str = optarg; break;
            case 'p': basic_colors_str[0] = optarg; break;
            case 'o': basic_colors_str[1] = optarg; break;
            case 'O': basic_colors_str[2] = optarg; break;
            case 's':
                color_selector = 0;
                style_selector = atoi(optarg);
                if (style_selector < 0)
                    style_selector = -1;
                if (style_selector >= NUM_STYLES)
                    style_selector = -1;
                if (style_selector == -1)
                    fprintf(stderr, __NAME__": Warning, invalid style selector\n");
                break;
            case 'c':
                if (style_selector != -1 && color_selector != -1)
                {
                    styles_str[style_selector][color_selector] = optarg;
                    color_selector++;
                    if (color_selector >= PER_STYLE)
                        color_selector = -1;
                }
                else
                    fprintf(stderr, __NAME__": Warning, cannot set color on "
                            "the basis of an invalid style/color selector "
                            "(too many colors for one style?)\n");
                break;
        }
    }

    if (strncmp(horiz_pos_str, "left", strlen("left")) == 0)
        horiz_pos = -1;
    else if (strncmp(horiz_pos_str, "center", strlen("center")) == 0)
        horiz_pos = 0;
    else
        horiz_pos = 1;

    if (strncmp(verti_pos_str, "top", strlen("top")) == 0)
        verti_pos = -1;
    else
        verti_pos = 1;

    font = XftFontOpenName(dpy, screen, font_str);
    if (!font)
    {
        fprintf(stderr, __NAME__": Cannot open font '%s'\n", font_str);
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
    font_horiz_margin = 0.25 * font_height;

    for (i = 0; i < 3; i++)
    {
        if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                               DefaultColormap(dpy, screen), basic_colors_str[i],
                               &basic_colors[i]))
        {
            fprintf(stderr, __NAME__": Cannot load color '%s'\n",
                    basic_colors_str[i]);
            return false;
        }
    }

    for (i = 0; i < NUM_STYLES; i++)
    {
        if (styles_str[i][0] != NULL)
        {
            for (j = 0; j < PER_STYLE; j++)
            {
                if (styles_str[i][j] == NULL)
                {
                    fprintf(stderr, __NAME__": Expected another color in style "
                            "%d\n", i);
                    return false;
                }

                if (!XftColorAllocName(dpy, DefaultVisual(dpy, screen),
                                       DefaultColormap(dpy, screen),
                                       styles_str[i][j],
                                       &styles[i * PER_STYLE + j]))
                {
                    fprintf(stderr, __NAME__": Cannot load color '%s'\n",
                            styles_str[i][j]);
                    return false;
                }
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

    if (!evaluate_args(argc, argv))
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
            if (inputbuf)
                free(inputbuf);

            if ((inputbuf = handle_stdin(&inputbuf_len)) == NULL)
                exit(EXIT_FAILURE);

            parse_input_and_draw();
        }

        while (XPending(dpy))
        {
            XNextEvent(dpy, &ev);
            if (ev.type == Expose)
                parse_input_and_draw();
        }
    }

    exit(EXIT_SUCCESS);
}
