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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#ifdef HAVE_OLD_UPOWER
#define UPOWER_ENABLE_DEPRECATED 1
#include <upower.h>
#endif

#include "csm-system.h"
#include "csm-consolekit.h"

#define CK_NAME      "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH         "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE    CK_NAME ".Manager"
#define CK_SEAT_INTERFACE       CK_NAME ".Seat"
#define CK_SESSION_INTERFACE    CK_NAME ".Session"


struct _CsmConsolekitPrivate
{
        GDBusProxy      *ck_proxy;
        GDBusProxy      *ck_session_proxy;
#ifdef HAVE_OLD_UPOWER
        UpClient        *up_client;
#endif
        char            *session_id;
        gchar           *session_path;

        GSList          *inhibitors;
        gint             inhibit_fd;

        gboolean         is_active;

        gint             delay_inhibit_fd;
        gboolean         prepare_for_shutdown_expected;
};

enum {
        PROP_0,
        PROP_ACTIVE
};

static void csm_consolekit_system_init (CsmSystemInterface *iface);

G_DEFINE_TYPE_WITH_CODE (CsmConsolekit, csm_consolekit, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (CSM_TYPE_SYSTEM,
                                                csm_consolekit_system_init))

static void
drop_system_inhibitor (CsmConsolekit *manager)
{
        if (manager->priv->inhibit_fd != -1) {
                g_debug ("CsmConsolekit: Dropping system inhibitor");
                close (manager->priv->inhibit_fd);
                manager->priv->inhibit_fd = -1;
        }
}

static void
drop_delay_inhibitor (CsmConsolekit *manager)
{
        if (manager->priv->delay_inhibit_fd != -1) {
                g_debug ("CsmConsolekit: Dropping delay inhibitor");
                close (manager->priv->delay_inhibit_fd);
                manager->priv->delay_inhibit_fd = -1;
        }
}

static void
csm_consolekit_finalize (GObject *object)
{
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (object);

        g_clear_object (&consolekit->priv->ck_proxy);
        g_clear_object (&consolekit->priv->ck_session_proxy);
        free (consolekit->priv->session_id);
        g_free (consolekit->priv->session_path);

        if (consolekit->priv->inhibitors != NULL) {
                g_slist_free_full (consolekit->priv->inhibitors, g_free);
        }
        drop_system_inhibitor (consolekit);
        drop_delay_inhibitor (consolekit);

#ifdef HAVE_OLD_UPOWER
        g_clear_object (&manager->priv->up_client);
#endif

        G_OBJECT_CLASS (csm_consolekit_parent_class)->finalize (object);
}

