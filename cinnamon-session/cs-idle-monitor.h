/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __CS_IDLE_MONITOR_H
#define __CS_IDLE_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CS_TYPE_IDLE_MONITOR         (cs_idle_monitor_get_type ())
#define CS_IDLE_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CS_TYPE_IDLE_MONITOR, CSIdleMonitor))
#define CS_IDLE_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CS_TYPE_IDLE_MONITOR, CSIdleMonitorClass))
#define CS_IS_IDLE_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CS_TYPE_IDLE_MONITOR))
#define CS_IS_IDLE_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CS_TYPE_IDLE_MONITOR))
#define CS_IDLE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CS_TYPE_IDLE_MONITOR, CSIdleMonitorClass))

typedef struct CSIdleMonitorPrivate CSIdleMonitorPrivate;

typedef struct
{
        GObject               parent;
        CSIdleMonitorPrivate *priv;
} CSIdleMonitor;

typedef struct
{
        GObjectClass          parent_class;
} CSIdleMonitorClass;

typedef gboolean (*CSIdleMonitorWatchFunc) (CSIdleMonitor *monitor,
                                            guint          id,
                                            gboolean       condition,
                                            gpointer       user_data);

GType           cs_idle_monitor_get_type       (void);

CSIdleMonitor * cs_idle_monitor_new            (void);

guint           cs_idle_monitor_add_watch      (CSIdleMonitor         *monitor,
                                                guint                  interval,
                                                CSIdleMonitorWatchFunc callback,
                                                gpointer               user_data);

void            cs_idle_monitor_remove_watch   (CSIdleMonitor         *monitor,
                                                guint                  id);
void            cs_idle_monitor_reset          (CSIdleMonitor         *monitor);


G_END_DECLS

#endif /* __CS_IDLE_MONITOR_H */
