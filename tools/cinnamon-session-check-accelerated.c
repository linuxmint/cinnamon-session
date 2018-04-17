/* -*- mode:c; c-basic-offset: 8; indent-tabs-mode: nil; -*- */
/* Tool to set the property _CINNAMON_SESSION_ACCELERATED on the root window */
/*
 * Copyright (C) 2011 Red Hat, Inc.
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
 *   Colin Walters <walters@verbum.org>
 */

#include "config.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

/* Wait up to this long for a running check to finish */
#define PROPERTY_CHANGE_TIMEOUT 5000

/* Values used for the _CINNAMON_SESSION_ACCELERATED root window property */
#define NO_ACCEL            0
#define HAVE_ACCEL          1
#define ACCEL_CHECK_RUNNING 2

static Atom is_accelerated_atom;
static gboolean property_changed;

static void
exit_1_message (const char *msg) G_GNUC_NORETURN;

static void
exit_1_message (const char *msg)
{
  g_printerr ("%s", msg);
  exit (1);
}

static gboolean
on_property_notify_timeout (gpointer data)
{
        gtk_main_quit ();
        return FALSE;
}

static GdkFilterReturn
property_notify_filter (GdkXEvent *xevent,
                        GdkEvent  *event,
                        gpointer   data)
{
        XPropertyEvent *ev = xevent;

        if (ev->type == PropertyNotify && ev->atom == is_accelerated_atom) {
                property_changed = TRUE;
                gtk_main_quit ();
        }

        return GDK_FILTER_CONTINUE;
}

static gboolean
wait_for_property_notify (void)
{
        GdkDisplay *display = NULL;
        GdkScreen *screen;
        GdkWindow *root;
        Window rootwin;

        property_changed = FALSE;

        display = gdk_display_get_default ();
        screen = gdk_display_get_default_screen (display);
        root = gdk_screen_get_root_window (screen);
        rootwin = gdk_x11_window_get_xid (root);

        XSelectInput (GDK_DISPLAY_XDISPLAY (display), rootwin, PropertyChangeMask);
        gdk_window_add_filter (root, property_notify_filter, NULL);
        g_timeout_add (PROPERTY_CHANGE_TIMEOUT, on_property_notify_timeout, NULL);

        gtk_main ();

        return property_changed;
}

int
main (int argc, char **argv)
{
        GdkDisplay *display = NULL;
        int estatus;
        char *child_argv[] = { LIBEXECDIR "/cinnamon-session-check-accelerated-helper", NULL };
        Window rootwin;
        glong is_accelerated;
        GError *error = NULL;

        gtk_init (NULL, NULL);

        display = gdk_display_get_default ();
        rootwin = gdk_x11_get_default_root_xwindow ();

        is_accelerated_atom = gdk_x11_get_xatom_by_name_for_display (display, "_CINNAMON_SESSION_ACCELERATED");

        {
                Atom type;
                gint format;
                gulong nitems;
                gulong bytes_after;
                guchar *data;

 read:
                gdk_x11_display_error_trap_push (display);
                XGetWindowProperty (GDK_DISPLAY_XDISPLAY (display), rootwin,
                                    is_accelerated_atom,
                                    0, G_MAXLONG, False, XA_CARDINAL, &type, &format, &nitems,
                                    &bytes_after, &data);
                gdk_x11_display_error_trap_pop_ignored (display);

                if (type == XA_CARDINAL) {
                        glong *is_accelerated_ptr = (glong*) data;

                        if (*is_accelerated_ptr == ACCEL_CHECK_RUNNING) {
                                /* Test in progress, wait */
                                if (wait_for_property_notify ())
                                        goto read;
                                /* else fall through and do the check ourselves */

                        } else {
                                return (*is_accelerated_ptr == 0 ? 1 : 0);
                        }
                }
        }

        /* We don't have the property or it's the wrong type.
         * Try to compute it now.
         */

        /* First indicate that a test is in progress */
        is_accelerated = ACCEL_CHECK_RUNNING;
        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         rootwin,
                         is_accelerated_atom,
                         XA_CARDINAL, 32, PropModeReplace, (guchar *) &is_accelerated, 1);

        gdk_display_sync (display);

        estatus = 1;
        if (!g_spawn_sync (NULL, (char**)child_argv, NULL, 0,
                           NULL, NULL, NULL, NULL, &estatus, &error)) {
                is_accelerated = FALSE;
                g_printerr ("cinnamon-session-check-accelerated: Failed to run helper: %s\n", error->message);
                g_clear_error (&error);
        } else {
                is_accelerated = (estatus == 0);
                if (!is_accelerated)
                        g_printerr ("cinnamon-session-check-accelerated: Helper exited with code %d\n", estatus);
        }

        XChangeProperty (GDK_DISPLAY_XDISPLAY (display),
                         rootwin,
                         is_accelerated_atom,
                         XA_CARDINAL, 32, PropModeReplace, (guchar *) &is_accelerated, 1);

        gdk_display_sync (display);

        return is_accelerated ? 0 : 1;
}
