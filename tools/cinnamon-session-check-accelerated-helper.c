/* gcc -o cinnamon-session-accelerated `pkg-config --cflags --libs xcomposite gl` -Wall cinnamon-session-is-accelerated.c */

/*
 * Copyright (C) 2010      Novell, Inc.
 * Copyright (C) 2006-2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA  02110-1335  USA
 *
 * Author:
 *   Vincent Untz <vuntz@gnome.org>
 *
 * Most of the code comes from desktop-effects [1], released under GPLv2+.
 * desktop-effects was written by:
 *   Soren Sandmann <sandmann@redhat.com>
 *
 * [1] http://git.fedorahosted.org/git/?p=desktop-effects.git;a=blob_plain;f=desktop-effects.c;hb=HEAD
 */

/*
 * Here's the rationale behind this helper, quoting Owen, in his mail to the
 * release team:
 * (http://mail.gnome.org/archives/release-team/2010-June/msg00079.html)
 *
 * """
 * There are some limits to what we can do here automatically without
 * knowing anything about the driver situation on the system. The basic
 * problem is that there are all sorts of suck:
 *
 *  * No GL at all. This typically only happens if a system is
 *    misconfigured.
 *
 *  * Only software GL. This one is easy to detect. We have code in
 *    the Fedora desktop-effects tool, etc.
 *
 *  * GL that isn't featureful enough. (Tiny texture size limits, no
 *    texture-from-pixmap, etc.) Possible to detect with more work, but
 *    largely a fringe case.
 *
 *  * Buggy GL. This isn't possible to detect. Except for the case where
 *    all GL programs crash. For that reason, we probably don't want
 *    cinnamon-session to directly try and do any GL detection; better to
 *    use a helper binary.
 *
 *  * Horribly slow hardware GL. We could theoretically develop some sort
 *    of benchmark, but it's a tricky area. And how slow is too slow?
 * """
 *
 * Some other tools are doing similar checks:
 *  - desktop-effects (Fedora Config Tool) [1]
 *  - drak3d (Mandriva Config Tool) [2]
 *  - compiz-manager (Compiz wrapper) [3]
 *
 * [1] http://git.fedorahosted.org/git/?p=desktop-effects.git;a=blob_plain;f=desktop-effects.c;hb=HEAD
 * [2] http://svn.mandriva.com/cgi-bin/viewvc.cgi/soft/drak3d/trunk/lib/Xconfig/glx.pm?view=markup
 * [3] http://git.compiz.org/fusion/misc/compiz-manager/tree/compiz-manager
 */

#include "config.h"

/* for strcasestr */
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <regex.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <GL/gl.h>
#include <GL/glx.h>

static int max_texture_size = 0;

static inline void
_print_error (const char *str)
{
        fprintf (stderr, "cinnamon-session-is-accelerated: %s\n", str);
}

static int
_parse_kcmdline (void)
{
        FILE *kcmdline;
        char *line = NULL;
        size_t line_len = 0;
        int ret = -1;

        kcmdline = fopen("/proc/cmdline", "r");
        if (kcmdline == NULL)
                return ret;

        while (getline (&line, &line_len, kcmdline) != -1) {
                const char *arg;
                const char *str;
                int key_len = strlen ("gnome.fallback=");

                if (line == NULL)
                        break;

                /* don't break if we found the argument once: last mention wins */

                str = line;
                do {
                        arg = strstr (str, "gnome.fallback=");
                        str = arg + key_len;

                        if (arg &&
                                        (arg == line || isspace (arg[-1])) && /* gnome.fallback= is really the beginning of an argument */
                                        (isdigit (arg[key_len]))) { /* the first character of the value of this argument is an integer */
                                if ((arg[key_len+1] == '\0' || isspace (arg[key_len+1]))) /* the value of this argument is only one character long */
                                        ret = arg[key_len] - '0';
                                else /* invalid value */
                                        ret = 0xDEAD;

                        }
                } while (arg != NULL);

                free (line);
                line = NULL;
                line_len = 0;
        }

        fclose (kcmdline);

        return ret;
}

static int
_has_composite (Display *display)
{
        int dummy1, dummy2;

        if (XCompositeQueryExtension (display, &dummy1, &dummy2))
                return 0;

        return 1;
}

static int
_is_comment (const char *line)
{
        while (*line && isspace(*line))
                line++;

        if (*line == '#' || *line == '\0')
                return 0;
        else
                return 1;
}

static int
_is_gl_renderer_blacklisted (const char *renderer)
{
        FILE *blacklist;
        char *line = NULL;
        size_t line_len = 0;
        int ret = 1;

        blacklist = fopen(PKGDATADIR "/hardware-compatibility", "r");
        if (blacklist == NULL)
                goto out;

        while (getline (&line, &line_len, blacklist) != -1) {
                int whitelist = 0;
                const char *re_str;
                regex_t re;
                int status;

                if (line == NULL)
                        break;

                /* Drop trailing \n */
                line[strlen(line) - 1] = '\0';

                if (_is_comment (line) == 0) {
                        free (line);
                        line = NULL;
                        continue;
                }

                if (line[0] == '+')
                        whitelist = 1;
                else if (line[0] == '-')
                        whitelist = 0;
                else {
                        _print_error ("Invalid syntax in this line for hardware compatibility:");
                        _print_error (line);
                        free (line);
                        line = NULL;
                        continue;
                }

                re_str = line + 1;

                if (regcomp (&re, re_str, REG_EXTENDED|REG_ICASE|REG_NOSUB) != 0) {
                        _print_error ("Cannot use this regular expression for hardware compatibility:");
                        _print_error (re_str);
                } else {
                        status = regexec (&re, renderer, 0, NULL, 0);
                        regfree(&re);

                        if (status == 0) {
                                if (whitelist)
                                        ret = 0;
                                goto out;
                        }
                }

                free (line);
                line = NULL;
        }

        ret = 0;

out:
        if (line != NULL)
                free (line);

        if (blacklist != NULL)
                fclose (blacklist);

        return ret;
}

