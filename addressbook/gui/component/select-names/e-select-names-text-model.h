/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors:
 *   Chris Lahey    <clahey@ximian.com>
 *   Jon Trowbridge <trow@ximian.com>
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 */

#ifndef __E_SELECT_NAMES_TEXT_MODEL_H__
#define __E_SELECT_NAMES_TEXT_MODEL_H__

#include <time.h>
#include <stdio.h>
#include <gtk/gtkobject.h>
#include <gal/e-text/e-text-model.h>
#include "e-select-names-model.h"

#define E_TYPE_SELECT_NAMES_TEXT_MODEL            (e_select_names_text_model_get_type ())
#define E_SELECT_NAMES_TEXT_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SELECT_NAMES_TEXT_MODEL, ESelectNamesTextModel))
#define E_SELECT_NAMES_TEXT_MODEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SELECT_NAMES_TEXT_MODEL, ESelectNamesTextModelClass))
#define E_IS_SELECT_NAMES_TEXT_MODEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SELECT_NAMES_TEXT_MODEL))
#define E_IS_SELECT_NAMES_TEXT_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_SELECT_NAMES_TEXT_MODEL))

typedef struct _ESelectNamesTextModel ESelectNamesTextModel;
typedef struct _ESelectNamesTextModelClass ESelectNamesTextModelClass;

struct _ESelectNamesTextModel {
	ETextModel parent;

	ESelectNamesModel *source;
	gint source_changed_id;
	gint source_resize_id;

	gchar *text;

	gchar *sep;
	gint seplen;
	
	gint last_magic_comma_pos;
};

struct _ESelectNamesTextModelClass {
	ETextModelClass parent_class;
};

ETextModel *e_select_names_text_model_new           (ESelectNamesModel *source);
void        e_select_names_text_model_set_separator (ESelectNamesTextModel *model, const char *sep);

/* Standard Gtk function */			      
GType       e_select_names_text_model_get_type (void);

#endif /* ! __E_SELECT_NAMES_TEXT_MODEL_H__ */
