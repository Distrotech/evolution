/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: 
 *   Chris Lahey     <clahey@ximian.com>
 *   Jon Trowbidge   <trow@ximian.com>
 *
 * Copyright (C) 2000, 2001 Ximian, Inc.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtkmarshal.h>
#include <gtk/gtksignal.h>

#include <gal/util/e-util.h>

#include "e-select-names-model.h"
#include "addressbook/backend/ebook/e-card-simple.h"

#define SEPARATOR ", "
#define SEPLEN    (strlen(SEPARATOR))

#define MAX_LENGTH 2047


enum {
	E_SELECT_NAMES_MODEL_CHANGED,
	E_SELECT_NAMES_MODEL_RESIZED,
	E_SELECT_NAMES_MODEL_LAST_SIGNAL
};

static guint e_select_names_model_signals[E_SELECT_NAMES_MODEL_LAST_SIGNAL] = { 0 };

/* Object argument IDs */
enum {
	ARG_0,
	ARG_CARD,
};

enum {
	NAME_DATA_BLANK,
	NAME_DATA_CARD,
	NAME_DATA_STRING
};

enum {
	NAME_FORMAT_GIVEN_FIRST,
	NAME_FORMAT_FAMILY_FIRST
};

struct _ESelectNamesModelPrivate {
	gchar *id;
	gchar *title;

	GList *data;  /* of EDestination */
	gchar *text;
	gchar *addr_text;

	gint limit;

	gint freeze_count;
	gboolean pending_changed;
};


static void e_select_names_model_init (ESelectNamesModel *model);
static void e_select_names_model_class_init (ESelectNamesModelClass *klass);

static void e_select_names_model_destroy (GtkObject *object);
static void e_select_names_model_set_arg (GtkObject *object, GtkArg *arg, guint arg_id);
static void e_select_names_model_get_arg (GtkObject *object, GtkArg *arg, guint arg_id);

GtkType
e_select_names_model_get_type (void)
{
	static GtkType model_type = 0;

	if (!model_type) {
		GtkTypeInfo model_info = {
			"ESelectNamesModel",
			sizeof (ESelectNamesModel),
			sizeof (ESelectNamesModelClass),
			(GtkClassInitFunc) e_select_names_model_class_init,
			(GtkObjectInitFunc) e_select_names_model_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		model_type = gtk_type_unique (gtk_object_get_type (), &model_info);
	}

	return model_type;
}

typedef void (*GtkSignal_NONE__INT_INT_INT) (GtkObject *object, gint arg1, gint arg2, gint arg3, gpointer user_data);
static void
local_gtk_marshal_NONE__INT_INT_INT (GtkObject    *object, 
				     GtkSignalFunc func, 
				     gpointer      func_data, 
				     GtkArg       *args)
{
	GtkSignal_NONE__INT_INT_INT rfunc;
	rfunc = (GtkSignal_NONE__INT_INT_INT) func;
	(* rfunc) (object,
		   GTK_VALUE_INT(args[0]),
		   GTK_VALUE_INT(args[1]),
		   GTK_VALUE_INT(args[2]),
		   func_data);
}


static void
e_select_names_model_class_init (ESelectNamesModelClass *klass)
{
	GtkObjectClass *object_class;

	object_class = GTK_OBJECT_CLASS(klass);

	e_select_names_model_signals[E_SELECT_NAMES_MODEL_CHANGED] =
		gtk_signal_new ("changed",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ESelectNamesModelClass, changed),
				gtk_marshal_NONE__NONE,
				GTK_TYPE_NONE, 0);

	e_select_names_model_signals[E_SELECT_NAMES_MODEL_RESIZED] =
		gtk_signal_new ("resized",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ESelectNamesModelClass, resized),
				local_gtk_marshal_NONE__INT_INT_INT,
				GTK_TYPE_NONE, 3, GTK_TYPE_INT, GTK_TYPE_INT, GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, e_select_names_model_signals, E_SELECT_NAMES_MODEL_LAST_SIGNAL);

	gtk_object_add_arg_type ("ESelectNamesModel::card",
				 GTK_TYPE_OBJECT, GTK_ARG_READWRITE, ARG_CARD);

	klass->changed = NULL;

	object_class->destroy = e_select_names_model_destroy;
	object_class->get_arg = e_select_names_model_get_arg;
	object_class->set_arg = e_select_names_model_set_arg;
}