static void
csm_consolekit_set_property (GObject      *object,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
        CsmConsolekit *self = CSM_CONSOLEKIT (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                self->priv->is_active = g_value_get_boolean (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
csm_consolekit_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
        CsmConsolekit *self = CSM_CONSOLEKIT (object);

        switch (prop_id) {
        case PROP_ACTIVE:
                g_value_set_boolean (value, self->priv->is_active);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_consolekit_class_init (CsmConsolekitClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        object_class->get_property = csm_consolekit_get_property;
        object_class->set_property = csm_consolekit_set_property;
        object_class->finalize = csm_consolekit_finalize;

        g_object_class_override_property (object_class, PROP_ACTIVE, "active");

        g_type_class_add_private (manager_class, sizeof (CsmConsolekitPrivate));
}

static void ck_session_proxy_signal_cb (GDBusProxy  *proxy,
                                        const gchar *sender_name,
                                        const gchar *signal_name,
                                        GVariant    *parameters,
                                        gpointer     user_data);

static void ck_proxy_signal_cb (GDBusProxy  *proxy,
                                const gchar *sender_name,
                                const gchar *signal_name,
                                GVariant    *parameters,
                                gpointer     user_data);

static void
ck_pid_get_session (CsmConsolekit *manager,
                    pid_t          pid,
                    gchar        **session_id)
{
        GVariant *res;

        *session_id = NULL;

        if (pid < 0) {
                g_warning ("Calling GetSessionForUnixProcess failed."
                           "Invalid pid.");
                return;
        }

        res = g_dbus_proxy_call_sync (manager->priv->ck_proxy,
                                      "GetSessionForUnixProcess",
                                      g_variant_new ("(u)", pid),
                                      0,
                                      -1,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling GetSessionForUnixProcess failed."
                           "Check that ConsoleKit is properly installed.");
                return;
        }

        g_variant_get (res, "(o)", session_id);
        g_variant_unref (res);
}

static void
csm_consolekit_init (CsmConsolekit *manager)
{
        GError *error = NULL;
        GDBusConnection *bus;
        GVariant *res;

        manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                     CSM_TYPE_CONSOLEKIT,
                                                     CsmConsolekitPrivate);

        manager->priv->inhibit_fd = -1;
        manager->priv->delay_inhibit_fd = -1;

        bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (bus == NULL)
                g_error ("Failed to connect to system bus: %s",
                         error->message);
        manager->priv->ck_proxy =
                g_dbus_proxy_new_sync (bus,
                                       0,
                                       NULL,
                                       CK_NAME,
                                       CK_MANAGER_PATH,
                                       CK_MANAGER_INTERFACE,
                                       NULL,
                                       &error);
        if (manager->priv->ck_proxy == NULL) {
                g_warning ("Failed to connect to consolekit: %s",
                           error->message);
                g_clear_error (&error);
        }

        g_signal_connect (manager->priv->ck_proxy, "g-signal",
                          G_CALLBACK (ck_proxy_signal_cb), manager);

        ck_pid_get_session (manager, getpid (), &manager->priv->session_id);

        if (manager->priv->session_id == NULL) {
                g_warning ("Could not get session id for session. Check that ConsoleKit is "
                           "properly installed.");
                return;
        }

        /* in ConsoleKit, the session id is the session path */
        manager->priv->session_path = g_strdup (manager->priv->session_id);

        manager->priv->ck_session_proxy =
                g_dbus_proxy_new_sync (bus,
                                       0,
                                       NULL,
                                       CK_NAME,
                                       manager->priv->session_path,
                                       CK_SESSION_INTERFACE,
                                       NULL,
                                       &error);
        if (manager->priv->ck_proxy == NULL) {
                g_warning ("Failed to connect to consolekit session: %s",
                           error->message);
                g_clear_error (&error);
        }

        g_signal_connect (manager->priv->ck_session_proxy, "g-signal",
                          G_CALLBACK (ck_session_proxy_signal_cb), manager);

#ifdef HAVE_OLD_UPOWER
        g_clear_object (&manager->priv->up_client);
        manager->priv->up_client = up_client_new ();
#endif

        g_object_unref (bus);
}

static void
emit_restart_complete (CsmConsolekit *manager,
                       GError     *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (CSM_SYSTEM_ERROR,
                                                  CSM_SYSTEM_ERROR_RESTARTING,
                                                  error->message);
        }

        g_signal_emit_by_name (G_OBJECT (manager),
                               "request_completed", call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
emit_stop_complete (CsmConsolekit *manager,
                    GError     *error)
{
        GError *call_error;

        call_error = NULL;

        if (error != NULL) {
                call_error = g_error_new_literal (CSM_SYSTEM_ERROR,
                                                  CSM_SYSTEM_ERROR_STOPPING,
                                                  error->message);
        }

        g_signal_emit_by_name (G_OBJECT (manager),
                               "request_completed", call_error);

        if (call_error != NULL) {
                g_error_free (call_error);
        }
}

static void
restart_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        CsmConsolekit *manager = user_data;
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to restart system: %s", error->message);
                emit_restart_complete (manager, error);
                g_error_free (error);
        } else {
                emit_restart_complete (manager, NULL);
                g_variant_unref (res);
        }
}

static void
csm_consolekit_attempt_restart (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);

        /* Use Restart instead of Reboot because it will work on
         * both CK and CK2 */
        g_dbus_proxy_call (manager->priv->ck_proxy,
                           "Restart",
                           g_variant_new ("()"),
                           0,
                           G_MAXINT,
                           NULL,
                           restart_done,
                           manager);
}

