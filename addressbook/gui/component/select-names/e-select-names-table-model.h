/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc.
 */

#ifndef __E_SELECT_NAMES_TABLE_MODEL_H__
#define __E_SELECT_NAMES_TABLE_MODEL_H__

#include <time.h>
#include <stdio.h>
#include <gtk/gtkobject.h>
#include <gal/e-table/e-table-model.h>
#include "e-select-names-model.h"

#define E_TYPE_SELECT_NAMES_TABLE_MODEL            (e_select_names_table_model_get_type ())
#define E_SELECT_NAMES_TABLE_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SELECT_NAMES_TABLE_MODEL, ESelectNamesTableModel))
#define E_SELECT_NAMES_TABLE_MODEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SELECT_NAMES_TABLE_MODEL, ESelectNamesTableModelClass))
#define E_IS_SELECT_NAMES_TABLE_MODEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SELECT_NAMES_TABLE_MODEL))
#define E_IS_SELECT_NAMES_TABLE_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_SELECT_NAMES_TABLE_MODEL))

typedef struct {
	char *name;
	char *email;
} ESelectNamesTableModelData;

typedef struct _ESelectNamesTableModel ESelectNamesTableModel;
typedef struct _ESelectNamesTableModelClass ESelectNamesTableModelClass;

struct _ESelectNamesTableModel {
	ETableModel parent;

	ESelectNamesModel *source;
	int source_changed_id;

	int count;
	ESelectNamesTableModelData *data; /* This is used as an array. */
};

struct _ESelectNamesTableModelClass {
	ETableModelClass parent_class;
};

ETableModel *e_select_names_table_model_new  (ESelectNamesModel *source);

/* Standard Gtk function */			      
GType       e_select_names_table_model_get_type (void);

#endif /* ! __E_SELECT_NAMES_TABLE_MODEL_H__ */