/**
 * e_select_names_model_init:
 */
static void
e_select_names_model_init (ESelectNamesModel *model)
{
	model->priv = g_new0 (struct _ESelectNamesModelPrivate, 1);

	model->priv->limit = -1;
}

static void
e_select_names_model_destroy (GtkObject *object)
{
	ESelectNamesModel *model = E_SELECT_NAMES_MODEL (object);
	
	g_free (model->priv->title);
	g_free (model->priv->id);

	g_list_foreach (model->priv->data, (GFunc) gtk_object_unref, NULL);
	g_list_free (model->priv->data);

	g_free (model->priv->text);
	g_free (model->priv->addr_text);

	g_free (model->priv);

}


/* Set_arg handler for the model */
static void
e_select_names_model_set_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	ESelectNamesModel *model;
	
	model = E_SELECT_NAMES_MODEL (object);

	switch (arg_id) {
	case ARG_CARD:
		break;
	default:
		return;
	}
}

/* Get_arg handler for the model */
static void
e_select_names_model_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	ESelectNamesModel *model;

	model = E_SELECT_NAMES_MODEL (object);

	switch (arg_id) {
	case ARG_CARD:
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}


static void
e_select_names_model_changed (ESelectNamesModel *model)
{
	g_free (model->priv->text);
	model->priv->text = NULL;
	
	g_free (model->priv->addr_text);
	model->priv->addr_text = NULL;

	if (model->priv->freeze_count > 0) {
		model->priv->pending_changed = TRUE;
	} else {
		gtk_signal_emit (GTK_OBJECT(model), e_select_names_model_signals[E_SELECT_NAMES_MODEL_CHANGED]);
		model->priv->pending_changed = FALSE;
	}
}

static void
destination_changed_proxy (EDestination *dest, gpointer closure)
{
	e_select_names_model_changed (E_SELECT_NAMES_MODEL (closure));
}

/** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** **/

ESelectNamesModel *
e_select_names_model_new (void)
{
	ESelectNamesModel *model;
	model = E_SELECT_NAMES_MODEL (gtk_type_new (e_select_names_model_get_type ()));
	return model;
}

ESelectNamesModel *
e_select_names_model_duplicate (ESelectNamesModel *old)
{
	ESelectNamesModel *model = E_SELECT_NAMES_MODEL(gtk_type_new(e_select_names_model_get_type()));
	GList *iter;

	model->priv->id = g_strdup (old->priv->id);
	model->priv->title = g_strdup (old->priv->title);
	
	for (iter = old->priv->data; iter != NULL; iter = g_list_next (iter)) {
		EDestination *dup = e_destination_copy (E_DESTINATION (iter->data));
		e_select_names_model_append (model, dup);
	}

	model->priv->limit = old->priv->limit;

	return model;
}

const gchar *
e_select_names_model_get_textification (ESelectNamesModel *model)
{
	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), NULL);

	if (model->priv->text == NULL) {

		if (model->priv->data == NULL) {
			
			model->priv->text = g_strdup ("");

		} else {
			gchar **strv = g_new0 (gchar *, g_list_length (model->priv->data)+1);
			gint i = 0;
			GList *iter = model->priv->data;
			
			while (iter) {
				EDestination *dest = E_DESTINATION (iter->data);
				strv[i] = (gchar *) e_destination_get_textrep (dest);
				++i;
				iter = g_list_next (iter);
			}

			model->priv->text = g_strjoinv (SEPARATOR, strv);

			if (strlen(model->priv->text) > MAX_LENGTH) {
				model->priv->text[MAX_LENGTH] = 0;
				g_realloc (model->priv->text, MAX_LENGTH + 1);
			}
			
			g_free (strv);
		}
	}

	return model->priv->text;
}

