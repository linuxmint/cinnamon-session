/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "csm-inhibitor.h"
#include "csm-shell.h"

#define SHELL_NAME      "org.cinnamon.Shell"
#define SHELL_PATH      "/org/cinnamon/Shell"
#define SHELL_INTERFACE "org.cinnamon.Shell"

#define SHELL_END_SESSION_DIALOG_PATH      "/org/gnome/SessionManager/EndSessionDialog"
#define SHELL_END_SESSION_DIALOG_INTERFACE "org.gnome.SessionManager.EndSessionDialog"

#define CSM_SHELL_DBUS_TYPE_G_OBJECT_PATH_ARRAY (dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH))

#define AUTOMATIC_ACTION_TIMEOUT 60

#define CSM_SHELL_GET_PRIVATE(o)                                   \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_SHELL, CsmShellPrivate))

struct _CsmShellPrivate
{
        DBusGConnection *bus_connection;
        DBusGProxy      *bus_proxy;
        DBusGProxy      *proxy;
        DBusGProxy      *end_session_dialog_proxy;
        CsmStore        *inhibitors;

        guint32          is_running : 1;

        DBusGProxyCall  *end_session_open_call;
        CsmShellEndSessionDialogType end_session_dialog_type;

        guint            update_idle_id;
};

enum {
        PROP_0,
        PROP_IS_RUNNING
};

enum {
        END_SESSION_DIALOG_OPENED = 0,
        END_SESSION_DIALOG_OPEN_FAILED,
        END_SESSION_DIALOG_CLOSED,
        END_SESSION_DIALOG_CANCELED,
        END_SESSION_DIALOG_CONFIRMED_LOGOUT,
        END_SESSION_DIALOG_CONFIRMED_SHUTDOWN,
        END_SESSION_DIALOG_CONFIRMED_REBOOT,
        NUMBER_OF_SIGNALS
};

static guint signals[NUMBER_OF_SIGNALS] = { 0 };

static void     csm_shell_class_init   (CsmShellClass *klass);
static void     csm_shell_init         (CsmShell      *ck);
static void     csm_shell_finalize     (GObject            *object);

static void     csm_shell_disconnect_from_bus    (CsmShell      *shell);

static DBusHandlerResult csm_shell_bus_filter (DBusConnection *connection,
                                                DBusMessage    *message,
                                                void           *user_data);

static void     csm_shell_on_name_owner_changed (DBusGProxy *bus_proxy,
                                                 const char *name,
                                                 const char *prev_owner,
                                                 const char *new_owner,
                                                 CsmShell   *shell);
static void     queue_end_session_dialog_update (CsmShell *shell);

G_DEFINE_TYPE (CsmShell, csm_shell, G_TYPE_OBJECT);

static void
csm_shell_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        CsmShell *shell = CSM_SHELL (object);

        switch (prop_id) {
        case PROP_IS_RUNNING:
                g_value_set_boolean (value,
                                     shell->priv->is_running);
                break;

        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object,
                                                   prop_id,
                                                   pspec);
        }
}

static void
csm_shell_class_init (CsmShellClass *shell_class)
{
        GObjectClass *object_class;
        GParamSpec   *param_spec;

        object_class = G_OBJECT_CLASS (shell_class);

        object_class->finalize = csm_shell_finalize;
        object_class->get_property = csm_shell_get_property;

        param_spec = g_param_spec_boolean ("is-running",
                                           "Is running",
                                           "Whether GNOME Shell is running in the session",
                                           FALSE,
                                           G_PARAM_READABLE);

        g_object_class_install_property (object_class, PROP_IS_RUNNING,
                                         param_spec);

        signals [END_SESSION_DIALOG_OPENED] =
                g_signal_new ("end-session-dialog-opened",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_opened),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_OPEN_FAILED] =
                g_signal_new ("end-session-dialog-open-failed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_open_failed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CLOSED] =
                g_signal_new ("end-session-dialog-closed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_closed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CANCELED] =
                g_signal_new ("end-session-dialog-canceled",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_canceled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_LOGOUT] =
                g_signal_new ("end-session-dialog-confirmed-logout",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_confirmed_logout),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_SHUTDOWN] =
                g_signal_new ("end-session-dialog-confirmed-shutdown",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_confirmed_shutdown),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        signals [END_SESSION_DIALOG_CONFIRMED_REBOOT] =
                g_signal_new ("end-session-dialog-confirmed-reboot",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmShellClass, end_session_dialog_confirmed_reboot),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        g_type_class_add_private (shell_class, sizeof (CsmShellPrivate));
}

