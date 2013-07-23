/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Novell, Inc.
 * Copyright (C) 2008 Red Hat, Inc.
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

#include "config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "csm-xsmp-client.h"
#include "csm-marshal.h"

#include "csm-util.h"
#include "csm-autostart-app.h"
#include "csm-icon-names.h"
#include "csm-manager.h"

#define CsmDesktopFile "_CSM_DesktopFile"

#define CSM_XSMP_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_XSMP_CLIENT, CsmXSMPClientPrivate))

struct CsmXSMPClientPrivate
{

        SmsConn    conn;
        IceConn    ice_connection;

        guint      watch_id;

        char      *description;
        GPtrArray *props;

        /* SaveYourself state */
        int        current_save_yourself;
        int        next_save_yourself;
        guint      next_save_yourself_allow_interact : 1;
};

enum {
        PROP_0,
        PROP_ICE_CONNECTION
};

enum {
        REGISTER_REQUEST,
        LOGOUT_REQUEST,
        LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (CsmXSMPClient, csm_xsmp_client, CSM_TYPE_CLIENT)

static gboolean
client_iochannel_watch (GIOChannel    *channel,
                        GIOCondition   condition,
                        CsmXSMPClient *client)
{
        gboolean keep_going;

        g_object_ref (client);
        switch (IceProcessMessages (client->priv->ice_connection, NULL, NULL)) {
        case IceProcessMessagesSuccess:
                keep_going = TRUE;
                break;

        case IceProcessMessagesIOError:
                g_debug ("CsmXSMPClient: IceProcessMessagesIOError on '%s'", client->priv->description);
                csm_client_set_status (CSM_CLIENT (client), CSM_CLIENT_FAILED);
                /* Emitting "disconnected" will eventually cause
                 * IceCloseConnection() to be called.
                 */
                csm_client_disconnected (CSM_CLIENT (client));
                keep_going = FALSE;
                break;

        case IceProcessMessagesConnectionClosed:
                g_debug ("CsmXSMPClient: IceProcessMessagesConnectionClosed on '%s'",
                         client->priv->description);
                client->priv->ice_connection = NULL;
                keep_going = FALSE;
                break;

        default:
                g_assert_not_reached ();
        }
        g_object_unref (client);

        return keep_going;
}

static SmProp *
find_property (CsmXSMPClient *client,
               const char    *name,
               int           *index)
{
        SmProp *prop;
        int i;

        for (i = 0; i < client->priv->props->len; i++) {
                prop = client->priv->props->pdata[i];

                if (!strcmp (prop->name, name)) {
                        if (index) {
                                *index = i;
                        }
                        return prop;
                }
        }

        return NULL;
}

static void
set_description (CsmXSMPClient *client)
{
        SmProp     *prop;
        const char *id;

        prop = find_property (client, SmProgram, NULL);
        id = csm_client_peek_startup_id (CSM_CLIENT (client));

        g_free (client->priv->description);
        if (prop) {
                client->priv->description = g_strdup_printf ("%p [%.*s %s]",
                                                             client,
                                                             prop->vals[0].length,
                                                             (char *)prop->vals[0].value,
                                                             id);
        } else if (id != NULL) {
                client->priv->description = g_strdup_printf ("%p [%s]", client, id);
        } else {
                client->priv->description = g_strdup_printf ("%p", client);
        }
}

static void
setup_connection (CsmXSMPClient *client)
{
        GIOChannel    *channel;
        int            fd;

        g_debug ("CsmXSMPClient: Setting up new connection");

        fd = IceConnectionNumber (client->priv->ice_connection);
        fcntl (fd, F_SETFD, fcntl (fd, F_GETFD, 0) | FD_CLOEXEC);
        channel = g_io_channel_unix_new (fd);
        client->priv->watch_id = g_io_add_watch (channel,
                                                 G_IO_IN | G_IO_ERR,
                                                 (GIOFunc)client_iochannel_watch,
                                                 client);
        g_io_channel_unref (channel);

        set_description (client);

        g_debug ("CsmXSMPClient: New client '%s'", client->priv->description);
}

static GObject *
csm_xsmp_client_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        CsmXSMPClient *client;

        client = CSM_XSMP_CLIENT (G_OBJECT_CLASS (csm_xsmp_client_parent_class)->constructor (type,
                                                                                              n_construct_properties,
                                                                                              construct_properties));
        setup_connection (client);

        return G_OBJECT (client);
}

static void
csm_xsmp_client_init (CsmXSMPClient *client)
{
        client->priv = CSM_XSMP_CLIENT_GET_PRIVATE (client);

        client->priv->props = g_ptr_array_new ();
        client->priv->current_save_yourself = -1;
        client->priv->next_save_yourself = -1;
        client->priv->next_save_yourself_allow_interact = FALSE;
}


