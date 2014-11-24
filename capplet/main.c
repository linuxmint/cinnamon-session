/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 * main.c
 * Copyright (C) 1999 Free Software Foundation, Inc.
 * Copyright (C) 2008 Lucas Rocha.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 */

#include <config.h>

#include <unistd.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "csm-properties-dialog.h"

static gboolean    show_version     = FALSE;

static GOptionEntry options[] = {
        { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, N_("Version of this application"), NULL },
        { NULL, 0, 0, 0, NULL, NULL, NULL }
};

static gboolean
delay_quit (GtkWidget *dialog)
{
    gtk_widget_destroy (dialog);
    gtk_main_quit ();
    return FALSE;
}

static void
dialog_response (CsmPropertiesDialog *dialog,
                 guint                response_id,
                 gpointer             data)
{
        GdkScreen *screen;
        GError    *error;

        if (response_id == GTK_RESPONSE_HELP) {
                screen = gtk_widget_get_screen (GTK_WIDGET (dialog));

                error = NULL;
                gtk_show_uri (screen, "ghelp:user-guide?gosstartsession-2",
                              gtk_get_current_event_time (), &error);

                if (error != NULL) {
                        GtkWidget *d;
                        d = gtk_message_dialog_new (GTK_WINDOW (dialog),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_CLOSE,
                                                    "%s",
                                                    _("Could not display help document"));
                        gtk_message_dialog_format_secondary_text (
                                                GTK_MESSAGE_DIALOG (d),
                                                "%s", error->message);
                        g_error_free (error);

                        gtk_dialog_run (GTK_DIALOG (d));
                        gtk_widget_destroy (d);
                }
        } else {
            gtk_widget_hide (GTK_WIDGET (dialog));
            g_timeout_add_seconds (2, (GSourceFunc) delay_quit, dialog);
        }
}

int
main (int argc, char *argv[])
{
        GError    *error;
        GtkWidget *dialog;

        bindtextdomain ("cinnamon-session", "/usr/share/locale");
        bind_textdomain_codeset ("cinnamon-session", "UTF-8");
        textdomain ("cinnamon-session");

        error = NULL;
        if (! gtk_init_with_args (&argc, &argv, " - Cinnamon Session Properties", options, "cinnamon-session", &error)) {
                g_warning ("Unable to start: %s", error->message);
                g_error_free (error);
                return 1;
        }

        if (show_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                return 0;
        }

        dialog = csm_properties_dialog_new ();
        g_signal_connect (dialog,
                          "response",
                          G_CALLBACK (dialog_response),
                          NULL);

        g_signal_connect (dialog,
                          "delete-event",
                          G_CALLBACK (gtk_widget_hide_on_delete),
                          NULL);

        gtk_widget_show (dialog);

        gtk_main ();

        return 0;
}
