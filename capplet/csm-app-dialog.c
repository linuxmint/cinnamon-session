/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
 * Foundation, Inc., 51 Franklin Street - Suite 500, Boston, MA 02110-1335, USA.
 *
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "csm-util.h"

#include "csm-app-dialog.h"

#define GTKBUILDER_FILE "cinnamon-session-properties.glade"

#define CAPPLET_NAME_ENTRY_WIDGET_NAME    "session_properties_name_entry"
#define CAPPLET_COMMAND_ENTRY_WIDGET_NAME "session_properties_command_entry"
#define CAPPLET_COMMENT_ENTRY_WIDGET_NAME "session_properties_comment_entry"
#define CAPPLET_BROWSE_WIDGET_NAME        "session_properties_browse_button"
#define CAPPLET_DELAY_SPINNER_WIDGET_NAME "session_properties_spinner"

#define CSM_APP_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CSM_TYPE_APP_DIALOG, CsmAppDialogPrivate))

struct CsmAppDialogPrivate
{
        GtkWidget *name_entry;
        GtkWidget *command_entry;
        GtkWidget *comment_entry;
        GtkWidget *delay_spinner;
        GtkWidget *browse_button;
        char      *name;
        char      *command;
        char      *comment;
        char      *delay;
};

static void     csm_app_dialog_class_init  (CsmAppDialogClass *klass);
static void     csm_app_dialog_init        (CsmAppDialog      *app_dialog);
static void     csm_app_dialog_finalize    (GObject           *object);

enum {
        PROP_0,
        PROP_NAME,
        PROP_COMMAND,
        PROP_COMMENT,
        PROP_DELAY
};

G_DEFINE_TYPE (CsmAppDialog, csm_app_dialog, GTK_TYPE_DIALOG)

gint
char_to_int (const char *in)
{
    gint64 ret;
    ret = g_ascii_strtoll (in, NULL, 10);
    if (ret > G_MAXINT || ret < G_MININT)
        ret = 0;
    return (gint) ret;
}

static char *
make_exec_uri (const char *exec)
{
        GString    *str;
        const char *c;

        if (exec == NULL) {
                return g_strdup ("");
        }

        if (strchr (exec, ' ') == NULL) {
                return g_strdup (exec);
        }

        str = g_string_new_len (NULL, strlen (exec));

        str = g_string_append_c (str, '"');
        for (c = exec; *c != '\0'; c++) {
                /* FIXME: GKeyFile will add an additional backslach so we'll
                 * end up with toto\\" instead of toto\"
                 * We could use g_key_file_set_value(), but then we don't
                 * benefit from the other escaping that glib is doing...
                 */
                if (*c == '"') {
                        str = g_string_append (str, "\\\"");
                } else {
                        str = g_string_append_c (str, *c);
                }
        }
        str = g_string_append_c (str, '"');

        return g_string_free (str, FALSE);
}

static void
on_browse_button_clicked (GtkWidget    *widget,
                          CsmAppDialog *dialog)
{
        GtkWidget *chooser;
        int        response;

        chooser = gtk_file_chooser_dialog_new ("",
                                               GTK_WINDOW (dialog),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               GTK_STOCK_CANCEL,
                                               GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_OPEN,
                                               GTK_RESPONSE_ACCEPT,
                                               NULL);

        gtk_window_set_transient_for (GTK_WINDOW (chooser),
                                      GTK_WINDOW (dialog));

        gtk_window_set_destroy_with_parent (GTK_WINDOW (chooser), TRUE);

        gtk_window_set_title (GTK_WINDOW (chooser), _("Select Command"));

        gtk_widget_show (chooser);

        response = gtk_dialog_run (GTK_DIALOG (chooser));

        if (response == GTK_RESPONSE_ACCEPT) {
                char *text;
                char *uri;

                text = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));

                uri = make_exec_uri (text);

                g_free (text);

                gtk_entry_set_text (GTK_ENTRY (dialog->priv->command_entry), uri);

                g_free (uri);
        }

        gtk_widget_destroy (chooser);
}