static void
delete_property (CsmXSMPClient *client,
                 const char    *name)
{
        int     index;
        SmProp *prop;

        prop = find_property (client, name, &index);
        if (!prop) {
                return;
        }

#if 0
        /* This is wrong anyway; we can't unconditionally run the current
         * discard command; if this client corresponds to a CsmAppResumed,
         * and the current discard command is identical to the app's
         * discard_command, then we don't run the discard command now,
         * because that would delete a saved state we may want to resume
         * again later.
         */
        if (!strcmp (name, SmDiscardCommand)) {
                csm_client_run_discard (CSM_CLIENT (client));
        }
#endif

        g_ptr_array_remove_index_fast (client->priv->props, index);
        SmFreeProperty (prop);
}


static void
debug_print_property (SmProp *prop)
{
        GString *tmp;
        int      i;

        switch (prop->type[0]) {
        case 'C': /* CARD8 */
                g_debug ("CsmXSMPClient:   %s = %d", prop->name, *(unsigned char *)prop->vals[0].value);
                break;

        case 'A': /* ARRAY8 */
                g_debug ("CsmXSMPClient:   %s = '%s'", prop->name, (char *)prop->vals[0].value);
                break;

        case 'L': /* LISTofARRAY8 */
                tmp = g_string_new (NULL);
                for (i = 0; i < prop->num_vals; i++) {
                        g_string_append_printf (tmp, "'%.*s' ", prop->vals[i].length,
                                                (char *)prop->vals[i].value);
                }
                g_debug ("CsmXSMPClient:   %s = %s", prop->name, tmp->str);
                g_string_free (tmp, TRUE);
                break;

        default:
                g_debug ("CsmXSMPClient:   %s = ??? (%s)", prop->name, prop->type);
                break;
        }
}


static void
set_properties_callback (SmsConn     conn,
                         SmPointer   manager_data,
                         int         num_props,
                         SmProp    **props)
{
        CsmXSMPClient *client = manager_data;
        int            i;

        g_debug ("CsmXSMPClient: Set properties from client '%s'", client->priv->description);

        for (i = 0; i < num_props; i++) {
                delete_property (client, props[i]->name);
                g_ptr_array_add (client->priv->props, props[i]);

                debug_print_property (props[i]);

                if (!strcmp (props[i]->name, SmProgram))
                        set_description (client);
        }

        free (props);

}

static void
delete_properties_callback (SmsConn     conn,
                            SmPointer   manager_data,
                            int         num_props,
                            char      **prop_names)
{
        CsmXSMPClient *client = manager_data;
        int i;

        g_debug ("CsmXSMPClient: Delete properties from '%s'", client->priv->description);

        for (i = 0; i < num_props; i++) {
                delete_property (client, prop_names[i]);

                g_debug ("  %s", prop_names[i]);
        }

        free (prop_names);
}

static void
get_properties_callback (SmsConn   conn,
                         SmPointer manager_data)
{
        CsmXSMPClient *client = manager_data;

        g_debug ("CsmXSMPClient: Get properties request from '%s'", client->priv->description);

        SmsReturnProperties (conn,
                             client->priv->props->len,
                             (SmProp **)client->priv->props->pdata);
}

static char *
prop_to_command (SmProp *prop)
{
        GString *str;
        int i, j;
        gboolean need_quotes;

        str = g_string_new (NULL);
        for (i = 0; i < prop->num_vals; i++) {
                char *val = prop->vals[i].value;

                need_quotes = FALSE;
                for (j = 0; j < prop->vals[i].length; j++) {
                        if (!g_ascii_isalnum (val[j]) && !strchr ("-_=:./", val[j])) {
                                need_quotes = TRUE;
                                break;
                        }
                }

                if (i > 0) {
                        g_string_append_c (str, ' ');
                }

                if (!need_quotes) {
                        g_string_append_printf (str,
                                                "%.*s",
                                                prop->vals[i].length,
                                                (char *)prop->vals[i].value);
                } else {
                        g_string_append_c (str, '\'');
                        while (val < (char *)prop->vals[i].value + prop->vals[i].length) {
                                if (*val == '\'') {
                                        g_string_append (str, "'\''");
                                } else {
                                        g_string_append_c (str, *val);
                                }
                                val++;
                        }
                        g_string_append_c (str, '\'');
                }
        }

        return g_string_free (str, FALSE);
}

static char *
xsmp_get_restart_command (CsmClient *client)
{
        SmProp *prop;

        prop = find_property (CSM_XSMP_CLIENT (client), SmRestartCommand, NULL);

        if (!prop || strcmp (prop->type, SmLISTofARRAY8) != 0) {
                return NULL;
        }

        return prop_to_command (prop);
}