const gchar *
e_select_names_model_get_address_text (ESelectNamesModel *model)
{
	g_return_val_if_fail (model != NULL, NULL);
	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), NULL);

	if (model->priv->addr_text == NULL) {

		if (model->priv->data == NULL) {

			model->priv->addr_text = g_strdup ("");

		} else {
			gchar **strv = g_new0 (gchar *, g_list_length (model->priv->data)+1);
			gint i = 0;
			GList *iter = model->priv->data;

			while (iter) {
				EDestination *dest = E_DESTINATION (iter->data);
				strv[i] = (gchar *) e_destination_get_address (dest);
				if (strv[i])
					++i;
				iter = g_list_next (iter);
			}

			model->priv->addr_text = g_strjoinv (SEPARATOR, strv);

			g_free (strv);
		}
	}

	return model->priv->addr_text;
}

gint
e_select_names_model_count (ESelectNamesModel *model)
{
	g_return_val_if_fail (model != NULL, 0);
	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), 0);

	return g_list_length (model->priv->data);
}

gint
e_select_names_model_get_limit (ESelectNamesModel *model)
{
	g_return_val_if_fail (model != NULL, 0);
	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), 0);

	return model->priv->limit;
}

void
e_select_names_model_set_limit (ESelectNamesModel *model, gint limit)
{
	g_return_if_fail (model != NULL);
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));

	model->priv->limit = MAX (limit, -1);
}

gboolean
e_select_names_model_at_limit (ESelectNamesModel *model)
{
	g_return_val_if_fail (model != NULL, TRUE);
	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), TRUE);

	return model->priv->limit >= 0 && g_list_length (model->priv->data) >= model->priv->limit;
}

const EDestination *
e_select_names_model_get_destination (ESelectNamesModel *model, gint index)
{
	g_return_val_if_fail (model && E_IS_SELECT_NAMES_MODEL (model), NULL);
	g_return_val_if_fail (0 <= index, NULL);
	g_return_val_if_fail (index < g_list_length (model->priv->data), NULL);

	return E_DESTINATION (g_list_nth_data (model->priv->data, index));
}

gchar *
e_select_names_model_export_destinationv (ESelectNamesModel *model)
{
	EDestination **destv;
	gchar *str;
	gint i, len = 0;
	GList *j;
	g_return_val_if_fail (model && E_IS_SELECT_NAMES_MODEL (model), NULL);

	len = g_list_length (model->priv->data);
	destv = g_new0 (EDestination *, len+1);
	
	for (i=0, j = model->priv->data; j != NULL; j = g_list_next (j)) {
		EDestination *dest = E_DESTINATION (j->data);

		if (dest)
			destv[i++] = dest;
	}

	str = e_destination_exportv (destv);
	g_free (destv);

	return str;
}

static
void send_changed (EDestination *dest, ECard *card, gpointer closure)
{
	ESelectNamesModel *model = closure;
	e_select_names_model_changed (model);
}

void
e_select_names_model_import_destinationv (ESelectNamesModel *model,
					  gchar *destinationv)
{
	EDestination **destv;
	gint i;

	g_return_if_fail (model && E_IS_SELECT_NAMES_MODEL (model));

	destv = e_destination_importv (destinationv);

	e_select_names_model_delete_all (model);

	if (destv == NULL)
		return;

	for (i = 0; destv[i]; i++) {
		e_destination_use_card (destv[i], send_changed, model);
		e_select_names_model_append (model, destv[i]);
	}
	g_free (destv);
}

ECard *
e_select_names_model_get_card (ESelectNamesModel *model, gint index)
{
	const EDestination *dest;

	g_return_val_if_fail (model && E_IS_SELECT_NAMES_MODEL (model), NULL);
	g_return_val_if_fail (0 <= index, NULL);
	g_return_val_if_fail (index < g_list_length (model->priv->data), NULL);

	dest = e_select_names_model_get_destination (model, index);
	return dest ? e_destination_get_card (dest) : NULL;

}

