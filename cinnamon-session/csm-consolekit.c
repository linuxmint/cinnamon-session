/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Jon McCann <jmccann@redhat.com>
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA
 * 02110-1335, USA.
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

#ifdef HAVE_OLD_UPOWER
#define UPOWER_ENABLE_DEPRECATED 1
#include <upower.h>
#endif

#include "csm-system.h"
#include "csm-consolekit.h"

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define CSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW "LoginWindow"

#define CSM_CONSOLEKIT_GET_PRIVATE(o)                                   \
        (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_CONSOLEKIT, CsmConsolekitPrivate))

struct _CsmConsolekitPrivate
{
        DBusGConnection *dbus_connection;
        DBusGProxy      *bus_proxy;
        DBusGProxy      *ck_proxy;
#ifdef HAVE_OLD_UPOWER
        UpClient        *up_client;
#endif
};

static void     csm_consolekit_finalize     (GObject            *object);

static void     csm_consolekit_free_dbus    (CsmConsolekit      *manager);

static DBusHandlerResult csm_consolekit_dbus_filter (DBusConnection *connection,
                                                     DBusMessage    *message,
                                                     void           *user_data);

static void     csm_consolekit_on_name_owner_changed (DBusGProxy        *bus_proxy,
                                                      const char        *name,
                                                      const char        *prev_owner,
                                                      const char        *new_owner,
                                                      CsmConsolekit   *manager);

static void csm_consolekit_system_init (CsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (CsmConsolekit, csm_consolekit, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CSM_TYPE_SYSTEM,
                                                csm_consolekit_system_init))

static void
csm_consolekit_class_init (CsmConsolekitClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->finalize = csm_consolekit_finalize;

        g_type_class_add_private (manager_class, sizeof (CsmConsolekitPrivate));
}