static char *
xsmp_get_discard_command (CsmClient *client)
{
        SmProp *prop;

        prop = find_property (CSM_XSMP_CLIENT (client), SmDiscardCommand, NULL);

        if (!prop || strcmp (prop->type, SmLISTofARRAY8) != 0) {
                return NULL;
        }

        return prop_to_command (prop);
}

static void
do_save_yourself (CsmXSMPClient *client,
                  int            save_type,
                  gboolean       allow_interact)
{
        g_assert (client->priv->conn != NULL);

        if (client->priv->next_save_yourself != -1) {
                /* Either we're currently doing a shutdown and there's a checkpoint
                 * queued after it, or vice versa. Either way, the new SaveYourself
                 * is redundant.
                 */
                g_debug ("CsmXSMPClient:   skipping redundant SaveYourself for '%s'",
                         client->priv->description);
        } else if (client->priv->current_save_yourself != -1) {
                g_debug ("CsmXSMPClient:   queuing new SaveYourself for '%s'",
                         client->priv->description);
                client->priv->next_save_yourself = save_type;
                client->priv->next_save_yourself_allow_interact = allow_interact;
        } else {
                client->priv->current_save_yourself = save_type;
                /* make sure we don't have anything queued */
                client->priv->next_save_yourself = -1;
                client->priv->next_save_yourself_allow_interact = FALSE;

                switch (save_type) {
                case SmSaveLocal:
                        /* Save state */
                        SmsSaveYourself (client->priv->conn,
                                         SmSaveLocal,
                                         FALSE,
                                         SmInteractStyleNone,
                                         FALSE);
                        break;

                default:
                        /* Logout */
                        if (!allow_interact) {
                                SmsSaveYourself (client->priv->conn,
                                                 save_type, /* save type */
                                                 TRUE, /* shutdown */
                                                 SmInteractStyleNone, /* interact style */
                                                 TRUE); /* fast */
                        } else {
                                SmsSaveYourself (client->priv->conn,
                                                 save_type, /* save type */
                                                 TRUE, /* shutdown */
                                                 SmInteractStyleAny, /* interact style */
                                                 FALSE /* fast */);
                        }
                        break;
                }
        }
}

static void
xsmp_save_yourself_phase2 (CsmClient *client)
{
        CsmXSMPClient *xsmp = (CsmXSMPClient *) client;

        g_debug ("CsmXSMPClient: xsmp_save_yourself_phase2 ('%s')", xsmp->priv->description);

        SmsSaveYourselfPhase2 (xsmp->priv->conn);
}

static void
xsmp_interact (CsmClient *client)
{
        CsmXSMPClient *xsmp = (CsmXSMPClient *) client;

        g_debug ("CsmXSMPClient: xsmp_interact ('%s')", xsmp->priv->description);

        SmsInteract (xsmp->priv->conn);
}

static gboolean
xsmp_cancel_end_session (CsmClient *client,
                         GError   **error)
{
        CsmXSMPClient *xsmp = (CsmXSMPClient *) client;

        g_debug ("CsmXSMPClient: xsmp_cancel_end_session ('%s')", xsmp->priv->description);

        if (xsmp->priv->conn == NULL) {
                g_set_error (error,
                             CSM_CLIENT_ERROR,
                             CSM_CLIENT_ERROR_NOT_REGISTERED,
                             "Client is not registered");
                return FALSE;
        }

        SmsShutdownCancelled (xsmp->priv->conn);

        /* reset the state */
        xsmp->priv->current_save_yourself = -1;
        xsmp->priv->next_save_yourself = -1;
        xsmp->priv->next_save_yourself_allow_interact = FALSE;

        return TRUE;
}

static char *
get_desktop_file_path (CsmXSMPClient *client)
{
        SmProp     *prop;
        char       *desktop_file_path = NULL;
        const char *program_name;

        /* XSMP clients using egcsmclient defines a special property
         * pointing to their respective desktop entry file */
        prop = find_property (client, CsmDesktopFile, NULL);

        if (prop) {
                GFile *file = g_file_new_for_uri (prop->vals[0].value);
                desktop_file_path = g_file_get_path (file);
                g_object_unref (file);
                goto out;
        }

        /* If we can't get desktop file from CsmDesktopFile then we
         * try to find the desktop file from its program name */
        prop = find_property (client, SmProgram, NULL);

        if (!prop) {
                goto out;
        }

        program_name = prop->vals[0].value;
        desktop_file_path =
                csm_util_find_desktop_file_for_app_name (program_name,
                                                         TRUE, FALSE);

out:
        g_debug ("CsmXSMPClient: desktop file for client %s is %s",
                 csm_client_peek_id (CSM_CLIENT (client)),
                 desktop_file_path ? desktop_file_path : "(null)");

        return desktop_file_path;
}

