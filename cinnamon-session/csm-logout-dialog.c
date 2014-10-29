/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 Vincent Untz
 * Copyright (C) 2008 Red Hat, Inc.
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
 *	Vincent Untz <vuntz@gnome.org>
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#ifdef HAVE_OLD_UPOWER
#define UPOWER_ENABLE_DEPRECATED 1
#include <upower.h>
#endif

#include "csm-logout-dialog.h"
#include "csm-system.h"
#include "csm-icon-names.h"
#include "mdm.h"

#define CSM_LOGOUT_DIALOG_GET_PRIVATE(o)                                \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_LOGOUT_DIALOG, CsmLogoutDialogPrivate))

#define LOCKDOWN_SCHEMA            "org.cinnamon.desktop.lockdown"
#define KEY_DISABLE_USER_SWITCHING "disable-user-switching"

struct _CsmLogoutDialogPrivate
{
        CsmDialogLogoutType  type;

#ifdef HAVE_OLD_UPOWER
        UpClient            *up_client;
#endif
        CsmSystem           *system;        
        unsigned int         default_response;
};

static CsmLogoutDialog *current_dialog = NULL;

static void csm_logout_dialog_destroy  (CsmLogoutDialog *logout_dialog,
                                        gpointer         data);

static void csm_logout_dialog_show     (CsmLogoutDialog *logout_dialog,
                                        gpointer         data);

enum {
        PROP_0,
        PROP_MESSAGE_TYPE
};

G_DEFINE_TYPE (CsmLogoutDialog, csm_logout_dialog, GTK_TYPE_MESSAGE_DIALOG);

