/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Red Hat, Inc.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <stdlib.h>

#include "csm-process-helper.h"

int
main (int   argc,
      char *argv[])
{
        char *command_line = "xeyes";
        int   timeout = 500;
        GError *error = NULL;

        if (argc > 3) {
                g_printerr ("Too many arguments.\n");
                g_printerr ("Usage: %s [COMMAND] [TIMEOUT]\n", argv[0]);
                return 1;
        }

        if (argc >= 2)
                command_line = argv[1];
        if (argc >= 3) {
                int i = atoi (argv[2]);
                if (i > 0)
                        timeout = i;
        }

        if (!csm_process_helper (command_line, timeout, &error)) {
                g_warning ("%s", error->message);
                g_clear_error (&error);
        } else {
                g_print ("Command exited successfully.\n");
        }

        return 0;
}