static void
set_desktop_file_keys_from_client (CsmClient *client,
                                   GKeyFile  *keyfile)
{
        SmProp     *prop;
        const char *name;
        char       *comment;

        prop = find_property (CSM_XSMP_CLIENT (client), SmProgram, NULL);
        if (prop) {
                name = prop->vals[0].value;
        } else {
                /* It'd be really surprising to reach this code: if we're here,
                 * then the XSMP client already has set several XSMP
                 * properties. But it could still be that SmProgram is not set.
                 */
                name = _("Remembered Application");
        }

        comment = g_strdup_printf ("Client %s which was automatically saved",
                                   csm_client_peek_startup_id (client));

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_NAME,
                               name);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_COMMENT,
                               comment);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_ICON,
                               CSM_ICON_XSMP_DEFAULT);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_TYPE,
                               "Application");

        g_key_file_set_boolean (keyfile,
                                G_KEY_FILE_DESKTOP_GROUP,
                                G_KEY_FILE_DESKTOP_KEY_STARTUP_NOTIFY,
                                TRUE);

        g_free (comment);
}

static GKeyFile *
create_client_key_file (CsmClient   *client,
                        const char  *desktop_file_path,
                        GError     **error) {
        GKeyFile *keyfile;

        keyfile = g_key_file_new ();

        if (desktop_file_path != NULL) {
                g_key_file_load_from_file (keyfile,
                                           desktop_file_path,
                                           G_KEY_FILE_KEEP_COMMENTS |
                                           G_KEY_FILE_KEEP_TRANSLATIONS,
                                           error);
        } else {
                set_desktop_file_keys_from_client (client, keyfile);
        }

        return keyfile;
}

static CsmClientRestartStyle
xsmp_get_restart_style_hint (CsmClient *client);

static GKeyFile *
xsmp_save (CsmClient *client,
           GError   **error)
{
        CsmClientRestartStyle restart_style;

        GKeyFile *keyfile = NULL;
        char     *desktop_file_path = NULL;
        char     *exec_program = NULL;
        char     *exec_discard = NULL;
        char     *startup_id = NULL;
        GError   *local_error;

        g_debug ("CsmXSMPClient: saving client with id %s",
                 csm_client_peek_id (client));

        local_error = NULL;

        restart_style = xsmp_get_restart_style_hint (client);
        if (restart_style == CSM_CLIENT_RESTART_NEVER) {
                goto out;
        }

        exec_program = xsmp_get_restart_command (client);
        if (!exec_program) {
                goto out;
        }

        desktop_file_path = get_desktop_file_path (CSM_XSMP_CLIENT (client));

        /* this can accept desktop_file_path == NULL */
        keyfile = create_client_key_file (client,
                                          desktop_file_path,
                                          &local_error);

        if (local_error) {
                goto out;
        }

        g_object_get (client,
                      "startup-id", &startup_id,
                      NULL);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               CSM_AUTOSTART_APP_STARTUP_ID_KEY,
                               startup_id);

        g_key_file_set_string (keyfile,
                               G_KEY_FILE_DESKTOP_GROUP,
                               G_KEY_FILE_DESKTOP_KEY_EXEC,
                               exec_program);

        exec_discard = xsmp_get_discard_command (client);
        if (exec_discard)
                g_key_file_set_string (keyfile,
                                       G_KEY_FILE_DESKTOP_GROUP,
                                       CSM_AUTOSTART_APP_DISCARD_KEY,
                                       exec_discard);

out:
        g_free (desktop_file_path);
        g_free (exec_program);
        g_free (exec_discard);
        g_free (startup_id);

        if (local_error != NULL) {
                g_propagate_error (error, local_error);
                g_key_file_free (keyfile);

                return NULL;
        }

        return keyfile;
}

static gboolean
xsmp_stop (CsmClient *client,
           GError   **error)
{
        CsmXSMPClient *xsmp = (CsmXSMPClient *) client;

        g_debug ("CsmXSMPClient: xsmp_stop ('%s')", xsmp->priv->description);

        if (xsmp->priv->conn == NULL) {
                g_set_error (error,
                             CSM_CLIENT_ERROR,
                             CSM_CLIENT_ERROR_NOT_REGISTERED,
                             "Client is not registered");
                return FALSE;
        }

        SmsDie (xsmp->priv->conn);

        return TRUE;
}

static gboolean
xsmp_query_end_session (CsmClient *client,
                        guint      flags,
                        GError   **error)
{
        gboolean allow_interact;
        int      save_type;

        if (CSM_XSMP_CLIENT (client)->priv->conn == NULL) {
                g_set_error (error,
                             CSM_CLIENT_ERROR,
                             CSM_CLIENT_ERROR_NOT_REGISTERED,
                             "Client is not registered");
                return FALSE;
        }

        allow_interact = !(flags & CSM_CLIENT_END_SESSION_FLAG_FORCEFUL);

        /* we don't want to save the session state, but we just want to know if
         * there's user data the client has to save and we want to give the
         * client a chance to tell the user about it. This is consistent with
         * the manager not setting CSM_CLIENT_END_SESSION_FLAG_SAVE for this
         * phase. */
        save_type = SmSaveGlobal;

        do_save_yourself (CSM_XSMP_CLIENT (client), save_type, allow_interact);
        return TRUE;
}