static void
stop_done (GObject      *source,
           GAsyncResult *result,
           gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        CsmConsolekit *manager = user_data;
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to stop system: %s", error->message);
                emit_stop_complete (manager, error);
                g_error_free (error);
        } else {
                emit_stop_complete (manager, NULL);
                g_variant_unref (res);
        }
}

static void
csm_consolekit_attempt_stop (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);

        /* Use Stop insetad of PowerOff because it will work with
         * Ck and CK2. */
        g_dbus_proxy_call (manager->priv->ck_proxy,
                           "Stop",
                           g_variant_new ("()"),
                           0,
                           G_MAXINT,
                           NULL,
                           stop_done,
                           manager);
}

static void
csm_consolekit_set_session_idle (CsmSystem *system,
                              gboolean   is_idle)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);

        g_debug ("Updating consolekit idle status: %d", is_idle);
        g_dbus_proxy_call_sync (manager->priv->ck_session_proxy,
                                "SetIdleHint",
                                g_variant_new ("(b)", is_idle),
                                0,
                                G_MAXINT,
                                NULL, NULL);
}

static void
ck_session_get_seat (CsmConsolekit *manager,
                     gchar        **seat)
{
        GVariant *res;

        *seat = NULL;

        res = g_dbus_proxy_call_sync (manager->priv->ck_session_proxy,
                                      "GetSeatId",
                                      g_variant_new ("()"),
                                      0,
                                      -1,
                                      NULL, NULL);
        if (!res) {
                g_warning ("CsmConsoleKit: Calling GetSeatId failed.");
                return;
        }

        g_variant_get (res, "(o)", seat);
        g_variant_unref (res);
}

/* returns -1 on failure
 *          0 seat is multi-session
 *          1 seat is not multi-session
 */
static gint
ck_seat_can_multi_session (CsmConsolekit *manager,
                           const gchar   *seat)
{
        GDBusConnection *bus;
        GVariant *res;
        gboolean  can_activate;


        bus = g_dbus_proxy_get_connection (manager->priv->ck_proxy);
        res = g_dbus_connection_call_sync (bus,
                                           CK_NAME,
                                           seat,
                                           CK_SEAT_INTERFACE,
                                           "CanActivateSessions",
                                           g_variant_new ("()"),
                                           G_VARIANT_TYPE_BOOLEAN,
                                           0,
                                           -1,
                                           NULL, NULL);
        if (!res) {
                g_warning ("CsmConsoleKit: Calling GetSeatId failed.");
                return -1;
        }

        g_variant_get (res, "(b)", &can_activate);
        g_variant_unref (res);

        return can_activate == TRUE ? 1 : 0;
}

static gboolean
csm_consolekit_can_switch_user (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gchar *seat;
        gint ret;

        ck_session_get_seat (manager, &seat);
        ret = ck_seat_can_multi_session (manager, seat);
        free (seat);

        return ret > 0;
}

static gboolean
csm_consolekit_can_restart (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        GVariant *res;
        gboolean can_restart;

        res = g_dbus_proxy_call_sync (manager->priv->ck_proxy,
                                      "CanRestart",
                                      g_variant_new ("()"),
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanRestart failed. Check that ConsoleKit is "
                           "properly installed.");
                return FALSE;
        }

        g_variant_get (res, "(b)", &can_restart);
        g_variant_unref (res);

        return can_restart;
}

static gboolean
csm_consolekit_can_stop (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        GVariant *res;
        gboolean can_stop;

        res = g_dbus_proxy_call_sync (manager->priv->ck_proxy,
                                      "CanStop",
                                      g_variant_new ("()"),
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanStop failed. Check that ConsoleKit is "
                           "properly installed.");
                return FALSE;
        }

        g_variant_get (res, "(b)", &can_stop);
        g_variant_unref (res);

        return can_stop;
}