const gchar *
e_select_names_model_get_string (ESelectNamesModel *model, gint index)
{
	const EDestination *dest;

	g_return_val_if_fail (model && E_IS_SELECT_NAMES_MODEL (model), NULL);
	g_return_val_if_fail (0 <= index, NULL);
	g_return_val_if_fail (index < g_list_length (model->priv->data), NULL);

	dest = e_select_names_model_get_destination (model, index);
	
	return dest ? e_destination_get_textrep (dest) : "";
}

static void
connect_destination (ESelectNamesModel *model, EDestination *dest)
{
	gtk_signal_connect (GTK_OBJECT (dest),
			    "changed",
			    destination_changed_proxy,
			    model);
}

static void
disconnect_destination (ESelectNamesModel *model, EDestination *dest)
{
	gtk_signal_disconnect_by_func (GTK_OBJECT (dest), destination_changed_proxy, model);
}

gboolean
e_select_names_model_contains (ESelectNamesModel *model, const EDestination *dest)
{
	GList *iter;

	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), FALSE);
	g_return_val_if_fail (E_IS_DESTINATION (dest), FALSE);

	for (iter = model->priv->data; iter != NULL; iter = g_list_next (iter)) {
		if (iter->data != NULL && e_destination_equal (dest, E_DESTINATION (iter->data)))
			return TRUE;
	}

	return FALSE;
}

void
e_select_names_model_insert (ESelectNamesModel *model, gint index, EDestination *dest)
{
	g_return_if_fail (model != NULL);
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (0 <= index && index <= g_list_length (model->priv->data));
	g_return_if_fail (dest && E_IS_DESTINATION (dest));

	if (e_select_names_model_at_limit (model)) {
		/* FIXME: This is bad. */
		gtk_object_unref (GTK_OBJECT (dest));
		return;
	}

	connect_destination (model, dest);

	model->priv->data = g_list_insert (model->priv->data, dest, index);
	
	gtk_object_ref (GTK_OBJECT (dest));
	gtk_object_sink (GTK_OBJECT (dest));

	e_select_names_model_changed (model);
}