static DBusHandlerResult
csm_consolekit_dbus_filter (DBusConnection *connection,
                            DBusMessage    *message,
                            void           *user_data)
{
        CsmConsolekit *manager;

        manager = CSM_CONSOLEKIT (user_data);

        if (dbus_message_is_signal (message,
                                    DBUS_INTERFACE_LOCAL, "Disconnected") &&
            strcmp (dbus_message_get_path (message), DBUS_PATH_LOCAL) == 0) {
                csm_consolekit_free_dbus (manager);
                /* let other filters get this disconnected signal, so that they
                 * can handle it too */
        }

        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static gboolean
csm_consolekit_ensure_ck_connection (CsmConsolekit  *manager,
                                     GError        **error)
{
        GError  *connection_error;
        gboolean is_connected;

        connection_error = NULL;

        if (manager->priv->dbus_connection == NULL) {
                DBusConnection *connection;

                manager->priv->dbus_connection = dbus_g_bus_get (DBUS_BUS_SYSTEM,
                                                                 &connection_error);

                if (manager->priv->dbus_connection == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                connection = dbus_g_connection_get_connection (manager->priv->dbus_connection);
                dbus_connection_set_exit_on_disconnect (connection, FALSE);
                dbus_connection_add_filter (connection,
                                            csm_consolekit_dbus_filter,
                                            manager, NULL);
        }

        if (manager->priv->bus_proxy == NULL) {
                manager->priv->bus_proxy =
                        dbus_g_proxy_new_for_name_owner (manager->priv->dbus_connection,
                                                         DBUS_SERVICE_DBUS,
                                                         DBUS_PATH_DBUS,
                                                         DBUS_INTERFACE_DBUS,
                                                         &connection_error);

                if (manager->priv->bus_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }

                dbus_g_proxy_add_signal (manager->priv->bus_proxy,
                                         "NameOwnerChanged",
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_STRING,
                                         G_TYPE_INVALID);

                dbus_g_proxy_connect_signal (manager->priv->bus_proxy,
                                             "NameOwnerChanged",
                                             G_CALLBACK (csm_consolekit_on_name_owner_changed),
                                             manager, NULL);
        }

        if (manager->priv->ck_proxy == NULL) {
                manager->priv->ck_proxy =
                        dbus_g_proxy_new_for_name_owner (manager->priv->dbus_connection,
                                                         "org.freedesktop.ConsoleKit",
                                                         "/org/freedesktop/ConsoleKit/Manager",
                                                         "org.freedesktop.ConsoleKit.Manager",
                                                         &connection_error);

                if (manager->priv->ck_proxy == NULL) {
                        g_propagate_error (error, connection_error);
                        is_connected = FALSE;
                        goto out;
                }
        }

#ifdef HAVE_OLD_UPOWER
        g_clear_object (&manager->priv->up_client);
        manager->priv->up_client = up_client_new ();
#endif

        is_connected = TRUE;

 out:
        if (!is_connected) {
                if (manager->priv->dbus_connection == NULL) {
                        if (manager->priv->bus_proxy != NULL) {
                                g_object_unref (manager->priv->bus_proxy);
                                manager->priv->bus_proxy = NULL;
                        }

                        if (manager->priv->ck_proxy != NULL) {
                                g_object_unref (manager->priv->ck_proxy);
                                manager->priv->ck_proxy = NULL;
                        }
                } else if (manager->priv->bus_proxy == NULL) {
                        if (manager->priv->ck_proxy != NULL) {
                                g_object_unref (manager->priv->ck_proxy);
                                manager->priv->ck_proxy = NULL;
                        }
                }
        }

        return is_connected;
}

static void
csm_consolekit_on_name_owner_changed (DBusGProxy    *bus_proxy,
                                      const char    *name,
                                      const char    *prev_owner,
                                      const char    *new_owner,
                                      CsmConsolekit *manager)
{
        if (name != NULL && strcmp (name, "org.freedesktop.ConsoleKit") != 0) {
                return;
        }

        g_clear_object (&manager->priv->ck_proxy);

        csm_consolekit_ensure_ck_connection (manager, NULL);

}

static void
csm_consolekit_init (CsmConsolekit *manager)
{
        GError *error;

        manager->priv = CSM_CONSOLEKIT_GET_PRIVATE (manager);

        error = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
        }
}

static void
csm_consolekit_free_dbus (CsmConsolekit *manager)
{
        g_clear_object (&manager->priv->bus_proxy);
        g_clear_object (&manager->priv->ck_proxy);
#ifdef HAVE_OLD_UPOWER
        g_clear_object (&manager->priv->up_client);
#endif

        if (manager->priv->dbus_connection != NULL) {
                DBusConnection *connection;
                connection = dbus_g_connection_get_connection (manager->priv->dbus_connection);
                dbus_connection_remove_filter (connection,
                                               csm_consolekit_dbus_filter,
                                               manager);

                dbus_g_connection_unref (manager->priv->dbus_connection);
                manager->priv->dbus_connection = NULL;
        }
}

static void
csm_consolekit_finalize (GObject *object)
{
        CsmConsolekit *manager;
        GObjectClass  *parent_class;

        manager = CSM_CONSOLEKIT (object);

        parent_class = G_OBJECT_CLASS (csm_consolekit_parent_class);

        csm_consolekit_free_dbus (manager);

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

static void
csm_consolekit_attempt_restart (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gboolean res;
        GError  *error;

        error = NULL;

        g_warning ("Attempting to restart using consolekit...");

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s", error->message);
                g_signal_emit_by_name (G_OBJECT (manager), "request-failed", NULL);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "Restart",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to restart system via consolekit: %s", error->message);
                g_signal_emit_by_name (G_OBJECT (manager), "request-failed", NULL);
                g_error_free (error);
        }
}

static void
csm_consolekit_attempt_stop (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gboolean res;
        GError  *error;

        error = NULL;

        g_warning ("Attempting to shutdown using consolekit...");

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s", error->message);
                g_signal_emit_by_name (G_OBJECT (manager), "request-failed", NULL);
                g_error_free (error);
                return;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "Stop",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Unable to stop system via consolekit: %s", error->message);
                g_signal_emit_by_name (G_OBJECT (manager), "request-failed", NULL);
                g_error_free (error);
        }
}

static gboolean
get_current_session_id (DBusConnection *connection,
                        char          **session_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        gboolean        ret;
        DBusMessageIter iter;
        const char     *value;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                CK_MANAGER_PATH,
                                                CK_MANAGER_INTERFACE,
                                                "GetCurrentSession");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to determine session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        if (session_id != NULL) {
                *session_id = g_strdup (value);
        }

        ret = TRUE;
 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static gboolean
