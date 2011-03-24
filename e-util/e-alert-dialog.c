/*
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
 *
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Jonathon Jongsma <jonathon.jongsma@collabora.co.uk>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2009 Intel Corporation
 */

#include "e-alert-dialog.h"

#include "e-util.h"

struct _EAlertDialogPrivate {
	GtkWidget *content_area;  /* not referenced */
	EAlert *alert;
};

enum {
	PROP_0,
	PROP_ALERT
};

G_DEFINE_TYPE (
	EAlertDialog,
	e_alert_dialog,
	GTK_TYPE_DIALOG)

static void
alert_dialog_set_alert (EAlertDialog *dialog,
                        EAlert *alert)
{
	g_return_if_fail (E_IS_ALERT (alert));
	g_return_if_fail (dialog->priv->alert == NULL);

	dialog->priv->alert = g_object_ref (alert);
}

static void
alert_dialog_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ALERT:
			alert_dialog_set_alert (
				E_ALERT_DIALOG (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
alert_dialog_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_ALERT:
			g_value_set_object (
				value, e_alert_dialog_get_alert (
				E_ALERT_DIALOG (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
alert_dialog_dispose (GObject *object)
{
	EAlertDialogPrivate *priv;

	priv = E_ALERT_DIALOG (object)->priv;

	if (priv->alert) {
		g_signal_handlers_disconnect_matched (
			priv->alert, G_SIGNAL_MATCH_DATA,
			0, 0, NULL, NULL, object);
		g_object_unref (priv->alert);
		priv->alert = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_alert_dialog_parent_class)->dispose (object);
}

static void
alert_dialog_constructed (GObject *object)
{
	EAlert *alert;
	EAlertDialog *dialog;
	GtkWidget *action_area;
	GtkWidget *content_area;
	GtkWidget *container;
	GtkWidget *widget;
	PangoAttribute *attr;
	PangoAttrList *list;
	GList *actions;
	const gchar *primary, *secondary;
	gint min_width = -1, prefer_width = -1;
	gint height;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_alert_dialog_parent_class)->constructed (object);

	dialog = E_ALERT_DIALOG (object);
	alert = e_alert_dialog_get_alert (dialog);

	gtk_window_set_title (GTK_WINDOW (dialog), " ");

	action_area = gtk_dialog_get_action_area (GTK_DIALOG (dialog));
	content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	gtk_widget_ensure_style (GTK_WIDGET (dialog));
	gtk_container_set_border_width (GTK_CONTAINER (action_area), 12);
	gtk_container_set_border_width (GTK_CONTAINER (content_area), 0);

	gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);

	/* Forward EAlert::response signals to GtkDialog::response. */
	g_signal_connect_swapped (
		alert, "response",
		G_CALLBACK (gtk_dialog_response), dialog);

	/* Add buttons from actions. */
	actions = e_alert_peek_actions (alert);
	while (actions != NULL) {
		GtkWidget *button;

		/* These actions are already wired to trigger an
		 * EAlert::response signal when activated, which
		 * will in turn call to gtk_dialog_response(),
		 * so we can add buttons directly to the action
		 * area without knowing their response IDs. */

		button = gtk_button_new ();

		gtk_activatable_set_related_action (
			GTK_ACTIVATABLE (button),
			GTK_ACTION (actions->data));

		gtk_box_pack_end (
			GTK_BOX (action_area),
			button, FALSE, FALSE, 0);

		actions = g_list_next (actions);
	}

	if (e_alert_get_default_response (alert))
		gtk_dialog_set_default_response (
			GTK_DIALOG (dialog),
			e_alert_get_default_response (alert));

	widget = gtk_hbox_new (FALSE, 12);
	gtk_container_set_border_width (GTK_CONTAINER (widget), 12);
	gtk_box_pack_start (GTK_BOX (content_area), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	container = widget;

	widget = e_alert_create_image (alert, GTK_ICON_SIZE_DIALOG);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_show (widget);

	widget = gtk_vbox_new (FALSE, 12);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	dialog->priv->content_area = widget;
	gtk_widget_show (widget);

	container = widget;

	primary = e_alert_get_primary_text (alert);
	secondary = e_alert_get_secondary_text (alert);

	list = pango_attr_list_new ();
	attr = pango_attr_scale_new (PANGO_SCALE_LARGE);
	pango_attr_list_insert (list, attr);
	attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
	pango_attr_list_insert (list, attr);

	widget = gtk_label_new (primary);
	gtk_label_set_attributes (GTK_LABEL (widget), list);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_label_set_selectable (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_set_can_focus (widget, FALSE);
	gtk_widget_show (widget);

	widget = gtk_label_new (secondary);
	gtk_label_set_line_wrap (GTK_LABEL (widget), TRUE);
	gtk_label_set_selectable (GTK_LABEL (widget), TRUE);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	gtk_box_pack_start (GTK_BOX (container), widget, FALSE, FALSE, 0);
	gtk_widget_set_can_focus (widget, FALSE);
	gtk_widget_show (widget);

	widget = GTK_WIDGET (dialog);

	height = gtk_widget_get_allocated_height (widget);
	gtk_widget_get_preferred_width_for_height (
		widget, height, &min_width, &prefer_width);
	if (min_width < prefer_width)
		gtk_window_set_default_size (
			GTK_WINDOW (dialog), MIN (
			(min_width + prefer_width) / 2,
			min_width * 5 / 4), -1);

	pango_attr_list_unref (list);
}

static void
e_alert_dialog_class_init (EAlertDialogClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EAlertDialogPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = alert_dialog_set_property;
	object_class->get_property = alert_dialog_get_property;
	object_class->dispose = alert_dialog_dispose;
	object_class->constructed = alert_dialog_constructed;

	g_object_class_install_property (
		object_class,
		PROP_ALERT,
		g_param_spec_object (
			"alert",
			"Alert",
			"Alert to be displayed",
			E_TYPE_ALERT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_alert_dialog_init (EAlertDialog *dialog)
{
	dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		dialog, E_TYPE_ALERT_DIALOG, EAlertDialogPrivate);
}

GtkWidget *
e_alert_dialog_new (GtkWindow *parent,
                    EAlert *alert)
{
	g_return_val_if_fail (E_IS_ALERT (alert), NULL);

	return g_object_new (
		E_TYPE_ALERT_DIALOG,
		"alert", alert, "transient-for", parent, NULL);
}

GtkWidget *
e_alert_dialog_new_for_args (GtkWindow *parent,
                             const gchar *tag,
                             ...)
{
	GtkWidget *dialog;
	EAlert *alert;
	va_list ap;

	g_return_val_if_fail (tag != NULL, NULL);

	va_start (ap, tag);
	alert = e_alert_new_valist (tag, ap);
	va_end (ap);

	dialog = e_alert_dialog_new (parent, alert);

	g_object_unref (alert);

	return dialog;
}

gint
e_alert_run_dialog (GtkWindow *parent,
                    EAlert *alert)
{
	GtkWidget *dialog;
	gint response;

	g_return_val_if_fail (E_IS_ALERT (alert), 0);

	dialog = e_alert_dialog_new (parent, alert);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	return response;
}

gint
e_alert_run_dialog_for_args (GtkWindow *parent,
                             const gchar *tag,
                             ...)
{
	EAlert *alert;
	gint response;
	va_list ap;

	g_return_val_if_fail (tag != NULL, 0);

	va_start (ap, tag);
	alert = e_alert_new_valist (tag, ap);
	va_end (ap);

	response = e_alert_run_dialog (parent, alert);

	g_object_unref (alert);

	return response;
}

/**
 * e_alert_dialog_get_alert:
 * @dialog: an #EAlertDialog
 *
 * Returns the #EAlert associated with @dialog.
 *
 * Returns: the #EAlert associated with @dialog
 **/
EAlert *
e_alert_dialog_get_alert (EAlertDialog *dialog)
{
	g_return_val_if_fail (E_IS_ALERT_DIALOG (dialog), NULL);

	return dialog->priv->alert;
}

/**
 * e_alert_dialog_get_content_area:
 * @dialog: an #EAlertDialog
 *
 * Returns the vertical box containing the primary and secondary labels.
 * Use this to pack additional widgets into the dialog with the proper
 * horizontal alignment (maintaining the left margin below the image).
 *
 * Returns: the content area #GtkBox
 **/
GtkWidget *
e_alert_dialog_get_content_area (EAlertDialog *dialog)
{
	g_return_val_if_fail (E_IS_ALERT_DIALOG (dialog), NULL);

	return dialog->priv->content_area;
}