static gboolean
xsmp_end_session (CsmClient *client,
                  guint      flags,
                  GError   **error)
{
        gboolean phase2;

        if (CSM_XSMP_CLIENT (client)->priv->conn == NULL) {
                g_set_error (error,
                             CSM_CLIENT_ERROR,
                             CSM_CLIENT_ERROR_NOT_REGISTERED,
                             "Client is not registered");
                return FALSE;
        }

        phase2 = (flags & CSM_CLIENT_END_SESSION_FLAG_LAST);

        if (phase2) {
                xsmp_save_yourself_phase2 (client);
        } else {
                gboolean allow_interact;
                int      save_type;

                /* we gave a chance to interact to the app during
                 * xsmp_query_end_session(), now it's too late to interact */
                allow_interact = FALSE;

                if (flags & CSM_CLIENT_END_SESSION_FLAG_SAVE) {
                        save_type = SmSaveBoth;
                } else {
                        save_type = SmSaveGlobal;
                }

                do_save_yourself (CSM_XSMP_CLIENT (client),
                                  save_type, allow_interact);
        }

        return TRUE;
}

static char *
xsmp_get_app_name (CsmClient *client)
{
        SmProp *prop;
        char   *name = NULL;

        prop = find_property (CSM_XSMP_CLIENT (client), SmProgram, NULL);
        if (prop) {
                name = prop_to_command (prop);
        }

        return name;
}

static void
csm_client_set_ice_connection (CsmXSMPClient *client,
                               gpointer       conn)
{
        client->priv->ice_connection = conn;
}

