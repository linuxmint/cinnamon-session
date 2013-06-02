/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Authors:
 *	Matthias Clasen <mclasen@redhat.com>
 */

#ifndef __GSM_SYSTEMD_H__
#define __GSM_SYSTEMD_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GSM_TYPE_SYSTEMD             (gsm_systemd_get_type ())
#define GSM_SYSTEMD(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), GSM_TYPE_SYSTEMD, GsmSystemd))
#define GSM_SYSTEMD_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), GSM_TYPE_SYSTEMD, GsmSystemdClass))
#define GSM_IS_SYSTEMD(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GSM_TYPE_SYSTEMD))
#define GSM_IS_SYSTEMD_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GSM_TYPE_SYSTEMD))
#define GSM_SYSTEMD_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GSM_TYPE_SYSTEMD, GsmSystemdClass))

typedef struct _GsmSystemd        GsmSystemd;
typedef struct _GsmSystemdClass   GsmSystemdClass;
typedef struct _GsmSystemdPrivate GsmSystemdPrivate;

struct _GsmSystemd
{
        GObject            parent;

        GsmSystemdPrivate *priv;
};

struct _GsmSystemdClass
{
        GObjectClass parent_class;
};

GType         gsm_systemd_get_type (void);

GsmSystemd   *gsm_systemd_new      (void) G_GNUC_MALLOC;

G_END_DECLS

#endif /* __GSM_SYSTEMD_H__ */