void
e_select_names_model_append (ESelectNamesModel *model, EDestination *dest)
{
	g_return_if_fail (model && E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (dest && E_IS_DESTINATION (dest));

	if (e_select_names_model_at_limit (model)) {
		/* FIXME: This is bad. */
		gtk_object_unref (GTK_OBJECT (dest));
		return;
	}

	connect_destination (model, dest);

	model->priv->data = g_list_append (model->priv->data, dest);

	gtk_object_ref  (GTK_OBJECT (dest));
	gtk_object_sink (GTK_OBJECT (dest));

	e_select_names_model_changed (model);
}

void
e_select_names_model_replace (ESelectNamesModel *model, gint index, EDestination *dest)
{
	GList *node;
	const gchar *new_str, *old_str;
	gint old_strlen=0, new_strlen=0;

	g_return_if_fail (model != NULL);
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (model->priv->data == NULL || (0 <= index && index < g_list_length (model->priv->data)));
	g_return_if_fail (dest && E_IS_DESTINATION (dest));
	
	new_str = e_destination_get_textrep (dest);
	new_strlen = new_str ? strlen (new_str) : 0;

	if (model->priv->data == NULL) {

		connect_destination (model, dest);

		model->priv->data = g_list_append (model->priv->data, dest);
		gtk_object_ref (GTK_OBJECT (dest));
		gtk_object_sink (GTK_OBJECT (dest));

	} else {
		
		node = g_list_nth (model->priv->data, index);

		if (node->data != dest) {

			disconnect_destination (model, E_DESTINATION (node->data));
			connect_destination (model, dest);

			old_str = e_destination_get_textrep (E_DESTINATION (node->data));
			old_strlen = old_str ? strlen (old_str) : 0;

			gtk_object_unref (GTK_OBJECT (node->data));

			node->data = dest;
			gtk_object_ref (GTK_OBJECT (dest));
			gtk_object_sink (GTK_OBJECT (dest));
		}
	}

	e_select_names_model_changed (model);

	gtk_signal_emit (GTK_OBJECT (model), e_select_names_model_signals[E_SELECT_NAMES_MODEL_RESIZED],
			 index, old_strlen, new_strlen);
}

void
e_select_names_model_delete (ESelectNamesModel *model, gint index)
{
	GList *node;
	EDestination *dest;

	g_return_if_fail (model != NULL);
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (0 <= index && index < g_list_length (model->priv->data));
	
	node = g_list_nth (model->priv->data, index);
	dest = E_DESTINATION (node->data);

	disconnect_destination (model, dest);
	gtk_object_unref (GTK_OBJECT (dest));

	model->priv->data = g_list_remove_link (model->priv->data, node);
	g_list_free_1 (node);

	e_select_names_model_changed (model);
}

void
e_select_names_model_clean (ESelectNamesModel *model)
{
	GList *iter, *next;
	gboolean changed = FALSE;

	g_return_if_fail (model != NULL && E_IS_SELECT_NAMES_MODEL (model));

	iter = model->priv->data;

	while (iter) {
		EDestination *dest;

		next = g_list_next (iter);
		dest = iter->data ? E_DESTINATION (iter->data) : NULL;

		if (dest == NULL || e_destination_is_empty (dest)) {
			if (dest) {
				disconnect_destination (model, dest);
				gtk_object_unref (GTK_OBJECT (dest));
			}
			model->priv->data = g_list_remove_link (model->priv->data, iter);
			g_list_free_1 (iter);
			changed = TRUE;
		}
		
		iter = next;
	}

	if (changed)
		e_select_names_model_changed (model);
}

static void
delete_all_iter (gpointer data, gpointer closure)
{
	disconnect_destination (E_SELECT_NAMES_MODEL (closure), E_DESTINATION (data));
	gtk_object_unref (GTK_OBJECT (data));
}

void
e_select_names_model_delete_all (ESelectNamesModel *model)
{
	g_return_if_fail (model != NULL && E_IS_SELECT_NAMES_MODEL (model));

	g_list_foreach (model->priv->data, delete_all_iter, model);
	g_list_free (model->priv->data);
	model->priv->data = NULL;

	e_select_names_model_changed (model);
}

void
e_select_names_model_overwrite_copy (ESelectNamesModel *dest, ESelectNamesModel *src)
{
	gint i, len;

	g_return_if_fail (dest && E_IS_SELECT_NAMES_MODEL (dest));
	g_return_if_fail (src && E_IS_SELECT_NAMES_MODEL (src));

	if (src == dest)
		return;

	e_select_names_model_delete_all (dest);
	len = e_select_names_model_count (src);
	for (i = 0; i < len; ++i) {
		const EDestination *d = e_select_names_model_get_destination (src, i);
		if (d)
			e_select_names_model_append (dest, e_destination_copy (d));
	}
}

void
e_select_names_model_merge (ESelectNamesModel *dest, ESelectNamesModel *src)
{
	gint i, len;

	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (dest));
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (src));

	if (src == dest)
		return;

	len = e_select_names_model_count (src);
	for (i = 0; i < len; ++i) {
		const EDestination *d = e_select_names_model_get_destination (src, i);
		if (d && !e_select_names_model_contains (dest, d))
			e_select_names_model_append (dest, e_destination_copy (d));
	}
}

void
e_select_names_model_name_pos (ESelectNamesModel *model, gint index, gint *pos, gint *length)
{
	gint rp = 0, i, len = 0;
	GList *iter;
	const gchar *str;

	g_return_if_fail (model != NULL);
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));

	i = 0;
	iter = model->priv->data;
	while (iter && i <= index) {
		rp += len + (i > 0 ? SEPLEN : 0);
		str = e_destination_get_textrep (E_DESTINATION (iter->data));
		len = str ? strlen (str) : 0;
		++i;
		iter = g_list_next (iter);
	}
	
	if (i <= index) {
		rp = -1;
		len = 0;
	}
	
	if (pos)
		*pos = rp;
	if (length)
		*length = len;
}