static void
csm_xsmp_client_set_property (GObject       *object,
                              guint          prop_id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
        CsmXSMPClient *self;

        self = CSM_XSMP_CLIENT (object);

        switch (prop_id) {
        case PROP_ICE_CONNECTION:
                csm_client_set_ice_connection (self, g_value_get_pointer (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_xsmp_client_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        CsmXSMPClient *self;

        self = CSM_XSMP_CLIENT (object);

        switch (prop_id) {
        case PROP_ICE_CONNECTION:
                g_value_set_pointer (value, self->priv->ice_connection);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_xsmp_client_disconnect (CsmXSMPClient *client)
{
        if (client->priv->watch_id > 0) {
                g_source_remove (client->priv->watch_id);
        }

        if (client->priv->conn != NULL) {
                SmsCleanUp (client->priv->conn);
        }

        if (client->priv->ice_connection != NULL) {
                IceSetShutdownNegotiation (client->priv->ice_connection, FALSE);
                IceCloseConnection (client->priv->ice_connection);
        }
}

static void
csm_xsmp_client_finalize (GObject *object)
{
        CsmXSMPClient *client = (CsmXSMPClient *) object;

        g_debug ("CsmXSMPClient: xsmp_finalize (%s)", client->priv->description);
        csm_xsmp_client_disconnect (client);

        g_free (client->priv->description);
        g_ptr_array_foreach (client->priv->props, (GFunc)SmFreeProperty, NULL);
        g_ptr_array_free (client->priv->props, TRUE);

        G_OBJECT_CLASS (csm_xsmp_client_parent_class)->finalize (object);
}

static gboolean
_boolean_handled_accumulator (GSignalInvocationHint *ihint,
                              GValue                *return_accu,
                              const GValue          *handler_return,
                              gpointer               dummy)
{
        gboolean    continue_emission;
        gboolean    signal_handled;

        signal_handled = g_value_get_boolean (handler_return);
        g_value_set_boolean (return_accu, signal_handled);
        continue_emission = !signal_handled;

        return continue_emission;
}

static CsmClientRestartStyle
xsmp_get_restart_style_hint (CsmClient *client)
{
        SmProp               *prop;
        CsmClientRestartStyle hint;

        g_debug ("CsmXSMPClient: getting restart style");
        hint = CSM_CLIENT_RESTART_IF_RUNNING;

        prop = find_property (CSM_XSMP_CLIENT (client), SmRestartStyleHint, NULL);

        if (!prop || strcmp (prop->type, SmCARD8) != 0) {
                return CSM_CLIENT_RESTART_IF_RUNNING;
        }

        switch (((unsigned char *)prop->vals[0].value)[0]) {
        case SmRestartIfRunning:
                hint = CSM_CLIENT_RESTART_IF_RUNNING;
                break;
        case SmRestartAnyway:
                hint = CSM_CLIENT_RESTART_ANYWAY;
                break;
        case SmRestartImmediately:
                hint = CSM_CLIENT_RESTART_IMMEDIATELY;
                break;
        case SmRestartNever:
                hint = CSM_CLIENT_RESTART_NEVER;
                break;
        default:
                break;
        }

        return hint;
}

static gboolean
_parse_value_as_uint (const char *value,
                      guint      *uintval)
{
        char  *end_of_valid_uint;
        gulong ulong_value;
        guint  uint_value;

        errno = 0;
        ulong_value = strtoul (value, &end_of_valid_uint, 10);

        if (*value == '\0' || *end_of_valid_uint != '\0') {
                return FALSE;
        }

        uint_value = ulong_value;
        if (uint_value != ulong_value || errno == ERANGE) {
                return FALSE;
        }

        *uintval = uint_value;

        return TRUE;
}

static guint
xsmp_get_unix_process_id (CsmClient *client)
{
        SmProp  *prop;
        guint    pid;
        gboolean res;

        g_debug ("CsmXSMPClient: getting pid");

        prop = find_property (CSM_XSMP_CLIENT (client), SmProcessID, NULL);

        if (!prop || strcmp (prop->type, SmARRAY8) != 0) {
                return 0;
        }

        pid = 0;
        res = _parse_value_as_uint ((char *)prop->vals[0].value, &pid);
        if (! res) {
                pid = 0;
        }

        return pid;
}

static void
csm_xsmp_client_class_init (CsmXSMPClientClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        CsmClientClass *client_class = CSM_CLIENT_CLASS (klass);

        object_class->finalize             = csm_xsmp_client_finalize;
        object_class->constructor          = csm_xsmp_client_constructor;
        object_class->get_property         = csm_xsmp_client_get_property;
        object_class->set_property         = csm_xsmp_client_set_property;

        client_class->impl_save                   = xsmp_save;
        client_class->impl_stop                   = xsmp_stop;
        client_class->impl_query_end_session      = xsmp_query_end_session;
        client_class->impl_end_session            = xsmp_end_session;
        client_class->impl_cancel_end_session     = xsmp_cancel_end_session;
        client_class->impl_get_app_name           = xsmp_get_app_name;
        client_class->impl_get_restart_style_hint = xsmp_get_restart_style_hint;
        client_class->impl_get_unix_process_id    = xsmp_get_unix_process_id;

        signals[REGISTER_REQUEST] =
                g_signal_new ("register-request",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmXSMPClientClass, register_request),
                              _boolean_handled_accumulator,
                              NULL,
                              csm_marshal_BOOLEAN__POINTER,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_POINTER);
        signals[LOGOUT_REQUEST] =
                g_signal_new ("logout-request",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CsmXSMPClientClass, logout_request),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_ICE_CONNECTION,
                                         g_param_spec_pointer ("ice-connection",
                                                               "ice-connection",
                                                               "ice-connection",
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_type_class_add_private (klass, sizeof (CsmXSMPClientPrivate));
}

CsmClient *
csm_xsmp_client_new (IceConn ice_conn)
{
        CsmXSMPClient *xsmp;

        xsmp = g_object_new (CSM_TYPE_XSMP_CLIENT,
                             "ice-connection", ice_conn,
                             NULL);

        return CSM_CLIENT (xsmp);
}

static Status
register_client_callback (SmsConn    conn,
                          SmPointer  manager_data,
                          char      *previous_id)
{
        CsmXSMPClient *client = manager_data;
        gboolean       handled;
        char          *id;

        g_debug ("CsmXSMPClient: Client '%s' received RegisterClient(%s)",
                 client->priv->description,
                 previous_id ? previous_id : "NULL");


        /* There are three cases:
         * 1. id is NULL - we'll use a new one
         * 2. id is known - we'll use known one
         * 3. id is unknown - this is an error
         */
        id = g_strdup (previous_id);

        handled = FALSE;
        g_signal_emit (client, signals[REGISTER_REQUEST], 0, &id, &handled);
        if (! handled) {
                g_debug ("CsmXSMPClient:  RegisterClient not handled!");
                g_free (id);
                free (previous_id);
                g_assert_not_reached ();
                return FALSE;
        }

        if (IS_STRING_EMPTY (id)) {
                g_debug ("CsmXSMPClient:   rejected: invalid previous_id");
                free (previous_id);
                return FALSE;
        }

        g_object_set (client, "startup-id", id, NULL);

        set_description (client);

        g_debug ("CsmXSMPClient: Sending RegisterClientReply to '%s'", client->priv->description);

        SmsRegisterClientReply (conn, id);

        if (IS_STRING_EMPTY (previous_id)) {
                /* Send the initial SaveYourself. */
                g_debug ("CsmXSMPClient: Sending initial SaveYourself");
                SmsSaveYourself (conn, SmSaveLocal, False, SmInteractStyleNone, False);
                client->priv->current_save_yourself = SmSaveLocal;
        }

        csm_client_set_status (CSM_CLIENT (client), CSM_CLIENT_REGISTERED);

        g_free (id);
        free (previous_id);

        return TRUE;
}


static void
save_yourself_request_callback (SmsConn   conn,
                                SmPointer manager_data,
                                int       save_type,
                                Bool      shutdown,
                                int       interact_style,
                                Bool      fast,
                                Bool      global)
{
        CsmXSMPClient *client = manager_data;

        g_debug ("CsmXSMPClient: Client '%s' received SaveYourselfRequest(%s, %s, %s, %s, %s)",
                 client->priv->description,
                 save_type == SmSaveLocal ? "SmSaveLocal" :
                 save_type == SmSaveGlobal ? "SmSaveGlobal" : "SmSaveBoth",
                 shutdown ? "Shutdown" : "!Shutdown",
                 interact_style == SmInteractStyleAny ? "SmInteractStyleAny" :
                 interact_style == SmInteractStyleErrors ? "SmInteractStyleErrors" :
                 "SmInteractStyleNone", fast ? "Fast" : "!Fast",
                 global ? "Global" : "!Global");

        /* Examining the g_debug above, you can see that there are a total
         * of 72 different combinations of options that this could have been
         * called with. However, most of them are stupid.
         *
         * If @shutdown and @global are both TRUE, that means the caller is
         * requesting that a logout message be sent to all clients, so we do
         * that. We use @fast to decide whether or not to show a
         * confirmation dialog. (This isn't really what @fast is for, but
         * the old  and ksmserver both interpret it that way,
         * so we do too.) We ignore @save_type because we pick the correct
         * save_type ourselves later based on user prefs, dialog choices,
         * etc, and we ignore @interact_style, because clients have not used
         * it correctly consistently enough to make it worth honoring.
         *
         * If @shutdown is TRUE and @global is FALSE, the caller is
         * confused, so we ignore the request.
         *
         * If @shutdown is FALSE and @save_type is SmSaveGlobal or
         * SmSaveBoth, then the client wants us to ask some or all open
         * applications to save open files to disk, but NOT quit. This is
         * silly and so we ignore the request.
         *
         * If @shutdown is FALSE and @save_type is SmSaveLocal, then the
         * client wants us to ask some or all open applications to update
         * their current saved state, but not log out. At the moment, the
         * code only supports this for the !global case (ie, a client
         * requesting that it be allowed to update *its own* saved state,
         * but not having everyone else update their saved state).
         */

        if (shutdown && global) {
                g_debug ("CsmXSMPClient:   initiating shutdown");
                g_signal_emit (client, signals[LOGOUT_REQUEST], 0, !fast);
        } else if (!shutdown && !global) {
                g_debug ("CsmXSMPClient:   initiating checkpoint");
                do_save_yourself (client, SmSaveLocal, TRUE);
        } else {
                g_debug ("CsmXSMPClient:   ignoring");
        }
}

static void
save_yourself_phase2_request_callback (SmsConn   conn,
                                       SmPointer manager_data)
{
        CsmXSMPClient *client = manager_data;

        g_debug ("CsmXSMPClient: Client '%s' received SaveYourselfPhase2Request",
                 client->priv->description);

        client->priv->current_save_yourself = -1;

        /* this is a valid response to SaveYourself and therefore
           may be a response to a QES or ES */
        csm_client_end_session_response (CSM_CLIENT (client),
                                         TRUE, TRUE, FALSE,
                                         NULL);
}

static void
interact_request_callback (SmsConn   conn,
                           SmPointer manager_data,
                           int       dialog_type)
{
        CsmXSMPClient *client = manager_data;
#if 0
        gboolean       res;
        GError        *error;
#endif

        g_debug ("CsmXSMPClient: Client '%s' received InteractRequest(%s)",
                 client->priv->description,
                 dialog_type == SmDialogNormal ? "Dialog" : "Errors");

        csm_client_end_session_response (CSM_CLIENT (client),
                                         FALSE, FALSE, FALSE,
                                         _("This program is blocking logout."));

#if 0
        /* Can't just call back with Interact because session client
           grabs the keyboard!  So, we try to get it to release
           grabs by telling it we've cancelled the shutdown.
           This grabbing is clearly bullshit and is not supported by
           the client spec or protocol spec.
        */
        res = xsmp_cancel_end_session (CSM_CLIENT (client), &error);
        if (! res) {
                g_warning ("Unable to cancel end session: %s", error->message);
                g_error_free (error);
        }
#endif
        xsmp_interact (CSM_CLIENT (client));
}

static void
interact_done_callback (SmsConn   conn,
                        SmPointer manager_data,
                        Bool      cancel_shutdown)
{
        CsmXSMPClient *client = manager_data;

        g_debug ("CsmXSMPClient: Client '%s' received InteractDone(cancel_shutdown = %s)",
                 client->priv->description,
                 cancel_shutdown ? "True" : "False");

        csm_client_end_session_response (CSM_CLIENT (client),
                                         TRUE, FALSE, cancel_shutdown,
                                         NULL);
}

static void
save_yourself_done_callback (SmsConn   conn,
                             SmPointer manager_data,
                             Bool      success)
{
        CsmXSMPClient *client = manager_data;

        g_debug ("CsmXSMPClient: Client '%s' received SaveYourselfDone(success = %s)",
                 client->priv->description,
                 success ? "True" : "False");

	if (client->priv->current_save_yourself != -1) {
		SmsSaveComplete (client->priv->conn);
		client->priv->current_save_yourself = -1;
	}

        /* If success is false then the application couldn't save data. Nothing
         * the session manager can do about, though. FIXME: we could display a
         * dialog about this, I guess. */
        csm_client_end_session_response (CSM_CLIENT (client),
                                         TRUE, FALSE, FALSE,
                                         NULL);

        if (client->priv->next_save_yourself) {
                int      save_type = client->priv->next_save_yourself;
                gboolean allow_interact = client->priv->next_save_yourself_allow_interact;

                client->priv->next_save_yourself = -1;
                client->priv->next_save_yourself_allow_interact = -1;
                do_save_yourself (client, save_type, allow_interact);
        }
}

static void
close_connection_callback (SmsConn     conn,
                           SmPointer   manager_data,
                           int         count,
                           char      **reason_msgs)
{
        CsmXSMPClient *client = manager_data;
        int            i;

        g_debug ("CsmXSMPClient: Client '%s' received CloseConnection", client->priv->description);
        for (i = 0; i < count; i++) {
                g_debug ("CsmXSMPClient:  close reason: '%s'", reason_msgs[i]);
        }
        SmFreeReasons (count, reason_msgs);

        csm_client_set_status (CSM_CLIENT (client), CSM_CLIENT_FINISHED);
        csm_client_disconnected (CSM_CLIENT (client));
}

void
csm_xsmp_client_connect (CsmXSMPClient *client,
                         SmsConn        conn,
                         unsigned long *mask_ret,
                         SmsCallbacks  *callbacks_ret)
{
        client->priv->conn = conn;

        g_debug ("CsmXSMPClient: Initializing client %s", client->priv->description);

        *mask_ret = 0;

        *mask_ret |= SmsRegisterClientProcMask;
        callbacks_ret->register_client.callback = register_client_callback;
        callbacks_ret->register_client.manager_data  = client;

        *mask_ret |= SmsInteractRequestProcMask;
        callbacks_ret->interact_request.callback = interact_request_callback;
        callbacks_ret->interact_request.manager_data = client;

        *mask_ret |= SmsInteractDoneProcMask;
        callbacks_ret->interact_done.callback = interact_done_callback;
        callbacks_ret->interact_done.manager_data = client;

        *mask_ret |= SmsSaveYourselfRequestProcMask;
        callbacks_ret->save_yourself_request.callback = save_yourself_request_callback;
        callbacks_ret->save_yourself_request.manager_data = client;

        *mask_ret |= SmsSaveYourselfP2RequestProcMask;
        callbacks_ret->save_yourself_phase2_request.callback = save_yourself_phase2_request_callback;
        callbacks_ret->save_yourself_phase2_request.manager_data = client;

        *mask_ret |= SmsSaveYourselfDoneProcMask;
        callbacks_ret->save_yourself_done.callback = save_yourself_done_callback;
        callbacks_ret->save_yourself_done.manager_data = client;

        *mask_ret |= SmsCloseConnectionProcMask;
        callbacks_ret->close_connection.callback = close_connection_callback;
        callbacks_ret->close_connection.manager_data  = client;

        *mask_ret |= SmsSetPropertiesProcMask;
        callbacks_ret->set_properties.callback = set_properties_callback;
        callbacks_ret->set_properties.manager_data = client;

        *mask_ret |= SmsDeletePropertiesProcMask;
        callbacks_ret->delete_properties.callback = delete_properties_callback;
        callbacks_ret->delete_properties.manager_data = client;

        *mask_ret |= SmsGetPropertiesProcMask;
        callbacks_ret->get_properties.callback = get_properties_callback;
        callbacks_ret->get_properties.manager_data = client;
}

void
csm_xsmp_client_save_state (CsmXSMPClient *client)
{
        g_return_if_fail (CSM_IS_XSMP_CLIENT (client));
}