static void
on_entry_activate (GtkEntry     *entry,
                   CsmAppDialog *dialog)
{
        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static void
setup_dialog (CsmAppDialog *dialog)
{
        GtkWidget  *content_area;
        GtkWidget  *widget;
        GtkBuilder *xml;
        GError     *error;

        xml = gtk_builder_new ();
        gtk_builder_set_translation_domain (xml, GETTEXT_PACKAGE);

        error = NULL;
        if (!gtk_builder_add_from_file (xml,
                                        GTKBUILDER_DIR "/" GTKBUILDER_FILE,
                                        &error)) {
                if (error) {
                        g_warning ("Could not load capplet UI file: %s",
                                   error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Could not load capplet UI file.");
                }
        }

        content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
        widget = GTK_WIDGET (gtk_builder_get_object (xml, "main-table"));
        gtk_container_add (GTK_CONTAINER (content_area), widget);

        gtk_container_set_border_width (GTK_CONTAINER (dialog), 6);
        gtk_window_set_icon_name (GTK_WINDOW (dialog), "session-properties");

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

        if (dialog->priv->name == NULL
            && dialog->priv->command == NULL
            && dialog->priv->comment == NULL) {
                gtk_window_set_title (GTK_WINDOW (dialog), _("Add Startup Program"));
                gtk_dialog_add_button (GTK_DIALOG (dialog),
                                       GTK_STOCK_ADD, GTK_RESPONSE_OK);
        } else {
                gtk_window_set_title (GTK_WINDOW (dialog), _("Edit Startup Program"));
                gtk_dialog_add_button (GTK_DIALOG (dialog),
                                       GTK_STOCK_SAVE, GTK_RESPONSE_OK);
        }

        dialog->priv->name_entry = GTK_WIDGET (gtk_builder_get_object (xml, CAPPLET_NAME_ENTRY_WIDGET_NAME));
        g_signal_connect (dialog->priv->name_entry,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->priv->name != NULL) {
                gtk_entry_set_text (GTK_ENTRY (dialog->priv->name_entry), dialog->priv->name);
        }

        dialog->priv->browse_button = GTK_WIDGET (gtk_builder_get_object (xml, CAPPLET_BROWSE_WIDGET_NAME));
        g_signal_connect (dialog->priv->browse_button,
                          "clicked",
                          G_CALLBACK (on_browse_button_clicked),
                          dialog);

        dialog->priv->command_entry = GTK_WIDGET (gtk_builder_get_object (xml, CAPPLET_COMMAND_ENTRY_WIDGET_NAME));
        g_signal_connect (dialog->priv->command_entry,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->priv->command != NULL) {
                gtk_entry_set_text (GTK_ENTRY (dialog->priv->command_entry), dialog->priv->command);
        }

        dialog->priv->comment_entry = GTK_WIDGET (gtk_builder_get_object (xml, CAPPLET_COMMENT_ENTRY_WIDGET_NAME));
        g_signal_connect (dialog->priv->comment_entry,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->priv->comment != NULL) {
                gtk_entry_set_text (GTK_ENTRY (dialog->priv->comment_entry), dialog->priv->comment);
        }

        dialog->priv->delay_spinner = GTK_WIDGET (gtk_builder_get_object (xml, CAPPLET_DELAY_SPINNER_WIDGET_NAME));
        g_signal_connect (dialog->priv->delay_spinner,
                          "activate",
                          G_CALLBACK (on_entry_activate),
                          dialog);
        if (dialog->priv->delay != NULL) {
                gtk_spin_button_set_value (GTK_SPIN_BUTTON (dialog->priv->delay_spinner), char_to_int (dialog->priv->delay));
        }

        if (xml != NULL) {
                g_object_unref (xml);
        }
}

static GObject *
csm_app_dialog_constructor (GType                  type,
                            guint                  n_construct_app,
                            GObjectConstructParam *construct_app)
{
        CsmAppDialog *dialog;

        dialog = CSM_APP_DIALOG (G_OBJECT_CLASS (csm_app_dialog_parent_class)->constructor (type,
                                                                                                                  n_construct_app,
                                                                                                                  construct_app));

        setup_dialog (dialog);

        gtk_widget_show_all (GTK_WIDGET (dialog));

        return G_OBJECT (dialog);
}

static void
csm_app_dialog_dispose (GObject *object)
{
        CsmAppDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_APP_DIALOG (object));

        dialog = CSM_APP_DIALOG (object);

        g_free (dialog->priv->name);
        dialog->priv->name = NULL;
        g_free (dialog->priv->command);
        dialog->priv->command = NULL;
        g_free (dialog->priv->comment);
        dialog->priv->comment = NULL;
        g_free (dialog->priv->delay);
        dialog->priv->delay = NULL;

        G_OBJECT_CLASS (csm_app_dialog_parent_class)->dispose (object);
}

static void
csm_app_dialog_set_name (CsmAppDialog *dialog,
                         const char   *name)
{
        g_return_if_fail (CSM_IS_APP_DIALOG (dialog));

        g_free (dialog->priv->name);

        dialog->priv->name = g_strdup (name);
        g_object_notify (G_OBJECT (dialog), "name");
}

static void
csm_app_dialog_set_command (CsmAppDialog *dialog,
                            const char   *name)
{
        g_return_if_fail (CSM_IS_APP_DIALOG (dialog));

        g_free (dialog->priv->command);

        dialog->priv->command = g_strdup (name);
        g_object_notify (G_OBJECT (dialog), "command");
}

static void
csm_app_dialog_set_comment (CsmAppDialog *dialog,
                            const char   *name)
{
        g_return_if_fail (CSM_IS_APP_DIALOG (dialog));

        g_free (dialog->priv->comment);

        dialog->priv->comment = g_strdup (name);
        g_object_notify (G_OBJECT (dialog), "comment");
}

static void
csm_app_dialog_set_delay (CsmAppDialog *dialog,
                          const char   *delay)
{
    g_return_if_fail (CSM_IS_APP_DIALOG (dialog));

    g_free (dialog->priv->delay);

    dialog->priv->delay = g_strdup (delay);
    g_object_notify (G_OBJECT (dialog), "delay");
}

const char *
csm_app_dialog_get_name (CsmAppDialog *dialog)
{
        g_return_val_if_fail (CSM_IS_APP_DIALOG (dialog), NULL);
        return gtk_entry_get_text (GTK_ENTRY (dialog->priv->name_entry));
}

const char *
csm_app_dialog_get_command (CsmAppDialog *dialog)
{
        g_return_val_if_fail (CSM_IS_APP_DIALOG (dialog), NULL);
        return gtk_entry_get_text (GTK_ENTRY (dialog->priv->command_entry));
}

const char *
csm_app_dialog_get_comment (CsmAppDialog *dialog)
{
        g_return_val_if_fail (CSM_IS_APP_DIALOG (dialog), NULL);
        return gtk_entry_get_text (GTK_ENTRY (dialog->priv->comment_entry));
}

char *
csm_app_dialog_get_delay (CsmAppDialog *dialog)
{
        g_return_val_if_fail (CSM_IS_APP_DIALOG (dialog), NULL);
        gint val = gtk_spin_button_get_value (GTK_SPIN_BUTTON (dialog->priv->delay_spinner));
        gchar *ret = g_strdup_printf ("%d", val);
        return ret;
}

static void
csm_app_dialog_set_property (GObject        *object,
                             guint           prop_id,
                             const GValue   *value,
                             GParamSpec     *pspec)
{
        CsmAppDialog *dialog = CSM_APP_DIALOG (object);

        switch (prop_id) {
        case PROP_NAME:
                csm_app_dialog_set_name (dialog, g_value_get_string (value));
                break;
        case PROP_COMMAND:
                csm_app_dialog_set_command (dialog, g_value_get_string (value));
                break;
        case PROP_COMMENT:
                csm_app_dialog_set_comment (dialog, g_value_get_string (value));
                break;
        case PROP_DELAY:
                csm_app_dialog_set_delay (dialog, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_app_dialog_get_property (GObject        *object,
                             guint           prop_id,
                             GValue         *value,
                             GParamSpec     *pspec)
{
        CsmAppDialog *dialog = CSM_APP_DIALOG (object);

        switch (prop_id) {
        case PROP_NAME:
                g_value_set_string (value, dialog->priv->name);
                break;
        case PROP_COMMAND:
                g_value_set_string (value, dialog->priv->command);
                break;
        case PROP_COMMENT:
                g_value_set_string (value, dialog->priv->comment);
                break;
        case PROP_DELAY:
                g_value_set_string (value, dialog->priv->delay);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
csm_app_dialog_class_init (CsmAppDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = csm_app_dialog_get_property;
        object_class->set_property = csm_app_dialog_set_property;
        object_class->constructor = csm_app_dialog_constructor;
        object_class->dispose = csm_app_dialog_dispose;
        object_class->finalize = csm_app_dialog_finalize;

        g_object_class_install_property (object_class,
                                         PROP_NAME,
                                         g_param_spec_string ("name",
                                                              "name",
                                                              "name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COMMAND,
                                         g_param_spec_string ("command",
                                                              "command",
                                                              "command",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_COMMENT,
                                         g_param_spec_string ("comment",
                                                              "comment",
                                                              "comment",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_DELAY,
                                         g_param_spec_string ("delay",
                                                              "delay",
                                                              "delay",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (CsmAppDialogPrivate));
}

static void
csm_app_dialog_init (CsmAppDialog *dialog)
{

        dialog->priv = CSM_APP_DIALOG_GET_PRIVATE (dialog);
}

static void
csm_app_dialog_finalize (GObject *object)
{
        CsmAppDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CSM_IS_APP_DIALOG (object));

        dialog = CSM_APP_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        G_OBJECT_CLASS (csm_app_dialog_parent_class)->finalize (object);
}

GtkWidget *
csm_app_dialog_new (const char *name,
                    const char *command,
                    const char *comment,
                    const char *delay)
{
        GObject *object;

        object = g_object_new (CSM_TYPE_APP_DIALOG,
                               "name", name,
                               "command", command,
                               "comment", comment,
                               "delay", delay,
                               NULL);

        return GTK_WIDGET (object);
}

gboolean
csm_app_dialog_run (CsmAppDialog  *dialog,
                    char         **name_p,
                    char         **command_p,
                    char         **comment_p,
                    char         **delay_p)
{
        gboolean retval;

        retval = FALSE;

        while (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK) {
                const char *name;
                const char *exec;
                const char *comment;
                char *delay;
                const char *error_msg;
                GError     *error;
                char      **argv;
                int         argc;

                name = csm_app_dialog_get_name (CSM_APP_DIALOG (dialog));
                exec = csm_app_dialog_get_command (CSM_APP_DIALOG (dialog));
                comment = csm_app_dialog_get_comment (CSM_APP_DIALOG (dialog));
                delay = csm_app_dialog_get_delay (CSM_APP_DIALOG (dialog));

                error = NULL;
                error_msg = NULL;

                if (csm_util_text_is_blank (exec)) {
                        error_msg = _("The startup command cannot be empty");
                } else {
                        if (!g_shell_parse_argv (exec, &argc, &argv, &error)) {
                                if (error != NULL) {
                                        error_msg = error->message;
                                } else {
                                        error_msg = _("The startup command is not valid");
                                }
                        }
                }

                if (error_msg != NULL) {
                        GtkWidget *msgbox;

                        msgbox = gtk_message_dialog_new (GTK_WINDOW (dialog),
                                                         GTK_DIALOG_MODAL,
                                                         GTK_MESSAGE_ERROR,
                                                         GTK_BUTTONS_CLOSE,
                                                         "%s", error_msg);

                        if (error != NULL) {
                                g_error_free (error);
                        }

                        gtk_dialog_run (GTK_DIALOG (msgbox));

                        gtk_widget_destroy (msgbox);

                        continue;
                }

                if (csm_util_text_is_blank (name)) {
                        name = argv[0];
                }

                if (name_p) {
                        *name_p = g_strdup (name);
                }

                g_strfreev (argv);

                if (command_p) {
                        *command_p = g_strdup (exec);
                }

                if (comment_p) {
                        *comment_p = g_strdup (comment);
                }

                if (delay_p) {
                        *delay_p = g_strdup (delay);
                }

                g_free (delay);
                retval = TRUE;
                break;
        }

        gtk_widget_destroy (GTK_WIDGET (dialog));

        return retval;
}
