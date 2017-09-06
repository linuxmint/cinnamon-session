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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
 *
 * Authors:
 *	Colin Walters <walters@verbum.org>
 */

#include <config.h>

#include <glib/gi18n.h>

#include <gtk/gtkx.h>

#include "csm-fail-whale-dialog.h"

#include "csm-icon-names.h"
#include "csm-manager.h"
#include "csm-util.h"

#define CSM_FAIL_WHALE_DIALOG_GET_PRIVATE(o)                                \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_FAIL_WHALE_DIALOG, CsmFailWhaleDialogPrivate))

struct _CsmFailWhaleDialogPrivate
{
        gboolean debug_mode;
        gboolean allow_logout;
        GdkRectangle geometry;
};

G_DEFINE_TYPE (CsmFailWhaleDialog, csm_fail_whale_dialog, GTK_TYPE_WINDOW);

/* derived from tomboy */
static void
_window_override_user_time (CsmFailWhaleDialog *window)
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
_window_move_resize_window (CsmFailWhaleDialog *window,
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
update_geometry (CsmFailWhaleDialog *fail_dialog)
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
                        CsmFailWhaleDialog *fail_dialog)
{
        gtk_widget_queue_resize (GTK_WIDGET (fail_dialog));
}

static void
csm_fail_whale_dialog_finalize (GObject *object)
{
        CsmFailWhaleDialog *fail_dialog = CSM_FAIL_WHALE_DIALOG (object);

        G_OBJECT_CLASS (csm_fail_whale_dialog_parent_class)->finalize (object);
}

static void
csm_fail_whale_dialog_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (csm_fail_whale_dialog_parent_class)->realize) {
                GTK_WIDGET_CLASS (csm_fail_whale_dialog_parent_class)->realize (widget);
        }

        _window_override_user_time (CSM_FAIL_WHALE_DIALOG (widget));
        update_geometry (CSM_FAIL_WHALE_DIALOG (widget));
        _window_move_resize_window (CSM_FAIL_WHALE_DIALOG (widget), TRUE, TRUE);

        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (on_screen_size_changed),
                          widget);
}

static void
csm_fail_whale_dialog_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              on_screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (csm_fail_whale_dialog_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (csm_fail_whale_dialog_parent_class)->unrealize (widget);
        }
}

static void
csm_fail_whale_dialog_size_request (GtkWidget      *widget,
                                    GtkRequisition *requisition)
{
        CsmFailWhaleDialog *fail_dialog;
        GdkRectangle   old_geometry;
        int            position_changed = FALSE;
        int            size_changed = FALSE;

        fail_dialog = CSM_FAIL_WHALE_DIALOG (widget);

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
csm_fail_whale_dialog_get_preferred_width (GtkWidget *widget,
                                           gint      *minimal_width,
                                           gint      *natural_width)
{
        GtkRequisition requisition;

        csm_fail_whale_dialog_size_request (widget, &requisition);

        *minimal_width = *natural_width = requisition.width;
}

static void
csm_fail_whale_dialog_get_preferred_height (GtkWidget *widget,
                                            gint      *minimal_height,
                                            gint      *natural_height)
{
        GtkRequisition requisition;

        csm_fail_whale_dialog_size_request (widget, &requisition);

        *minimal_height = *natural_height = requisition.height;
}

static void
csm_fail_whale_dialog_class_init (CsmFailWhaleDialogClass *klass)
{
        GObjectClass   *object_class;
        GtkWidgetClass *widget_class;

        object_class = G_OBJECT_CLASS (klass);
        object_class->finalize = csm_fail_whale_dialog_finalize;

        widget_class = GTK_WIDGET_CLASS (klass);

        widget_class->realize = csm_fail_whale_dialog_realize;
        widget_class->unrealize = csm_fail_whale_dialog_unrealize;
        widget_class->get_preferred_width = csm_fail_whale_dialog_get_preferred_width;
        widget_class->get_preferred_height = csm_fail_whale_dialog_get_preferred_height;

        g_type_class_add_private (klass, sizeof (CsmFailWhaleDialogPrivate));
}

static void
csm_fail_whale_dialog_init (CsmFailWhaleDialog *fail_dialog)
{
        fail_dialog->priv = CSM_FAIL_WHALE_DIALOG_GET_PRIVATE (fail_dialog);
}

void
csm_fail_whale_dialog_we_failed (gboolean            debug_mode,
                                 gboolean            allow_logout)

{
        static gboolean failed = FALSE;

        if (failed) {
                return;
        }

        g_critical ("We failed, but the fail whale is dead. Sorry....");
        failed = TRUE;
}