static void
csm_logout_dialog_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
        switch (prop_id) {
        case PROP_MESSAGE_TYPE:
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_logout_dialog_get_property (GObject     *object,
                                guint        prop_id,
                                GValue      *value,
                                GParamSpec  *pspec)
{
        switch (prop_id) {
        case PROP_MESSAGE_TYPE:
                g_value_set_enum (value, GTK_MESSAGE_WARNING);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_logout_dialog_class_init (CsmLogoutDialogClass *klass)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (klass);

        /* This is a workaround to avoid a stupid crash: libgnomeui
         * listens for the "show" signal on all GtkMessageDialog and
         * gets the "message-type" of the dialogs. We will crash when
         * it accesses this property if we don't override it since we
         * didn't define it. */
        gobject_class->set_property = csm_logout_dialog_set_property;
        gobject_class->get_property = csm_logout_dialog_get_property;

        g_object_class_override_property (gobject_class,
                                          PROP_MESSAGE_TYPE,
                                          "message-type");

        g_type_class_add_private (klass, sizeof (CsmLogoutDialogPrivate));
}

static void
csm_logout_dialog_init (CsmLogoutDialog *logout_dialog)
{
        logout_dialog->priv = CSM_LOGOUT_DIALOG_GET_PRIVATE (logout_dialog);
     
        logout_dialog->priv->default_response = GTK_RESPONSE_CANCEL;

        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (logout_dialog), TRUE);
        gtk_window_set_keep_above (GTK_WINDOW (logout_dialog), TRUE);
        gtk_window_stick (GTK_WINDOW (logout_dialog));

#ifdef HAVE_OLD_UPOWER
        logout_dialog->priv->up_client = up_client_new ();
#endif

        logout_dialog->priv->system = csm_get_system ();

        g_signal_connect (logout_dialog,
                          "destroy",
                          G_CALLBACK (csm_logout_dialog_destroy),
                          NULL);

        g_signal_connect (logout_dialog,
                          "show",
                          G_CALLBACK (csm_logout_dialog_show),
                          NULL);
}

static void
csm_logout_dialog_destroy (CsmLogoutDialog *logout_dialog,
                           gpointer         data)
{       

#ifdef HAVE_OLD_UPOWER 
        if (logout_dialog->priv->up_client) {
                g_object_unref (logout_dialog->priv->up_client);
                logout_dialog->priv->up_client = NULL;
        }
#endif

        g_clear_object (&logout_dialog->priv->system);

        current_dialog = NULL;
}

static gboolean
csm_logout_supports_system_suspend (CsmLogoutDialog *logout_dialog)
{
#if defined(HAVE_SYSTEMD)
        return csm_system_can_suspend (logout_dialog->priv->system);
#elif defined(HAVE_OLD_UPOWER)
        return up_client_get_can_suspend (logout_dialog->priv->up_client);
#endif
}

static gboolean
csm_logout_supports_system_hibernate (CsmLogoutDialog *logout_dialog)
{
#if defined(HAVE_SYSTEMD)
        return csm_system_can_hibernate (logout_dialog->priv->system);
#elif defined(HAVE_OLD_UPOWER)
        return up_client_get_can_hibernate (logout_dialog->priv->up_client);
#endif
}

static gboolean
csm_logout_supports_switch_user (CsmLogoutDialog *logout_dialog)
{
        GSettings *settings;
        gboolean   ret;

        settings = g_settings_new (LOCKDOWN_SCHEMA);
        ret = !g_settings_get_boolean (settings, KEY_DISABLE_USER_SWITCHING);
        g_object_unref (settings);

        return ret;
}

static gboolean
csm_logout_supports_reboot (CsmLogoutDialog *logout_dialog)
{
        gboolean ret;

        ret = csm_system_can_restart (logout_dialog->priv->system);
        if (!ret) {
                ret = mdm_supports_logout_action (MDM_LOGOUT_ACTION_REBOOT);
        }

        return ret;
}

static gboolean
csm_logout_supports_shutdown (CsmLogoutDialog *logout_dialog)
{
        gboolean ret;

        ret = csm_system_can_stop (logout_dialog->priv->system);
        if (!ret) {
                ret = mdm_supports_logout_action (MDM_LOGOUT_ACTION_SHUTDOWN);
        }

        return ret;
}

static void
csm_logout_dialog_show (CsmLogoutDialog *logout_dialog, gpointer user_data)
{   
    if (!csm_system_is_login_session (logout_dialog->priv->system)) {
            gchar *name, *secondary_text;

            name = g_locale_to_utf8 (g_get_real_name (), -1, NULL, NULL, NULL);

            if (!name || name[0] == '\0' || strcmp (name, "Unknown") == 0) {
                    g_free (name);
                    name = g_locale_to_utf8 (g_get_user_name (), -1 , NULL, NULL, NULL);
            }

            if (!name) {
                    name = g_strdup (g_get_user_name ());
            }

            secondary_text = g_strdup_printf (_("You are currently logged in as \"%s\"."), name); 
            gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (logout_dialog), secondary_text, NULL);               
            g_free (secondary_text);        
            g_free (name);        
    }
}

static gboolean
grab_callback (GtkWidget *widget)
{
    return gdk_keyboard_grab (gtk_widget_get_window (widget), FALSE, GDK_CURRENT_TIME) != GDK_GRAB_SUCCESS;
}

static void
on_show (GtkWidget *widget, gpointer user_data)
{
    g_timeout_add (50, (GSourceFunc) grab_callback, widget);
}

