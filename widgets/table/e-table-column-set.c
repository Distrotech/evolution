/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * E-table-col-head.c: TableColHead implementation
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999 Helix Code, Inc
 */
#include <config.h>
#include <string.h>
#include <gtk/gtkobject.h>
#include <gtk/gtksignal.h>
#include "e-table-column-set.h"

/* The arguments we take */

static void etcs_set_arg (GtkObject *object, GtkArg *arg, guint arg_id);
static void etcs_get_arg (GtkObject *object, GtkArg *arg, guint arg_id);

static GtkObjectClass *e_table_column_set_parent_class;

struct _SetFuncAndData {
	ETableColumnSet     *etcs;
	ETableColumnSetFunc  func;
	gpointer             user_data;
};

static void
etcs_destroy_callback(gpointer key, gpointer value, gpointer user_data)
{
	g_free(key);
	if (value)
		gtk_object_unref(GTK_OBJECT(value));
}

static void
etcs_destroy (GtkObject *object)
{
	ETableColumnSet *etcs = E_TABLE_COLUMN_SET (object);

	g_hash_table_foreach(etcs->columns, etcs_destroy_callback, NULL);

	if (e_table_column_set_parent_class->destroy)
		e_table_column_set_parent_class->destroy (object);
}

static void
e_table_column_set_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = etcs_destroy;
	object_class->set_arg = etcs_set_arg;
	object_class->get_arg = etcs_get_arg;


	e_table_column_set_parent_class = (gtk_type_class (gtk_object_get_type ()));
}

static void
e_table_column_set_init (ETableColumnSet *etcs)
{
	etcs->columns = g_hash_table_new(g_str_hash, g_str_equal);
}

GtkType
e_table_column_set_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"ETableColumnSet",
			sizeof (ETableColumnSet),
			sizeof (ETableColumnSetClass),
			(GtkClassInitFunc) e_table_column_set_class_init,
			(GtkObjectInitFunc) e_table_column_set_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_object_get_type (), &info);
	}

	return type;
}

ETableColumnSet *
e_table_column_set_new (void)
{
	ETableColumnSet *etcs;

	etcs = gtk_type_new (e_table_column_set_get_type ());

	return etcs;
}

static void
etcs_set_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	/*	ETableColumnSet *etcs = E_TABLE_COLUMN_SET (object);*/

	switch (arg_id) {
	default:
		break;
	}
}

static void
etcs_get_arg (GtkObject *object, GtkArg *arg, guint arg_id)
{
	/* 	ETableColumnSet *etcs = E_TABLE_COLUMN_SET (object);*/

	switch (arg_id) {
	default:
		arg->type = GTK_TYPE_INVALID;
		break;
	}
}

void
e_table_column_set_add_column (ETableColumnSet *etcs, ETableCol *etc, const char *name)
{
	g_return_if_fail (etcs != NULL);
	g_return_if_fail (E_IS_TABLE_COLUMN_SET (etcs));
	g_return_if_fail (etc != NULL);
	g_return_if_fail (E_IS_TABLE_COL (etc));
	g_return_if_fail (name != NULL);

	/*
	 * We are the primary owners of the column
	 */
	gtk_object_ref (GTK_OBJECT (etc));
	gtk_object_sink (GTK_OBJECT (etc));

	g_hash_table_insert(etcs->columns, g_strdup(name), etc);
}

ETableCol *
e_table_column_set_get_column (ETableColumnSet *etcs, const char *name)
{
	g_return_val_if_fail (etcs != NULL, NULL);
	g_return_val_if_fail (E_IS_TABLE_COLUMN_SET (etcs), NULL);

	return g_hash_table_lookup(etcs->columns, name);
}

void
e_table_column_set_remove_column (ETableColumnSet *etcs, const char *name)
{
	char *orig_name;
	ETableCol *col;
	gpointer orig_key;
	gpointer orig_value;

	g_return_if_fail (etcs != NULL);
	g_return_if_fail (E_IS_TABLE_COLUMN_SET (etcs));
	g_return_if_fail (name != NULL);

	if (g_hash_table_lookup_extended(etcs->columns, name, &orig_key, &orig_value)) {
		orig_name = orig_key;
		col = orig_value;
		g_hash_table_remove(etcs->columns, name);
		g_free(orig_name);
		if (col)
			gtk_object_unref(GTK_OBJECT(col));
	}
}

static void
etcs_foreach_callback(gpointer key, gpointer value, gpointer user_data)
{
	struct _SetFuncAndData *sfd = user_data;
	sfd->func(sfd->etcs, E_TABLE_COL(value), (char *) key, sfd->user_data);
}

void
e_table_column_set_columns_foreach (ETableColumnSet     *etcs,
				    ETableColumnSetFunc  func,
				    gpointer             user_data)
{
	struct _SetFuncAndData sfd;
	sfd.etcs = etcs;
	sfd.func = func;
	sfd.user_data = user_data;
	g_hash_table_foreach (etcs->columns, etcs_foreach_callback, &sfd);
}
