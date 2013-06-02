/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Colin Walters <walters@verbum.org>
 */

#include <config.h>

#include <glib/gi18n.h>

#include <gtk/gtkx.h>

#include "gsm-fail-whale-dialog.h"

#include "gsm-icon-names.h"
#include "gsm-manager.h"
#include "gsm-util.h"
#include "gsm-shell-extensions.h"

#define GSM_FAIL_WHALE_DIALOG_GET_PRIVATE(o)                                \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), GSM_TYPE_FAIL_WHALE_DIALOG, GsmFailWhaleDialogPrivate))

struct _GsmFailWhaleDialogPrivate
{
        gboolean debug_mode;
        gboolean allow_logout;
        GdkRectangle geometry;
        GsmShellExtensions *extensions;
};

G_DEFINE_TYPE (GsmFailWhaleDialog, gsm_fail_whale_dialog, GTK_TYPE_WINDOW);

/* derived from tomboy */
static void
_window_override_user_time (GsmFailWhaleDialog *window)
{
        guint32 ev_time = gtk_get_current_event_time ();

        if (ev_time == 0) {
                gint ev_mask = gtk_widget_get_events (GTK_WIDGET (window));
                if (!(ev_mask & GDK_PROPERTY_CHANGE_MASK)) {
                        gtk_widget_add_events (GTK_WIDGET (window),
                                               GDK_PROPERTY_CHANGE_MASK);
                }

                /*
                 * NOTE: Last resort for D-BUS or other non-interactive
                 *       openings.  Causes roundtrip to server.  Lame.
                 */
                ev_time = gdk_x11_get_server_time (gtk_widget_get_window (GTK_WIDGET (window)));
        }

        gdk_x11_window_set_user_time (gtk_widget_get_window (GTK_WIDGET (window)), ev_time);
}

/* copied from panel-toplevel.c */
static void
_window_move_resize_window (GsmFailWhaleDialog *window,
                            gboolean  move,
                            gboolean  resize)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (window);

        g_assert (gtk_widget_get_realized (widget));

        if (window->priv->debug_mode)
                return;

        g_debug ("Move and/or resize window x=%d y=%d w=%d h=%d",
                 window->priv->geometry.x,
                 window->priv->geometry.y,
                 window->priv->geometry.width,
                 window->priv->geometry.height);

        if (move && resize) {
                gdk_window_move_resize (gtk_widget_get_window (widget),
                                        window->priv->geometry.x,
                                        window->priv->geometry.y,
                                        window->priv->geometry.width,
                                        window->priv->geometry.height);
        } else if (move) {
                gdk_window_move (gtk_widget_get_window (widget),
                                 window->priv->geometry.x,
                                 window->priv->geometry.y);
        } else if (resize) {
                gdk_window_resize (gtk_widget_get_window (widget),
                                   window->priv->geometry.width,
                                   window->priv->geometry.height);
        }
}

static void
update_geometry (GsmFailWhaleDialog *fail_dialog)
{
        int monitor;
        GdkScreen *screen;

        screen = gtk_widget_get_screen (GTK_WIDGET (fail_dialog));
        monitor = gdk_screen_get_primary_monitor (screen);

        gdk_screen_get_monitor_geometry (screen,
                                         monitor,
                                         &fail_dialog->priv->geometry);
}

static void
on_screen_size_changed (GdkScreen          *screen,
                        GsmFailWhaleDialog *fail_dialog)
{
        gtk_widget_queue_resize (GTK_WIDGET (fail_dialog));
}

static void
gsm_fail_whale_dialog_finalize (GObject *object)
{
        GsmFailWhaleDialog *fail_dialog = GSM_FAIL_WHALE_DIALOG (object);
        GsmFailWhaleDialogPrivate *priv = fail_dialog->priv;

        if (priv->extensions != NULL) {
                g_object_unref (priv->extensions);
                priv->extensions = NULL;
        }

        G_OBJECT_CLASS (gsm_fail_whale_dialog_parent_class)->finalize (object);
}

