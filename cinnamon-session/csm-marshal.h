
#ifndef __csm_marshal_MARSHAL_H__
#define __csm_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* BOOLEAN:POINTER (csm-marshal.list:1) */
extern void csm_marshal_BOOLEAN__POINTER (GClosure     *closure,
                                          GValue       *return_value,
                                          guint         n_param_values,
                                          const GValue *param_values,
                                          gpointer      invocation_hint,
                                          gpointer      marshal_data);

/* VOID:BOOLEAN,BOOLEAN,BOOLEAN,STRING (csm-marshal.list:2) */
extern void csm_marshal_VOID__BOOLEAN_BOOLEAN_BOOLEAN_STRING (GClosure     *closure,
                                                              GValue       *return_value,
                                                              guint         n_param_values,
                                                              const GValue *param_values,
                                                              gpointer      invocation_hint,
                                                              gpointer      marshal_data);

/* VOID:BOOLEAN,BOOLEAN,POINTER (csm-marshal.list:3) */
extern void csm_marshal_VOID__BOOLEAN_BOOLEAN_POINTER (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

G_END_DECLS

#endif /* __csm_marshal_MARSHAL_H__ */

