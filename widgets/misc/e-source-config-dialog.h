/*
 * e-source-config-dialog.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_SOURCE_CONFIG_DIALOG_H
#define E_SOURCE_CONFIG_DIALOG_H

#include <misc/e-source-config.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CONFIG_DIALOG \
	(e_source_config_dialog_get_type ())
#define E_SOURCE_CONFIG_DIALOG(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CONFIG_DIALOG, ESourceConfigDialog))
#define E_SOURCE_CONFIG_DIALOG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CONFIG_DIALOG, ESourceConfigDialogClass))
#define E_IS_SOURCE_CONFIG_DIALOG(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CONFIG_DIALOG))
#define E_IS_SOURCE_CONFIG_DIALOG_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CONFIG_DIALOG))
#define E_SOURCE_CONFIG_DIALOG_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CONFIG_DIALOG, ESourceConfigDialogClass))

G_BEGIN_DECLS

typedef struct _ESourceConfigDialog ESourceConfigDialog;
typedef struct _ESourceConfigDialogClass ESourceConfigDialogClass;
typedef struct _ESourceConfigDialogPrivate ESourceConfigDialogPrivate;

struct _ESourceConfigDialog {
	GtkDialog parent;
	ESourceConfigDialogPrivate *priv;
};

struct _ESourceConfigDialogClass {
	GtkDialogClass parent_class;
};

GType		e_source_config_dialog_get_type	(void) G_GNUC_CONST;
GtkWidget *	e_source_config_dialog_new	(ESourceConfig *config);
ESourceConfig *	e_source_config_dialog_get_config
						(ESourceConfigDialog *dialog);

G_END_DECLS

#endif /* E_SOURCE_CONFIG_DIALOG_H */