static DBusHandlerResult
csm_shell_bus_filter (DBusConnection *connection,
                       DBusMessage    *message,
                       void           *user_data)
{
        CsmShell *shell;

        shell = CSM_SHELL (user_data);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                csm_shell_disconnect_from_bus (shell);
                /* let other filters get this disconnected signal, so that they
                 * can handle it too */
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
csm_shell_ensure_connection (CsmShell  *shell,
                             GError   **error)
{
        GError  *connection_error;
        gboolean is_running;

        connection_error = NULL;

        if (shell->priv->bus_connection == NULL) {
                DBusConnection *connection;

                shell->priv->bus_connection = dbus_g_bus_get (DBUS_BUS_SESSION,
                                                               &connection_error);

                if (shell->priv->bus_connection == NULL) {
                        g_propagate_error (error, connection_error);
                        is_running = FALSE;
                        goto out;
                }

                connection = dbus_g_connection_get_connection (shell->priv->bus_connection);
                dbus_connection_set_exit_on_disconnect (connection, FALSE);
                dbus_connection_add_filter (connection,
                                            csm_shell_bus_filter,
                                            shell, NULL);
        }

        if (shell->priv->bus_proxy == NULL) {
                shell->priv->bus_proxy =
                        dbus_g_proxy_new_for_name_owner (shell->priv->bus_connection,
                                                         DBUS_SERVICE_DBUS,
                                                         DBUS_PATH_DBUS,
                                                         DBUS_INTERFACE_DBUS,
                                                         &connection_error);

                if (shell->priv->bus_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_running = FALSE;
                        goto out;
                }

                dbus_g_proxy_add_signal (shell->priv->bus_proxy,
                                         "NameOwnerChanged",
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_INVALID);

                dbus_g_proxy_connect_signal (shell->priv->bus_proxy,
                                             "NameOwnerChanged",
                                             G_CALLBACK (csm_shell_on_name_owner_changed),
                                             shell, NULL);
        }

        if (shell->priv->proxy == NULL) {
                shell->priv->proxy =
                        dbus_g_proxy_new_for_name_owner (shell->priv->bus_connection,
                                                         SHELL_NAME,
                                                         SHELL_PATH,
                                                         SHELL_INTERFACE,
                                                         &connection_error);

                if (shell->priv->proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_running = FALSE;
                        goto out;
                }
        }

        g_debug ("CsmShell: Connected to the shell");

        is_running = TRUE;

 out:
        if (shell->priv->is_running != is_running) {
                shell->priv->is_running = is_running;
                g_object_notify (G_OBJECT (shell), "is-running");
        }

        if (!is_running) {
                g_debug ("CsmShell: Not connected to the shell");

                if (shell->priv->bus_connection == NULL) {
                        if (shell->priv->bus_proxy != NULL) {
                                g_object_unref (shell->priv->bus_proxy);
                                shell->priv->bus_proxy = NULL;
                        }

                        if (shell->priv->proxy != NULL) {
                                g_object_unref (shell->priv->proxy);
                                shell->priv->proxy = NULL;
                        }
                } else if (shell->priv->bus_proxy == NULL) {
                        if (shell->priv->proxy != NULL) {
                                g_object_unref (shell->priv->proxy);
                                shell->priv->proxy = NULL;
                        }
                }
        }

        return is_running;
}

static void
csm_shell_on_name_owner_changed (DBusGProxy    *bus_proxy,
                                 const char    *name,
                                 const char    *prev_owner,
                                 const char    *new_owner,
                                 CsmShell      *shell)
{
        if (name != NULL && strcmp (name, SHELL_NAME) != 0) {
                return;
        }

        if (shell->priv->proxy != NULL) {
                g_object_unref (shell->priv->proxy);
                shell->priv->proxy = NULL;
        }

        csm_shell_ensure_connection (shell, NULL);
}

static void
csm_shell_init (CsmShell *shell)
{
        shell->priv = CSM_SHELL_GET_PRIVATE (shell);

        csm_shell_ensure_connection (shell, NULL);
}

static void
csm_shell_disconnect_from_bus (CsmShell *shell)
{
        if (shell->priv->bus_proxy != NULL) {
                g_object_unref (shell->priv->bus_proxy);
                shell->priv->bus_proxy = NULL;
        }

        if (shell->priv->proxy != NULL) {
                g_object_unref (shell->priv->proxy);
                shell->priv->proxy = NULL;
        }

        if (shell->priv->bus_connection != NULL) {
                DBusConnection *connection;
                connection = dbus_g_connection_get_connection (shell->priv->bus_connection);
                dbus_connection_remove_filter (connection,
                                               csm_shell_bus_filter,
                                               shell);

                dbus_g_connection_unref (shell->priv->bus_connection);
                shell->priv->bus_connection = NULL;
        }
}

static void
csm_shell_finalize (GObject *object)
{
        CsmShell *shell;
        GObjectClass  *parent_class;

        shell = CSM_SHELL (object);

        parent_class = G_OBJECT_CLASS (csm_shell_parent_class);

        g_object_unref (shell->priv->inhibitors);

        csm_shell_disconnect_from_bus (shell);

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

CsmShell *
csm_shell_new (void)
{
        CsmShell *shell;

        shell = g_object_new (CSM_TYPE_SHELL, NULL);

        return shell;
}

CsmShell *
csm_get_shell (void)
{
        static CsmShell *shell = NULL;

        if (shell == NULL) {
                shell = csm_shell_new ();
        }

        return g_object_ref (shell);
}

gboolean
csm_shell_is_running (CsmShell *shell)
{
        csm_shell_ensure_connection (shell, NULL);

        return shell->priv->is_running;
}

static gboolean
add_inhibitor_to_array (const char   *id,
                        CsmInhibitor *inhibitor,
                        GPtrArray    *array)
{

        g_ptr_array_add (array,
                         g_strdup (csm_inhibitor_peek_id (inhibitor)));
        return FALSE;
}

static GPtrArray *
get_array_from_store (CsmStore *inhibitors)
{
        GPtrArray *array;

        array = g_ptr_array_new ();

        csm_store_foreach (inhibitors,
                           (CsmStoreFunc) add_inhibitor_to_array,
                           array);

        return array;
}

static void
on_open_finished (DBusGProxy     *proxy,
                  DBusGProxyCall *call,
                  CsmShell       *shell)
{
        GError         *error;
        gboolean        res;

        g_assert (shell->priv->end_session_open_call == call);

        error = NULL;
        res = dbus_g_proxy_end_call (proxy,
                                     call,
                                     &error,
                                     G_TYPE_INVALID);
        shell->priv->end_session_open_call = NULL;

        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        if (!res) {
                g_warning ("Unable to open shell end session dialog");
                g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_OPEN_FAILED], 0);
                return;
        }

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_OPENED], 0);
}