void
e_select_names_model_text_pos (ESelectNamesModel *model, gint pos, gint *index, gint *start_pos, gint *length)
{
	GList *iter;
	const gchar *str;
	gint len = 0, i = 0, sp = 0, adj = 0;

	g_return_if_fail (model != NULL);
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));

	iter = model->priv->data;

	while (iter != NULL) {
		str = e_destination_get_textrep (E_DESTINATION (iter->data));
		len = str ? strlen (str) : 0;

		if (sp <= pos && pos <= sp + len + adj) {
			break;
		}

		sp += len + adj + 1;
		adj = 1;
		++i;

		iter = g_list_next (iter);
	}

	if (i != 0)
		++sp; /* skip past "magic space" */

	if (iter == NULL) {
#if 0
		g_print ("text_pos ended NULL\n");
#endif
		i = -1;
		sp = -1;
		len = 0;
	} else {
#if 0
		g_print ("text_pos got index %d\n", i);
#endif
	}

	if (index)
		*index = i;
	if (start_pos)
		*start_pos = sp;
	if (length)
		*length = len;
}

void
e_select_names_model_cardify (ESelectNamesModel *model, EBook *book, gint index, gint delay)
{
	EDestination *dest;

	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (book == NULL || E_IS_BOOK (book));
	g_return_if_fail (0 <= index && index < g_list_length (model->priv->data));

	dest = E_DESTINATION (g_list_nth_data (model->priv->data, index));

	if (!e_destination_is_empty (dest)) {

		if (delay > 0)
			e_destination_cardify_delayed (dest, book, delay);
		else
			e_destination_cardify (dest, book);
	}
}

gboolean
e_select_names_model_uncardify (ESelectNamesModel *model, gint index)
{
	EDestination *dest;
	gboolean rv = FALSE;

	g_return_val_if_fail (E_IS_SELECT_NAMES_MODEL (model), FALSE);
	g_return_val_if_fail (0 <= index && index < g_list_length (model->priv->data), FALSE);

	dest = E_DESTINATION (g_list_nth_data (model->priv->data, index));

	if (!e_destination_is_empty (dest)) {
		EDestination *cpy_dest = e_destination_copy (dest);

		rv = e_destination_uncardify (cpy_dest);

		if (rv) {
			e_select_names_model_replace (model, index, cpy_dest);
		}
		
	}

	return rv;
}

void
e_select_names_model_cancel_cardify (ESelectNamesModel *model, gint index)
{
	EDestination *dest;

	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (0 <= index && index < g_list_length (model->priv->data));

	dest = E_DESTINATION (g_list_nth_data (model->priv->data, index));
	
	e_destination_cancel_cardify (dest);
}

void
e_select_names_model_cardify_all (ESelectNamesModel *model, EBook *book, gint delay)
{
	GList *iter;

	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (book == NULL || E_IS_BOOK (book));

	for (iter = model->priv->data; iter != NULL; iter = g_list_next (iter)) {
		EDestination *dest = E_DESTINATION (iter->data);
		if (!e_destination_is_empty (dest)) {

			if (delay > 0)
				e_destination_cardify_delayed (dest, book, delay);
			else
				e_destination_cardify (dest, book);
		}
	}
}

void
e_select_names_model_cancel_cardify_all (ESelectNamesModel *model)
{
	GList *iter;

	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));

	for (iter = model->priv->data; iter != NULL; iter = g_list_next (iter)) {
		EDestination *dest = E_DESTINATION (iter->data);
		e_destination_cancel_cardify (dest);
	}
}

void
e_select_names_model_freeze (ESelectNamesModel *model)
{
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	
	++model->priv->freeze_count;
}

void
e_select_names_model_thaw (ESelectNamesModel *model)
{
	g_return_if_fail (E_IS_SELECT_NAMES_MODEL (model));
	g_return_if_fail (model->priv->freeze_count > 0);

	--model->priv->freeze_count;
	if (model->priv->pending_changed)
		e_select_names_model_changed (model);
}