static int
_has_hardware_gl (Display *display)
{
        int screen;
        Window root;
        XVisualInfo *visual = NULL;
        GLXContext context = NULL;
        XSetWindowAttributes cwa = { 0 };
        Window window = None;
        const char *renderer;
        int ret = 1;

        int attrlist[] = {
                GLX_RGBA,
                GLX_RED_SIZE, 1,
                GLX_GREEN_SIZE, 1,
                GLX_BLUE_SIZE, 1,
                GLX_DOUBLEBUFFER,
                None
        };

        screen = DefaultScreen (display);
        root = RootWindow (display, screen);

        visual = glXChooseVisual (display, screen, attrlist);
        if (!visual)
                goto out;

        context = glXCreateContext (display, visual, NULL, True);
        if (!context)
                goto out;

        cwa.colormap = XCreateColormap (display, root,
                                        visual->visual, AllocNone);
        cwa.background_pixel = 0;
        cwa.border_pixel = 0;
        window = XCreateWindow (display, root,
                                0, 0, 1, 1, 0,
                                visual->depth, InputOutput, visual->visual,
                                CWColormap | CWBackPixel | CWBorderPixel,
                                &cwa);

        if (!glXMakeCurrent (display, window, context))
                goto out;

        renderer = (const char *) glGetString (GL_RENDERER);
        if (_is_gl_renderer_blacklisted (renderer) != 0)
                goto out;

        /* we need to get the max texture size while we have a context,
         * but we'll check its value later */
        glGetIntegerv (GL_MAX_TEXTURE_SIZE, &max_texture_size);
        if (glGetError() != GL_NO_ERROR)
                max_texture_size = -1;

        ret = 0;

out:
        glXMakeCurrent (display, None, None);
        if (context)
                glXDestroyContext (display, context);
        if (window)
                XDestroyWindow (display, window);
        if (cwa.colormap)
                XFreeColormap (display, cwa.colormap);

        return ret;
}

static int
_has_extension (const char *extension_list,
                const char *extension)
{
        int s = 0, e = 0;
        int ext_len;

        /* Extension_list is one big string, containing extensions
         * separated by spaces. We could use strstr, except that we
         * can't know for sure that there's no extension that starts
         * with the same string... */

        if (!extension_list || extension_list[0] == 0)
                return 1;
        if (!extension || extension[0] == 0)
                return 0;

        ext_len = strlen (extension);

        while (1) {
                if (extension_list[e] != ' ' && extension_list[e] != 0) {
                        e++;
                        continue;
                }

                /* End of a word. Was is the extension we're looking for? */
                if ((e - s) == ext_len &&
                    strncmp (&extension_list[s], extension, ext_len) == 0) {
                        return 0;
                }

                /* was it the end of the string? */
                if (extension_list[e] == 0)
                        break;

                /* skip the space and start looking at the next word */
                e++;
                s = e;
        }

        return 1;
}

static int
_has_texture_from_pixmap (Display *display)
{
        int screen;
        const char *server_extensions;
        const char *client_extensions;
        int ret = 1;

        screen = DefaultScreen (display);

        server_extensions = glXQueryServerString (display, screen,
                                                  GLX_EXTENSIONS);
        if (_has_extension (server_extensions,
                            "GLX_EXT_texture_from_pixmap") != 0)
                goto out;

        client_extensions = glXGetClientString (display, GLX_EXTENSIONS);
        if (_has_extension (client_extensions,
                            "GLX_EXT_texture_from_pixmap") != 0)
                goto out;

        ret = 0;

out:
        return ret;
}

static int
_is_max_texture_size_big_enough (Display *display)
{
        int screen;

        screen = DefaultScreen (display);
        if (max_texture_size < DisplayWidth (display, screen) ||
            max_texture_size < DisplayHeight (display, screen))
                return 1;

        return 0;
}

int
main (int argc, char **argv)
{
        int      kcmdline_parsed;
        Display *display = NULL;
        int      ret = 1;

        kcmdline_parsed = _parse_kcmdline ();
        if (kcmdline_parsed >= 0) {
                if (kcmdline_parsed == 0) {
                        _print_error ("Non-fallback mode forced by kernel command line.");
                        ret = 0;
                        goto out;
                } else if (kcmdline_parsed == 1) {
                        _print_error ("Fallback mode forced by kernel command line.");
                        goto out;
                } else
                        _print_error ("Invalid value for gnome.fallback passed in kernel command line.");
        }

        display = XOpenDisplay (NULL);
        if (!display) {
                _print_error ("No X display.");
                goto out;
        }

        if (_has_composite (display) != 0) {
                _print_error ("No composite extension.");
                goto out;
        }

        if (_has_hardware_gl (display) != 0) {
                _print_error ("No hardware 3D support.");
                goto out;
        }

        if (_has_texture_from_pixmap (display) != 0) {
                _print_error ("No GLX_EXT_texture_from_pixmap support.");
                goto out;
        }

        if (_is_max_texture_size_big_enough (display) != 0) {
                _print_error ("GL_MAX_TEXTURE_SIZE is too small.");
                goto out;
        }

        ret = 0;

out:
        if (display)
                XCloseDisplay (display);

        return ret;
}