static GtkWidget *
csm_get_dialog (CsmDialogLogoutType type,
                GdkScreen          *screen,
                guint32             activate_time)
{
        CsmLogoutDialog *logout_dialog;
        GtkWidget       *dialog_image;
        GtkWidget       *hbox;
        const char      *primary_text;
        const char      *icon_name;

        if (current_dialog != NULL) {
                gtk_widget_destroy (GTK_WIDGET (current_dialog));
        }

        logout_dialog = g_object_new (CSM_TYPE_LOGOUT_DIALOG, NULL);

        current_dialog = logout_dialog;

        gtk_window_set_title (GTK_WINDOW (logout_dialog), _("Session"));

        logout_dialog->priv->type = type;       

        icon_name = NULL;
        primary_text = NULL;

        switch (type) {
        case CSM_DIALOG_LOGOUT_TYPE_LOGOUT:
                icon_name    = CSM_ICON_LOGOUT;
                primary_text = _("Log out of this system now?");

                logout_dialog->priv->default_response = CSM_LOGOUT_RESPONSE_LOGOUT;

                if (csm_logout_supports_switch_user (logout_dialog)) {
                        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                               _("_Switch User"),
                                               CSM_LOGOUT_RESPONSE_SWITCH_USER);
                }

                gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL);

                gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                       _("_Log Out"),
                                       CSM_LOGOUT_RESPONSE_LOGOUT);

                break;
        case CSM_DIALOG_LOGOUT_TYPE_SHUTDOWN:
                icon_name    = CSM_ICON_SHUTDOWN;
                primary_text = _("Shut down this system now?");

                logout_dialog->priv->default_response = CSM_LOGOUT_RESPONSE_SHUTDOWN;

                if (csm_logout_supports_system_suspend (logout_dialog)) {
                        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                               _("S_uspend"),
                                               CSM_LOGOUT_RESPONSE_SLEEP);
                }

                if (csm_logout_supports_system_hibernate (logout_dialog)) {
                        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                               _("_Hibernate"),
                                               CSM_LOGOUT_RESPONSE_HIBERNATE);
                }

                if (csm_logout_supports_reboot (logout_dialog)) {
                        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                               _("_Restart"),
                                               CSM_LOGOUT_RESPONSE_REBOOT);
                }

                gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL);

                if (csm_logout_supports_shutdown (logout_dialog)) {
                        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                               _("_Shut Down"),
                                               CSM_LOGOUT_RESPONSE_SHUTDOWN);
                }
                break;
        case CSM_DIALOG_LOGOUT_TYPE_REBOOT:
                icon_name    = CSM_ICON_SHUTDOWN;
                primary_text = _("Restart this system now?");

                logout_dialog->priv->default_response = CSM_LOGOUT_RESPONSE_REBOOT;

                gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL);

                if (csm_logout_supports_reboot (logout_dialog)) {
                        gtk_dialog_add_button (GTK_DIALOG (logout_dialog),
                                               _("_Restart"),
                                               CSM_LOGOUT_RESPONSE_REBOOT);
                }
                break;
        default:
                g_assert_not_reached ();
        }
        
        dialog_image = gtk_message_dialog_get_image (GTK_MESSAGE_DIALOG (logout_dialog));
        
        hbox = gtk_box_new (FALSE, 0);
        
        gtk_widget_show_all (hbox);
        gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area (GTK_DIALOG (logout_dialog))), hbox);

        gtk_image_set_from_icon_name (GTK_IMAGE (dialog_image),
                                      icon_name, GTK_ICON_SIZE_DIALOG);
        gtk_window_set_icon_name (GTK_WINDOW (logout_dialog), icon_name);
        gtk_window_set_position (GTK_WINDOW (logout_dialog), GTK_WIN_POS_CENTER);
        gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (logout_dialog), primary_text);

        gtk_dialog_set_default_response (GTK_DIALOG (logout_dialog),
                                         logout_dialog->priv->default_response);

        gtk_window_set_screen (GTK_WINDOW (logout_dialog), screen);

        g_signal_connect (logout_dialog, "show", G_CALLBACK (on_show), NULL);
        
        return GTK_WIDGET (logout_dialog);
}

GtkWidget *
csm_get_shutdown_dialog (GdkScreen           *screen,
                         guint32              activate_time,
                         CsmDialogLogoutType  type)
{
        return csm_get_dialog (type,
                               screen,
                               activate_time);
}

GtkWidget *
csm_get_logout_dialog (GdkScreen *screen,
                       guint32    activate_time)
{
        return csm_get_dialog (CSM_DIALOG_LOGOUT_TYPE_LOGOUT,
                               screen,
                               activate_time);
}
