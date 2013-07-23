/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
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


#ifndef __CSM_STORE_H
#define __CSM_STORE_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CSM_TYPE_STORE         (csm_store_get_type ())
#define CSM_STORE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CSM_TYPE_STORE, CsmStore))
#define CSM_STORE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CSM_TYPE_STORE, CsmStoreClass))
#define CSM_IS_STORE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CSM_TYPE_STORE))
#define CSM_IS_STORE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CSM_TYPE_STORE))
#define CSM_STORE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CSM_TYPE_STORE, CsmStoreClass))

typedef struct CsmStorePrivate CsmStorePrivate;

typedef struct
{
        GObject          parent;
        CsmStorePrivate *priv;
} CsmStore;

typedef struct
{
        GObjectClass   parent_class;

        void          (* added)    (CsmStore   *store,
                                    const char *id);
        void          (* removed)  (CsmStore   *store,
                                    const char *id);
} CsmStoreClass;

typedef enum
{
         CSM_STORE_ERROR_GENERAL
} CsmStoreError;

#define CSM_STORE_ERROR csm_store_error_quark ()

typedef gboolean (*CsmStoreFunc) (const char *id,
                                  GObject    *object,
                                  gpointer    user_data);

GQuark              csm_store_error_quark              (void);
GType               csm_store_get_type                 (void);

CsmStore *          csm_store_new                      (void);

gboolean            csm_store_get_locked               (CsmStore    *store);
void                csm_store_set_locked               (CsmStore    *store,
                                                        gboolean     locked);

guint               csm_store_size                     (CsmStore    *store);
gboolean            csm_store_add                      (CsmStore    *store,
                                                        const char  *id,
                                                        GObject     *object);
void                csm_store_clear                    (CsmStore    *store);
gboolean            csm_store_remove                   (CsmStore    *store,
                                                        const char  *id);

void                csm_store_foreach                  (CsmStore    *store,
                                                        CsmStoreFunc func,
                                                        gpointer     user_data);
guint               csm_store_foreach_remove           (CsmStore    *store,
                                                        CsmStoreFunc func,
                                                        gpointer     user_data);
GObject *           csm_store_find                     (CsmStore    *store,
                                                        CsmStoreFunc predicate,
                                                        gpointer     user_data);
GObject *           csm_store_lookup                   (CsmStore    *store,
                                                        const char  *id);


G_END_DECLS

#endif /* __CSM_STORE_H */