/* returns -1 on failure, 0 on success */
static gint
ck_session_get_class (CsmConsolekit *manager,
                      gchar        **session_class)
{
        GVariant *res;

        *session_class = NULL;

        res = g_dbus_proxy_call_sync (manager->priv->ck_session_proxy,
                                      "GetSessionClass",
                                      g_variant_new ("()"),
                                      0,
                                      -1,
                                      NULL, NULL);
        if (!res) {
                g_warning ("CsmConsoleKit: Calling GetSessionClass failed.");
                return -1;
        }

        g_variant_get (res, "(s)", session_class);
        g_variant_unref (res);

        return 0;
}

static gboolean
csm_consolekit_is_login_session (CsmSystem *system)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        int res;
        gboolean ret;
        gchar *session_class = NULL;

        ret = FALSE;

        if (manager->priv->session_id == NULL) {
                return ret;
        }

        res = ck_session_get_class (manager, &session_class);
        if (res < 0) {
                g_warning ("Could not get session class: %s", strerror (-res));
                return FALSE;
        }
        ret = (g_strcmp0 (session_class, "greeter") == 0);
        g_free (session_class);

        return ret;
}

static gboolean
csm_consolekit_can_suspend (CsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);
        return up_client_get_can_suspend (consolekit->priv->up_client);
#else
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gchar *rv;
        GVariant *res;
        gboolean can_suspend;

        res = g_dbus_proxy_call_sync (manager->priv->ck_proxy,
                                      "CanSuspend",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanSuspend failed. Check that ConsoleKit is "
                           "properly installed.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_suspend = g_strcmp0 (rv, "yes") == 0 ||
                      g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_suspend;
#endif
}

static gboolean
csm_consolekit_can_hibernate (CsmSystem *system)
{
#ifdef HAVE_OLD_UPOWER
        CsmConsolekit *consolekit = CSM_CONSOLEKIT (system);
        return up_client_get_can_hibernate (consolekit->priv->up_client);
#else
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        gchar *rv;
        GVariant *res;
        gboolean can_hibernate;

        res = g_dbus_proxy_call_sync (manager->priv->ck_proxy,
                                      "CanHibernate",
                                      NULL,
                                      0,
                                      G_MAXINT,
                                      NULL,
                                      NULL);
        if (!res) {
                g_warning ("Calling CanHibernate failed. Check that ConsoleKit is "
                           "properly installed.");
                return FALSE;
        }

        g_variant_get (res, "(s)", &rv);
        g_variant_unref (res);

        can_hibernate = g_strcmp0 (rv, "yes") == 0 ||
                        g_strcmp0 (rv, "challenge") == 0;

        g_free (rv);

        return can_hibernate;
#endif
}

static void
suspend_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to suspend system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
}

static void
hibernate_done (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        GError *error = NULL;
        GVariant *res;

        res = g_dbus_proxy_call_finish (proxy, result, &error);

        if (!res) {
                g_warning ("Unable to hibernate system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_unref (res);
        }
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
#else
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);

        g_dbus_proxy_call (manager->priv->ck_proxy,
                           "Suspend",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           suspend_done,
                           manager);
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
#else
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);

        g_dbus_proxy_call (manager->priv->ck_proxy,
                           "Hibernate",
                           g_variant_new ("(b)", TRUE),
                           0,
                           G_MAXINT,
                           NULL,
                           hibernate_done,
                           manager);
#endif
}

static void
inhibit_done (GObject      *source,
              GAsyncResult *result,
              gpointer      user_data)
{
        GDBusProxy *proxy = G_DBUS_PROXY (source);
        CsmConsolekit *manager = CSM_CONSOLEKIT (user_data);
        GError *error = NULL;
        GVariant *res;
        GUnixFDList *fd_list = NULL;
        gint idx;

        res = g_dbus_proxy_call_with_unix_fd_list_finish (proxy, &fd_list, result, &error);

        if (!res) {
                g_warning ("Unable to inhibit system: %s", error->message);
                g_error_free (error);
        } else {
                g_variant_get (res, "(h)", &idx);
                manager->priv->inhibit_fd = g_unix_fd_list_get (fd_list, idx, &error);
                if (manager->priv->inhibit_fd == -1) {
                        g_warning ("Failed to receive system inhibitor fd: %s", error->message);
                        g_error_free (error);
                }
                g_debug ("System inhibitor fd is %d", manager->priv->inhibit_fd);
                g_object_unref (fd_list);
                g_variant_unref (res);
        }

        if (manager->priv->inhibitors == NULL) {
                drop_system_inhibitor (manager);
        }
}