get_seat_id_for_session (DBusConnection *connection,
                         const char     *session_id,
                         char          **seat_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        gboolean        ret;
        DBusMessageIter iter;
        const char     *value;

        ret = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "GetSeatId");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to determine seat: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &value);
        if (seat_id != NULL) {
                *seat_id = g_strdup (value);
        }

        ret = TRUE;
 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return ret;
}

static char *
get_current_seat_id (DBusConnection *connection)
{
        gboolean res;
        char    *session_id;
        char    *seat_id;

        session_id = NULL;
        seat_id = NULL;

        res = get_current_session_id (connection, &session_id);
        if (res) {
                res = get_seat_id_for_session (connection, session_id, &seat_id);
        }
        g_free (session_id);

        return seat_id;
}

static void
csm_consolekit_set_session_idle (CsmSystem *system,
                                 gboolean   is_idle)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gboolean        res;
        GError         *error;
        char           *session_id;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusError       dbus_error;
        DBusMessageIter iter;

        error = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return;
        }

        session_id = NULL;
        res = get_current_session_id (dbus_g_connection_get_connection (manager->priv->dbus_connection),
                                      &session_id);
        if (!res) {
                goto out;
        }


        g_debug ("Updating ConsoleKit idle status: %d", is_idle);
        message = dbus_message_new_method_call (CK_NAME,
                                                session_id,
                                                CK_SESSION_INTERFACE,
                                                "SetIdleHint");
        if (message == NULL) {
                g_debug ("Couldn't allocate the D-Bus message");
                return;
        }

        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_BOOLEAN, &is_idle);

        /* FIXME: use async? */
        dbus_error_init (&dbus_error);
        reply = dbus_connection_send_with_reply_and_block (dbus_g_connection_get_connection (manager->priv->dbus_connection),
                                                           message,
                                                           -1,
                                                           &dbus_error);
        dbus_message_unref (message);

        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (dbus_error_is_set (&dbus_error)) {
                g_debug ("%s raised:\n %s\n\n", dbus_error.name, dbus_error.message);
                dbus_error_free (&dbus_error);
        }

out:
        g_free (session_id);
}

static gboolean
seat_can_activate_sessions (DBusConnection *connection,
                            const char     *seat_id)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusMessageIter iter;
        gboolean        can_activate;

        can_activate = FALSE;
        reply = NULL;

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call (CK_NAME,
                                                seat_id,
                                                CK_SEAT_INTERFACE,
                                                "CanActivateSessions");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        g_warning ("Unable to activate session: %s", local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &can_activate);

 out:
        if (message != NULL) {
                dbus_message_unref (message);
        }
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        return can_activate;
}

static gboolean
csm_consolekit_can_switch_user (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        GError  *error;
        char    *seat_id;
        gboolean ret;

        error = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        seat_id = get_current_seat_id (dbus_g_connection_get_connection (manager->priv->dbus_connection));
        if (seat_id == NULL || seat_id[0] == '\0') {
                g_debug ("seat id is not set; can't switch sessions");
                return FALSE;
        }

        ret = seat_can_activate_sessions (dbus_g_connection_get_connection (manager->priv->dbus_connection),
                                          seat_id);
        g_free (seat_id);

        return ret;
}

static gboolean
csm_consolekit_can_restart (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gboolean res;
	gboolean can_restart;
        GError  *error;

        error = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "CanRestart",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_BOOLEAN, &can_restart,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Could not query CanRestart from ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

	return can_restart;
}

static gboolean
csm_consolekit_can_stop (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gboolean res;
	gboolean can_stop;
        GError  *error;

        error = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &error)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }

        res = dbus_g_proxy_call_with_timeout (manager->priv->ck_proxy,
                                              "CanStop",
                                              INT_MAX,
                                              &error,
                                              G_TYPE_INVALID,
                                              G_TYPE_BOOLEAN, &can_stop,
                                              G_TYPE_INVALID);

        if (!res) {
                g_warning ("Could not query CanStop from ConsoleKit: %s",
                           error->message);
                g_error_free (error);
                return FALSE;
        }
	return can_stop;
}

