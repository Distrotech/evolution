/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * E-table-subset.c: Implements a table that contains a subset of another table.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999 Helix Code, Inc.
 */
#include <config.h>
#include <stdlib.h>
#include <gtk/gtksignal.h>
#include "gal/util/e-util.h"
#include "e-table-subset.h"

#define PARENT_TYPE E_TABLE_MODEL_TYPE

static ETableModelClass *etss_parent_class;

static void
etss_destroy (GtkObject *object)
{
	ETableSubset *etss = E_TABLE_SUBSET (object);

	if (etss->source)
		gtk_object_unref (GTK_OBJECT (etss->source));

	gtk_signal_disconnect (GTK_OBJECT (etss->source),
			       etss->table_model_pre_change_id);
	gtk_signal_disconnect (GTK_OBJECT (etss->source),
			       etss->table_model_changed_id);
	gtk_signal_disconnect (GTK_OBJECT (etss->source),
			       etss->table_model_row_changed_id);
	gtk_signal_disconnect (GTK_OBJECT (etss->source),
			       etss->table_model_cell_changed_id);

	etss->table_model_pre_change_id = 0;
	etss->table_model_changed_id = 0;
	etss->table_model_row_changed_id = 0;
	etss->table_model_cell_changed_id = 0;

	g_free (etss->map_table);

	GTK_OBJECT_CLASS (etss_parent_class)->destroy (object);
}

static int
etss_column_count (ETableModel *etm)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return e_table_model_column_count (etss->source);
}

static int
etss_row_count (ETableModel *etm)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return etss->n_map;
}

static void *
etss_value_at (ETableModel *etm, int col, int row)
{
	ETableSubset *etss = (ETableSubset *)etm;

	etss->last_access = row;
	return e_table_model_value_at (etss->source, col, etss->map_table [row]);
}

static void
etss_set_value_at (ETableModel *etm, int col, int row, const void *val)
{
	ETableSubset *etss = (ETableSubset *)etm;

	etss->last_access = row;
	return e_table_model_set_value_at (etss->source, col, etss->map_table [row], val);
}

static gboolean
etss_is_cell_editable (ETableModel *etm, int col, int row)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return e_table_model_is_cell_editable (etss->source, col, etss->map_table [row]);
}

static void
etss_append_row (ETableModel *etm, ETableModel *source, int row)
{
	ETableSubset *etss = (ETableSubset *)etm;
	e_table_model_append_row (etss->source, source, row);
}

static void *
etss_duplicate_value (ETableModel *etm, int col, const void *value)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return e_table_model_duplicate_value (etss->source, col, value);
}

static void
etss_free_value (ETableModel *etm, int col, void *value)
{
	ETableSubset *etss = (ETableSubset *)etm;

	e_table_model_free_value (etss->source, col, value);
}

static void *
etss_initialize_value (ETableModel *etm, int col)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return e_table_model_initialize_value (etss->source, col);
}

static gboolean
etss_value_is_empty (ETableModel *etm, int col, const void *value)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return e_table_model_value_is_empty (etss->source, col, value);
}

static char *
etss_value_to_string (ETableModel *etm, int col, const void *value)
{
	ETableSubset *etss = (ETableSubset *)etm;

	return e_table_model_value_to_string (etss->source, col, value);
}

static void
etss_class_init (GtkObjectClass *klass)
{
	ETableModelClass *table_class = (ETableModelClass *) klass;

	etss_parent_class = gtk_type_class (PARENT_TYPE);
	
	klass->destroy = etss_destroy;

	table_class->column_count     = etss_column_count;
	table_class->row_count        = etss_row_count;
	table_class->value_at         = etss_value_at;
	table_class->set_value_at     = etss_set_value_at;
	table_class->is_cell_editable = etss_is_cell_editable;
	table_class->append_row       = etss_append_row;
	table_class->duplicate_value  = etss_duplicate_value;
	table_class->free_value       = etss_free_value;
	table_class->initialize_value = etss_initialize_value;
	table_class->value_is_empty   = etss_value_is_empty;
	table_class->value_to_string  = etss_value_to_string;
}

static void
etss_init (ETableSubset *etss)
{
	etss->last_access = 0;
}

E_MAKE_TYPE(e_table_subset, "ETableSubset", ETableSubset, etss_class_init, etss_init, PARENT_TYPE);