static void
on_end_session_dialog_closed (DBusGProxy *proxy,
                              CsmShell   *shell)
{
        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        g_signal_handlers_disconnect_by_func (shell->priv->inhibitors,
                                              G_CALLBACK (queue_end_session_dialog_update),
                                              shell);

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CLOSED], 0);
}

static void
on_end_session_dialog_canceled (DBusGProxy *proxy,
                                CsmShell   *shell)
{
        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        g_signal_handlers_disconnect_by_func (shell->priv->inhibitors,
                                              G_CALLBACK (queue_end_session_dialog_update),
                                              shell);

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CANCELED], 0);
}

static void
on_end_session_dialog_confirmed_logout (DBusGProxy *proxy,
                                        CsmShell   *shell)
{
        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CONFIRMED_LOGOUT], 0);
}

static void
on_end_session_dialog_confirmed_shutdown (DBusGProxy *proxy,
                                          CsmShell   *shell)
{
        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CONFIRMED_SHUTDOWN], 0);
}

static void
on_end_session_dialog_confirmed_reboot (DBusGProxy *proxy,
                                        CsmShell   *shell)
{
        if (shell->priv->update_idle_id != 0) {
                g_source_remove (shell->priv->update_idle_id);
                shell->priv->update_idle_id = 0;
        }

        g_signal_emit (G_OBJECT (shell), signals[END_SESSION_DIALOG_CONFIRMED_REBOOT], 0);
}

static void
on_end_session_dialog_proxy_destroyed (DBusGProxy *proxy,
                                       CsmShell   *shell)
{
        if (shell->priv->end_session_dialog_proxy != NULL) {
                g_object_unref (shell->priv->proxy);
                shell->priv->end_session_dialog_proxy = NULL;
        }
}

static gboolean
on_need_end_session_dialog_update (CsmShell *shell)
{
        /* No longer need an update */
        if (shell->priv->update_idle_id == 0)
                return FALSE;

        shell->priv->update_idle_id = 0;

        csm_shell_open_end_session_dialog (shell,
                                           shell->priv->end_session_dialog_type,
                                           shell->priv->inhibitors);
        return FALSE;
}