static gchar *
csm_consolekit_get_current_session_type (CsmConsolekit *manager)
{
        GError *gerror;
	DBusConnection *connection;
	DBusError error;
	DBusMessage *message = NULL;
	DBusMessage *reply = NULL;
	gchar *session_id;
	gchar *ret;
	DBusMessageIter iter;
	const char *value;

	session_id = NULL;
	ret = NULL;
        gerror = NULL;

        if (!csm_consolekit_ensure_ck_connection (manager, &gerror)) {
                g_warning ("Could not connect to ConsoleKit: %s",
                           gerror->message);
                g_error_free (gerror);
		goto out;
        }

	connection = dbus_g_connection_get_connection (manager->priv->dbus_connection);
	if (!get_current_session_id (connection, &session_id)) {
		goto out;
	}

	dbus_error_init (&error);
	message = dbus_message_new_method_call (CK_NAME,
						session_id,
						CK_SESSION_INTERFACE,
						"GetSessionType");
	if (message == NULL) {
		goto out;
	}

	reply = dbus_connection_send_with_reply_and_block (connection,
							   message,
							   -1,
							   &error);

	if (reply == NULL) {
		if (dbus_error_is_set (&error)) {
			g_warning ("Unable to determine session type: %s", error.message);
			dbus_error_free (&error);
		}
		goto out;
	}

	dbus_message_iter_init (reply, &iter);
	dbus_message_iter_get_basic (&iter, &value);
	ret = g_strdup (value);

out:
	if (message != NULL) {
		dbus_message_unref (message);
	}
	if (reply != NULL) {
		dbus_message_unref (reply);
	}
	g_free (session_id);

	return ret;
}

static gboolean
csm_consolekit_is_login_session (CsmSystem *system)
{
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);
        char *session_type;
        gboolean ret;

        session_type = csm_consolekit_get_current_session_type (consolekit);

        ret = (g_strcmp0 (session_type, CSM_CONSOLEKIT_SESSION_TYPE_LOGIN_WINDOW) == 0);

        g_free (session_type);

        return ret;
}

static gboolean
csm_consolekit_can_suspend (CsmSystem *system)
{
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);

#ifdef HAVE_OLD_UPOWER
        return up_client_get_can_suspend (consolekit->priv->up_client);
#else
        return FALSE;
#endif
}

static gboolean
csm_consolekit_can_hibernate (CsmSystem *system)
{
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);

#ifdef HAVE_OLD_UPOWER
        return up_client_get_can_hibernate (consolekit->priv->up_client);
#else
        return FALSE;
#endif

}

static void
csm_consolekit_suspend (CsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);
        GError *error = NULL;
        gboolean ret;

        ret = up_client_suspend_sync (consolekit->priv->up_client, NULL, &error);
        if (!ret) {
                g_warning ("Unexpected suspend failure: %s", error->message);
                g_error_free (error);
        }
#endif
}

static void
csm_consolekit_hibernate (CsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);
        GError *error = NULL;
        gboolean ret;

        ret = up_client_hibernate_sync (consolekit->priv->up_client, NULL, &error);
        if (!ret) {
                g_warning ("Unexpected hibernate failure: %s", error->message);
                g_error_free (error);
        }
#endif
}

static void
csm_consolekit_add_inhibitor (CsmSystem        *system,
                              const gchar      *id,
                              CsmInhibitorFlag  flag)
{
}

static void
csm_consolekit_remove_inhibitor (CsmSystem   *system,
                                 const gchar *id)
{
}

static void
csm_consolekit_system_init (CsmSystemInterface *iface)
{
        iface->can_switch_user = csm_consolekit_can_switch_user;
        iface->can_stop = csm_consolekit_can_stop;
        iface->can_restart = csm_consolekit_can_restart;
        iface->can_suspend = csm_consolekit_can_suspend;
        iface->can_hibernate = csm_consolekit_can_hibernate;
        iface->attempt_stop = csm_consolekit_attempt_stop;
        iface->attempt_restart = csm_consolekit_attempt_restart;
        iface->suspend = csm_consolekit_suspend;
        iface->hibernate = csm_consolekit_hibernate;
        iface->set_session_idle = csm_consolekit_set_session_idle;
        iface->is_login_session = csm_consolekit_is_login_session;
        iface->add_inhibitor = csm_consolekit_add_inhibitor;
        iface->remove_inhibitor = csm_consolekit_remove_inhibitor;
}

CsmConsolekit *
csm_consolekit_new (void)
{
        CsmConsolekit *manager;

        manager = g_object_new (CSM_TYPE_CONSOLEKIT, NULL);

        return manager;
}
