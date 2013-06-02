/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Novell, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "gsm-process-helper.h"

typedef struct {
        const char *command_line;
        GPid        pid;
        gboolean    timed_out;
        int         status;
        GMainLoop  *loop;
        guint       child_id;
        guint       timeout_id;
} GsmProcessHelper;

static void
on_child_exited (GPid     pid,
                 gint     status,
                 gpointer data)
{
        GsmProcessHelper *helper = data;

        helper->timed_out = FALSE;
        helper->status = status;

        g_spawn_close_pid (pid);
        g_main_loop_quit (helper->loop);
}

static gboolean
on_child_timeout (gpointer data)
{
        GsmProcessHelper *helper = data;

        kill (helper->pid, SIGTERM);
        helper->timed_out = TRUE;
        g_main_loop_quit (helper->loop);

        return FALSE;
}

gboolean
gsm_process_helper (const char   *command_line,
                    unsigned int  timeout,
                    GError      **error)
{
        GsmProcessHelper *helper;
        gchar **argv = NULL;
        GPid pid;
        gboolean ret;

        if (!g_shell_parse_argv (command_line, NULL, &argv, error))
                return FALSE;

        ret = g_spawn_async (NULL,
                             argv,
                             NULL,
                             G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,
                             NULL,
                             NULL,
                             &pid,
                             error);

        g_strfreev (argv);

        if (!ret)
                return FALSE;

        ret = FALSE;

        helper = g_slice_new0 (GsmProcessHelper);

        helper->command_line = command_line;
        helper->pid = pid;
        helper->timed_out = FALSE;
        helper->status = -1;

        helper->loop = g_main_loop_new (NULL, FALSE);
        helper->child_id = g_child_watch_add (helper->pid, on_child_exited, helper);
        helper->timeout_id = g_timeout_add (timeout, on_child_timeout, helper);

        g_main_loop_run (helper->loop);

        if (helper->timed_out) {
                g_set_error_literal (error,
                                     G_IO_CHANNEL_ERROR,
                                     G_IO_CHANNEL_ERROR_FAILED,
                                     "Timed out");
        } else {
                if (g_spawn_check_exit_status (helper->status, error))
                        ret = TRUE;
        }

        if (helper->loop) {
                g_main_loop_unref (helper->loop);
                helper->loop = NULL;
        }

        if (helper->child_id) {
                g_source_remove (helper->child_id);
                helper->child_id = 0;
        }

        if (helper->timeout_id) {
                g_source_remove (helper->timeout_id);
                helper->timeout_id = 0;
        }

        g_slice_free (GsmProcessHelper, helper);

        return ret;
}