static void
queue_end_session_dialog_update (CsmShell *shell)
{
        if (shell->priv->update_idle_id != 0)
                return;

        shell->priv->update_idle_id = g_idle_add ((GSourceFunc) on_need_end_session_dialog_update,
                                                  shell);
}

gboolean
csm_shell_open_end_session_dialog (CsmShell *shell,
                                   CsmShellEndSessionDialogType type,
                                   CsmStore *inhibitors)
{
        DBusGProxyCall  *call;
        GPtrArray *inhibitor_array;
        GError *error;

        error = NULL;
        if (!csm_shell_ensure_connection (shell, &error)) {
                g_warning ("Could not connect to the shell: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        if (shell->priv->end_session_open_call != NULL) {
                g_return_val_if_fail (shell->priv->end_session_dialog_type == type,
                                      FALSE);

                return TRUE;
        }

        if (shell->priv->end_session_dialog_proxy == NULL) {
                DBusGProxy *proxy;

                proxy = dbus_g_proxy_new_for_name (shell->priv->bus_connection,
                                                   SHELL_NAME,
                                                   SHELL_END_SESSION_DIALOG_PATH,
                                                   SHELL_END_SESSION_DIALOG_INTERFACE);

                g_assert (proxy != NULL);

                shell->priv->end_session_dialog_proxy = proxy;

                g_signal_connect (proxy, "destroy",
                                  G_CALLBACK (on_end_session_dialog_proxy_destroyed),
                                  shell);

                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "Closed", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "Closed",
                                             G_CALLBACK (on_end_session_dialog_closed),
                                             shell, NULL);

                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "Canceled", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "Canceled",
                                             G_CALLBACK (on_end_session_dialog_canceled),
                                             shell, NULL);
                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "ConfirmedLogout", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "ConfirmedLogout",
                                             G_CALLBACK (on_end_session_dialog_confirmed_logout),
                                             shell, NULL);
                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "ConfirmedShutdown", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "ConfirmedShutdown",
                                             G_CALLBACK (on_end_session_dialog_confirmed_shutdown),
                                             shell, NULL);
                dbus_g_proxy_add_signal (shell->priv->end_session_dialog_proxy,
                                         "ConfirmedReboot", G_TYPE_INVALID);
                dbus_g_proxy_connect_signal (shell->priv->end_session_dialog_proxy,
                                             "ConfirmedReboot",
                                             G_CALLBACK (on_end_session_dialog_confirmed_reboot),
                                             shell, NULL);
        }

        inhibitor_array = get_array_from_store (inhibitors);

        call = dbus_g_proxy_begin_call_with_timeout (shell->priv->end_session_dialog_proxy,
                                                     "Open",
                                                     (DBusGProxyCallNotify)
                                                     on_open_finished,
                                                     shell,
                                                     NULL,
                                                     INT_MAX,
                                                     G_TYPE_UINT,
                                                     (guint) type,
                                                     G_TYPE_UINT,
                                                     (guint) 0,
                                                     G_TYPE_UINT,
                                                     AUTOMATIC_ACTION_TIMEOUT,
                                                     CSM_SHELL_DBUS_TYPE_G_OBJECT_PATH_ARRAY,
                                                     inhibitor_array,
                                                     G_TYPE_INVALID);
        g_ptr_array_foreach (inhibitor_array, (GFunc) g_free, NULL);
        g_ptr_array_free (inhibitor_array, TRUE);

        if (call == NULL) {
                g_warning ("Unable to make Open call");
                return FALSE;
        }

        g_object_ref (inhibitors);

        if (shell->priv->inhibitors != NULL) {
                g_signal_handlers_disconnect_by_func (shell->priv->inhibitors,
                                                      G_CALLBACK (queue_end_session_dialog_update),
                                                      shell);
                g_object_unref (shell->priv->inhibitors);
        }

        shell->priv->inhibitors = inhibitors;

        g_signal_connect_swapped (inhibitors, "added",
                                  G_CALLBACK (queue_end_session_dialog_update),
                                  shell);

        g_signal_connect_swapped (inhibitors, "removed",
                                  G_CALLBACK (queue_end_session_dialog_update),
                                  shell);

        shell->priv->end_session_open_call = call;
        shell->priv->end_session_dialog_type = type;

        return TRUE;
}