static void
etss_proxy_model_pre_change (ETableModel *etm, ETableSubset *etss)
{
	e_table_model_pre_change (E_TABLE_MODEL (etss));
}

static void
etss_proxy_model_changed (ETableModel *etm, ETableSubset *etss)
{
	e_table_model_changed (E_TABLE_MODEL (etss));
}

static void
etss_proxy_model_row_changed (ETableModel *etm, int row, ETableSubset *etss)
{
	int limit;
	const int n = etss->n_map;
	const int * const map_table = etss->map_table;
	int i;

	limit = MIN(n, etss->last_access + 10);
	for (i = etss->last_access; i < limit; i++) {
		if (map_table [i] == row){
			e_table_model_row_changed (E_TABLE_MODEL (etss), i);
			etss->last_access = i;
			return;
		}
	}

	limit = MAX(0, etss->last_access - 10);
	for (i = etss->last_access - 1; i >= limit; i--) {
		if (map_table [i] == row){
			e_table_model_row_changed (E_TABLE_MODEL (etss), i);
			etss->last_access = i;
			return;
		}
	}

	for (i = 0; i < n; i++){
		if (map_table [i] == row){
			e_table_model_row_changed (E_TABLE_MODEL (etss), i);
			etss->last_access = i;
			return;
		}
	}
}

static void
etss_proxy_model_cell_changed (ETableModel *etm, int col, int row, ETableSubset *etss)
{
	int limit;
	const int n = etss->n_map;
	const int * const map_table = etss->map_table;
	int i;

	limit = MIN(n, etss->last_access + 10);
	for (i = etss->last_access; i < limit; i++) {
		if (map_table [i] == row){
			e_table_model_cell_changed (E_TABLE_MODEL (etss), col, i);
			etss->last_access = i;
			return;
		}
	}

	limit = MAX(0, etss->last_access - 10);
	for (i = etss->last_access - 1; i >= limit; i--) {
		if (map_table [i] == row){
			e_table_model_cell_changed (E_TABLE_MODEL (etss), col, i);
			etss->last_access = i;
			return;
		}
	}
		
	for (i = 0; i < n; i++){
		if (map_table [i] == row){
			e_table_model_cell_changed (E_TABLE_MODEL (etss), col, i);
			etss->last_access = i;
			return;
		}
	}
}

ETableModel *
e_table_subset_construct (ETableSubset *etss, ETableModel *source, int nvals)
{
	unsigned int *buffer;
	int i;

	buffer = (unsigned int *) g_malloc (sizeof (unsigned int) * nvals);
	if (buffer == NULL)
		return NULL;
	etss->map_table = buffer;
	etss->n_map = nvals;
	etss->source = source;
	gtk_object_ref (GTK_OBJECT (source));
	
	/* Init */
	for (i = 0; i < nvals; i++)
		etss->map_table [i] = i;

	etss->table_model_pre_change_id = gtk_signal_connect (GTK_OBJECT (source), "model_pre_change",
							      GTK_SIGNAL_FUNC (etss_proxy_model_pre_change), etss);
	etss->table_model_changed_id = gtk_signal_connect (GTK_OBJECT (source), "model_changed",
						     GTK_SIGNAL_FUNC (etss_proxy_model_changed), etss);
	etss->table_model_row_changed_id = gtk_signal_connect (GTK_OBJECT (source), "model_row_changed",
							 GTK_SIGNAL_FUNC (etss_proxy_model_row_changed), etss);
	etss->table_model_cell_changed_id = gtk_signal_connect (GTK_OBJECT (source), "model_cell_changed",
							  GTK_SIGNAL_FUNC (etss_proxy_model_cell_changed), etss);
	
	return E_TABLE_MODEL (etss);
}

ETableModel *
e_table_subset_new (ETableModel *source, const int nvals)
{
	ETableSubset *etss = gtk_type_new (E_TABLE_SUBSET_TYPE);

	if (e_table_subset_construct (etss, source, nvals) == NULL){
		gtk_object_destroy (GTK_OBJECT (etss));
		return NULL;
	}

	return (ETableModel *) etss;
}

ETableModel *
e_table_subset_get_toplevel (ETableSubset *table)
{
	g_return_val_if_fail (table != NULL, NULL);
	g_return_val_if_fail (E_IS_TABLE_SUBSET (table), NULL);

	if (E_IS_TABLE_SUBSET (table->source))
		return e_table_subset_get_toplevel (E_TABLE_SUBSET (table->source));
	else
		return table->source;
}
