/* Evolution calendar - Data model for ETable
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_MODEL_H
#define E_CAL_MODEL_H

#include <gal/e-table/e-table-model.h>
#include <cal-client/cal-client.h>
#include "e-cell-date-edit-text.h"

G_BEGIN_DECLS

#define E_TYPE_CAL_MODEL            (e_cal_model_get_type ())
#define E_CAL_MODEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_MODEL, ECalModel))
#define E_CAL_MODEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_MODEL, ECalModelClass))
#define E_IS_CAL_MODEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_MODEL))
#define E_IS_CAL_MODEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_MODEL))

typedef struct _ECalModelPrivate ECalModelPrivate;

typedef enum {
	E_CAL_MODEL_FIELD_CATEGORIES,
	E_CAL_MODEL_FIELD_CLASSIFICATION,
	E_CAL_MODEL_FIELD_COLOR,            /* not a real field */
	E_CAL_MODEL_FIELD_COMPONENT,        /* not a real field */
	E_CAL_MODEL_FIELD_DESCRIPTION,
	E_CAL_MODEL_FIELD_DTSTART,
	E_CAL_MODEL_FIELD_HAS_ALARMS,       /* not a real field */
	E_CAL_MODEL_FIELD_ICON,             /* not a real field */
	E_CAL_MODEL_FIELD_SUMMARY,
	E_CAL_MODEL_FIELD_UID,
	E_CAL_MODEL_FIELD_LAST
} ECalModelField;

typedef struct {
	CalClient *client;
	icalcomponent *icalcomp;

	/* private data */
	ECellDateEditValue *dtstart;
	ECellDateEditValue *dtend;
} ECalModelComponent;

typedef struct {
	ETableModel model;
	ECalModelPrivate *priv;
} ECalModel;

typedef struct {
	ETableModelClass parent_class;

	/* virtual methods */
	icalcomponent * (* create_component_with_defaults) (ECalModel *model);
	const gchar * (* get_color_for_component) (ECalModel *model, ECalModelComponent *comp_data);
} ECalModelClass;

GType               e_cal_model_get_type (void);

icalcomponent_kind  e_cal_model_get_component_kind (ECalModel *model);
void                e_cal_model_set_component_kind (ECalModel *model, icalcomponent_kind kind);
icaltimezone       *e_cal_model_get_timezone (ECalModel *model);
void                e_cal_model_set_timezone (ECalModel *model, icaltimezone *zone);

void                e_cal_model_add_client (ECalModel *model, CalClient *client);
void                e_cal_model_remove_client (ECalModel *model, CalClient *client);
void                e_cal_model_remove_all_clients (ECalModel *model);

void                e_cal_model_set_query (ECalModel *model, const gchar *sexp);

ECalModelComponent *e_cal_model_get_component_at (ECalModel *model, gint row);

gchar              *e_cal_model_date_value_to_string (ECalModel *model, const void *value);

G_END_DECLS

#endif