static void
gsm_fail_whale_dialog_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gsm_fail_whale_dialog_parent_class)->realize) {
                GTK_WIDGET_CLASS (gsm_fail_whale_dialog_parent_class)->realize (widget);
        }

        _window_override_user_time (GSM_FAIL_WHALE_DIALOG (widget));
        update_geometry (GSM_FAIL_WHALE_DIALOG (widget));
        _window_move_resize_window (GSM_FAIL_WHALE_DIALOG (widget), TRUE, TRUE);

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (on_screen_size_changed),
                          widget);
}

static void
gsm_fail_whale_dialog_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              on_screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (gsm_fail_whale_dialog_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gsm_fail_whale_dialog_parent_class)->unrealize (widget);
        }
}

static void
gsm_fail_whale_dialog_size_request (GtkWidget      *widget,
                                    GtkRequisition *requisition)
{
        GsmFailWhaleDialog *fail_dialog;
        GdkRectangle   old_geometry;
        int            position_changed = FALSE;
        int            size_changed = FALSE;

        fail_dialog = GSM_FAIL_WHALE_DIALOG (widget);

        old_geometry = fail_dialog->priv->geometry;

        update_geometry (fail_dialog);

        requisition->width  = fail_dialog->priv->geometry.width;
        requisition->height = fail_dialog->priv->geometry.height;

        if (!gtk_widget_get_realized (widget)) {
                return;
        }

        if (old_geometry.width  != fail_dialog->priv->geometry.width ||
            old_geometry.height != fail_dialog->priv->geometry.height) {
                size_changed = TRUE;
        }

        if (old_geometry.x != fail_dialog->priv->geometry.x ||
            old_geometry.y != fail_dialog->priv->geometry.y) {
                position_changed = TRUE;
        }

        _window_move_resize_window (fail_dialog,
                                    position_changed, size_changed);
}

static void
gsm_fail_whale_dialog_get_preferred_width (GtkWidget *widget,
                                           gint      *minimal_width,
                                           gint      *natural_width)
{
        GtkRequisition requisition;

        gsm_fail_whale_dialog_size_request (widget, &requisition);

        *minimal_width = *natural_width = requisition.width;
}

static void
gsm_fail_whale_dialog_get_preferred_height (GtkWidget *widget,
                                            gint      *minimal_height,
                                            gint      *natural_height)
{
        GtkRequisition requisition;

        gsm_fail_whale_dialog_size_request (widget, &requisition);

        *minimal_height = *natural_height = requisition.height;
}

static void
gsm_fail_whale_dialog_class_init (GsmFailWhaleDialogClass *klass)
{
        GObjectClass   *object_class;
        GtkWidgetClass *widget_class;

        object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = gsm_fail_whale_dialog_finalize;

        widget_class = GTK_WIDGET_CLASS (klass);

        widget_class->realize = gsm_fail_whale_dialog_realize;
        widget_class->unrealize = gsm_fail_whale_dialog_unrealize;
        widget_class->get_preferred_width = gsm_fail_whale_dialog_get_preferred_width;
        widget_class->get_preferred_height = gsm_fail_whale_dialog_get_preferred_height;

        g_type_class_add_private (klass, sizeof (GsmFailWhaleDialogPrivate));
}

static void
on_logout_clicked (GtkWidget          *button,
                   GsmFailWhaleDialog *fail_dialog)
{
        if (fail_dialog->priv->debug_mode) {
                gtk_main_quit ();
        } else {
                gsm_manager_logout (gsm_manager_get (),
                                    GSM_MANAGER_LOGOUT_MODE_FORCE,
                                    NULL);

                gtk_widget_destroy (GTK_WIDGET (fail_dialog));
        }
}