static void
csm_consolekit_add_inhibitor (CsmSystem        *system,
                           const gchar      *id,
                           CsmInhibitorFlag  flag)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);

        if ((flag & CSM_INHIBITOR_FLAG_SUSPEND) == 0)
                return;

        if (manager->priv->inhibitors == NULL) {
                g_debug ("Adding system inhibitor");
                g_dbus_proxy_call_with_unix_fd_list (manager->priv->ck_proxy,
                                                     "Inhibit",
                                                     g_variant_new ("(ssss)",
                                                                    "sleep:shutdown",
                                                                    g_get_user_name (),
                                                                    "user session inhibited",
                                                                    "block"),
                                                     0,
                                                     G_MAXINT,
                                                     NULL,
                                                     NULL,
                                                     inhibit_done,
                                                     manager);
        }
        manager->priv->inhibitors = g_slist_prepend (manager->priv->inhibitors, g_strdup (id));
}

static void
csm_consolekit_remove_inhibitor (CsmSystem   *system,
                              const gchar *id)
{
        CsmConsolekit *manager = CSM_CONSOLEKIT (system);
        GSList *l;

        l = g_slist_find_custom (manager->priv->inhibitors, id, (GCompareFunc)g_strcmp0);
        if (l == NULL)
                return;

        g_free (l->data);
        manager->priv->inhibitors = g_slist_delete_link (manager->priv->inhibitors, l);
        if (manager->priv->inhibitors == NULL) {
                drop_system_inhibitor (manager);
        }
}

static gboolean
csm_consolekit_is_last_session_for_user (CsmSystem *system)
{
        return FALSE;
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
        iface->is_last_session_for_user = csm_consolekit_is_last_session_for_user;
}

CsmConsolekit *
csm_consolekit_new (void)
{
        CsmConsolekit *manager;

        manager = g_object_new (CSM_TYPE_CONSOLEKIT, NULL);

        return manager;
}

static void
ck_proxy_signal_cb (GDBusProxy  *proxy,
                    const gchar *sender_name,
                    const gchar *signal_name,
                    GVariant    *parameters,
                    gpointer     user_data)
{
        CsmConsolekit *consolekit = user_data;
        gboolean is_about_to_shutdown;

        g_debug ("CsmConsolekit: received ConsoleKit signal: %s", signal_name);

        if (g_strcmp0 (signal_name, "PrepareForShutdown") != 0) {
                g_debug ("CsmConsolekit: ignoring %s signal", signal_name);
                return;
        }

        g_variant_get (parameters, "(b)", &is_about_to_shutdown);
        if (!is_about_to_shutdown) {
                g_debug ("CsmConsolekit: ignoring %s signal since about-to-shutdown is FALSE", signal_name);
                return;
        }

        if (consolekit->priv->prepare_for_shutdown_expected) {
                g_debug ("CsmConsolekit: shutdown successfully prepared");
                g_signal_emit_by_name (consolekit, "shutdown-prepared", TRUE);
                consolekit->priv->prepare_for_shutdown_expected = FALSE;
        }
}

static void
ck_session_proxy_signal_cb (GDBusProxy  *proxy,
                            const gchar *sender_name,
                            const gchar *signal_name,
                            GVariant    *parameters,
                            gpointer     user_data)
{
        CsmConsolekit *consolekit = user_data;
        gboolean is_active;

        g_debug ("CsmConsolekit: received ConsoleKit signal: %s", signal_name);

        if (g_strcmp0 (signal_name, "ActiveChanged") != 0) {
                g_debug ("CsmConsolekit: ignoring %s signal", signal_name);
                return;
        }

        g_variant_get (parameters, "(b)", &is_active);
        if (consolekit->priv->is_active != is_active) {
                g_debug ("CsmConsolekit: session state changed");
                consolekit->priv->is_active = is_active;
                g_object_notify (G_OBJECT (consolekit), "active");
        }
}