static void
setup_window (GsmFailWhaleDialog *fail_dialog)
{
        GsmFailWhaleDialogPrivate *priv;
        GtkWidget *alignment;
        GtkWidget *box;
        GtkWidget *image;
        GtkWidget *label;
        GtkWidget *message_label;
        GtkWidget *button_box;
        GtkWidget *button;
        char *markup;

        priv = fail_dialog->priv;

        gtk_window_set_title (GTK_WINDOW (fail_dialog), "");
        gtk_window_set_icon_name (GTK_WINDOW (fail_dialog), GSM_ICON_COMPUTER_FAIL);

        if (!fail_dialog->priv->debug_mode) {
                gtk_window_set_skip_taskbar_hint (GTK_WINDOW (fail_dialog), TRUE);
                gtk_window_set_keep_above (GTK_WINDOW (fail_dialog), TRUE);
                gtk_window_stick (GTK_WINDOW (fail_dialog));
                gtk_window_set_position (GTK_WINDOW (fail_dialog), GTK_WIN_POS_CENTER_ALWAYS);
                /* only works if there is a window manager which is unlikely */
                gtk_window_fullscreen (GTK_WINDOW (fail_dialog));
        }

        alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
        gtk_widget_show (alignment);
        gtk_container_add (GTK_CONTAINER (fail_dialog), alignment);
        g_object_set (alignment, "valign", GTK_ALIGN_CENTER, NULL);

        box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_show (box);
        gtk_container_add (GTK_CONTAINER (alignment), box);

        image = gtk_image_new_from_icon_name (GSM_ICON_COMPUTER_FAIL,
                                              gsm_util_get_computer_fail_icon_size ());
        gtk_widget_show (image);
        gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);

        label = gtk_label_new (NULL);
        markup = g_strdup_printf ("<b><big>%s</big></b>", _("Oh no!  Something has gone wrong."));
        gtk_label_set_markup (GTK_LABEL (label), markup);
        g_free (markup);
        gtk_widget_show (label);
        gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

        if (!priv->allow_logout)
                message_label = gtk_label_new (_("A problem has occurred and the system can't recover. Please contact a system administrator"));
        else if (priv->extensions != NULL && gsm_shell_extensions_n_extensions (priv->extensions) > 0)
                message_label = gtk_label_new (_("A problem has occurred and the system can't recover. All extensions have been disabled as a precaution."));
        else
                message_label = gtk_label_new (_("A problem has occurred and the system can't recover.\nPlease log out and try again."));

        gtk_label_set_justify (GTK_LABEL (message_label), GTK_JUSTIFY_CENTER);
        gtk_label_set_line_wrap (GTK_LABEL (message_label), TRUE);
        gtk_widget_show (message_label);
        gtk_box_pack_start (GTK_BOX (box),
                            message_label, FALSE, FALSE, 0);

        button_box = gtk_button_box_new (GTK_ORIENTATION_HORIZONTAL);
        gtk_container_set_border_width (GTK_CONTAINER (button_box), 20);
        gtk_widget_show (button_box);
        gtk_box_pack_end (GTK_BOX (box),
                          button_box, FALSE, FALSE, 0);

        if (priv->allow_logout) {
                button = gtk_button_new_with_mnemonic (_("_Log Out"));
                gtk_widget_show (button);
                gtk_box_pack_end (GTK_BOX (button_box),
                                  button, FALSE, FALSE, 0);
                g_signal_connect (button, "clicked",
                                  G_CALLBACK (on_logout_clicked), fail_dialog);
        }
}

static void
gsm_fail_whale_dialog_init (GsmFailWhaleDialog *fail_dialog)
{
        fail_dialog->priv = GSM_FAIL_WHALE_DIALOG_GET_PRIVATE (fail_dialog);
}

void
gsm_fail_whale_dialog_we_failed (gboolean            debug_mode,
                                 gboolean            allow_logout,
                                 GsmShellExtensions *extensions)

{
        static GsmFailWhaleDialog *current_dialog = NULL;
        GsmFailWhaleDialog        *fail_dialog;

        if (current_dialog != NULL) {
                return;
        }

        fail_dialog = g_object_new (GSM_TYPE_FAIL_WHALE_DIALOG, NULL);
        fail_dialog->priv->debug_mode = debug_mode;
        fail_dialog->priv->allow_logout = allow_logout;
        fail_dialog->priv->extensions = extensions;

        setup_window (fail_dialog);

        current_dialog = fail_dialog;
        g_signal_connect (current_dialog,
                          "destroy",
                          G_CALLBACK (gtk_widget_destroyed),
                          &current_dialog);

        gtk_widget_show (GTK_WIDGET (fail_dialog));
}

