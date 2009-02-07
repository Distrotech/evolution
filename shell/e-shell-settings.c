/*
 * e-shell-settings.c
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
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include "e-shell-settings.h"

#include "e-util/gconf-bridge.h"

#define E_SHELL_SETTINGS_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SHELL_SETTINGS, EShellSettingsPrivate))

struct _EShellSettingsPrivate {
	GArray *value_array;
	guint debug	: 1;
};

static GList *instances;
static guint property_count;
static gpointer parent_class;

static void
shell_settings_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	EShellSettingsPrivate *priv;
	GValue *dest_value;

	priv = E_SHELL_SETTINGS_GET_PRIVATE (object);

	dest_value = &g_array_index (
		priv->value_array, GValue, property_id - 1);

	g_value_copy (value, dest_value);
	g_object_notify (object, pspec->name);

	if (priv->debug) {
		gchar *contents;

		contents = g_strdup_value_contents (value);
		g_debug (
			"Setting '%s' set to '%s' (%s)",
			pspec->name, contents, G_VALUE_TYPE_NAME (value));
		g_free (contents);
	}
}

static void
shell_settings_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	EShellSettingsPrivate *priv;
	GValue *src_value;

	priv = E_SHELL_SETTINGS_GET_PRIVATE (object);

	src_value = &g_array_index (
		priv->value_array, GValue, property_id - 1);

	g_value_copy (src_value, value);
}

static void
shell_settings_finalize (GObject *object)
{
	EShellSettingsPrivate *priv;
	guint ii;

	priv = E_SHELL_SETTINGS_GET_PRIVATE (object);

	for (ii = 0; ii < priv->value_array->len; ii++)
		g_value_unset (&g_array_index (priv->value_array, GValue, ii));

	g_array_free (priv->value_array, TRUE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
shell_settings_class_init (EShellSettingsClass *class)
{
	GObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (EShellSettingsPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = shell_settings_set_property;
	object_class->get_property = shell_settings_get_property;
	object_class->finalize = shell_settings_finalize;
}

static void
shell_settings_init (EShellSettings *shell_settings,
                     GObjectClass *object_class)
{
	GArray *value_array;
	GParamSpec **pspecs;
	guint ii;

	instances = g_list_prepend (instances, shell_settings);

	value_array = g_array_new (FALSE, TRUE, sizeof (GValue));
	g_array_set_size (value_array, property_count);

	shell_settings->priv = E_SHELL_SETTINGS_GET_PRIVATE (shell_settings);
	shell_settings->priv->value_array = value_array;

	g_object_freeze_notify (G_OBJECT (shell_settings));

	pspecs = g_object_class_list_properties (object_class, NULL);
	for (ii = 0; ii < property_count; ii++) {
		GParamSpec *pspec = pspecs[ii];
		GValue *value;

		value = &g_array_index (value_array, GValue, ii);
		g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));
		g_param_value_set_default (pspec, value);
		g_object_notify (G_OBJECT (shell_settings), pspec->name);
	}
	g_free (pspecs);

	g_object_thaw_notify (G_OBJECT (shell_settings));
}

GType
e_shell_settings_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		const GTypeInfo type_info = {
			sizeof (EShellSettingsClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) shell_settings_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,  /* class_data */
			sizeof (EShellSettings),
			0,     /* n_preallocs */
			(GInstanceInitFunc) shell_settings_init,
			NULL   /* value_table */
		};

		type = g_type_register_static (
			G_TYPE_OBJECT, "EShellSettings", &type_info, 0);
	}

	return type;
}

/**
 * e_shell_settings_install_property:
 * @pspec: a #GParamSpec
 *
 * Installs a new class property for #EShellSettings.  This is usually
 * done during initialization of a #EShellModule or plugin, followed by
 * a call to e_shell_settings_bind_to_gconf() to bind the property to a
 * GConf key.
 **/
void
e_shell_settings_install_property (GParamSpec *pspec)
{
	static GObjectClass *class = NULL;
	GList *iter, *next;

	g_return_if_fail (G_IS_PARAM_SPEC (pspec));

	if (G_UNLIKELY (class == NULL))
		class = g_type_class_ref (E_TYPE_SHELL_SETTINGS);

	if (g_object_class_find_property (class, pspec->name) != NULL) {
		g_warning (
			"Settings property \"%s\" already exists",
			pspec->name);
		return;
	}

	for (iter = instances; iter != NULL; iter = iter->next)
		g_object_freeze_notify (iter->data);

	g_object_class_install_property (class, ++property_count, pspec);

	for (iter = instances; iter != NULL; iter = iter->next) {
		EShellSettings *shell_settings = iter->data;
		GArray *value_array;
		GValue *value;

		value_array = shell_settings->priv->value_array;
		g_array_set_size (value_array, property_count);

		value = &g_array_index (
			value_array, GValue, property_count - 1);
		g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));
		g_param_value_set_default (pspec, value);
		g_object_notify (G_OBJECT (shell_settings), pspec->name);
	}

	for (iter = instances; iter != NULL; iter = next) {
		next = iter->next;
		g_object_thaw_notify (iter->data);
	}
}

/**
 * e_shell_settings_bind_to_gconf:
 * @shell_settings: an #EShellSettings
 * @property_name: the name of the property to bind
 * @gconf_key: the GConf key to bind the property to
 *
 * Binds @property_name to @gconf_key, causing them to have the same value
 * at all times.
 *
 * The types of @property_name and @gconf_key should be compatible.  Floats
 * and doubles, and ints, uints, longs, unlongs, int64s, uint64s, chars,
 * uchars and enums can be matched up.  Booleans and strings can only be
 * matched to their respective types.
 *
 * On calling this function, @property_name is initialized to the current
 * value of @gconf_key.
 **/
void
e_shell_settings_bind_to_gconf (EShellSettings *shell_settings,
                                const gchar *property_name,
                                const gchar *gconf_key)
{
	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));
	g_return_if_fail (property_name != NULL);
	g_return_if_fail (gconf_key != NULL);

	gconf_bridge_bind_property (
		gconf_bridge_get (), gconf_key,
		G_OBJECT (shell_settings), property_name);
}

/**
 * e_shell_settings_enable_debug:
 * @shell_settings: an #EShellSettings
 *
 * Print a debug message to standard output when a property value changes.
 **/
void
e_shell_settings_enable_debug (EShellSettings *shell_settings)
{
	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));

	shell_settings->priv->debug = TRUE;
}

/**
 * e_shell_settings_get_boolean:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 *
 * Return the contents of an #EShellSettings property of type
 * #G_TYPE_BOOLEAN.
 *
 * Returns: boolean contents of @property_name
 **/
gboolean
e_shell_settings_get_boolean (EShellSettings *shell_settings,
                              const gchar *property_name)
{
	GObject *object;
	GValue value = { 0, };
	gboolean v_boolean;

	g_return_val_if_fail (E_IS_SHELL_SETTINGS (shell_settings), FALSE);
	g_return_val_if_fail (property_name != NULL, FALSE);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_object_get_property (object, property_name, &value);
	v_boolean = g_value_get_boolean (&value);
	g_value_unset (&value);

	return v_boolean;
}

/**
 * e_shell_settings_set_boolean:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 * @v_boolean: boolean value to be set
 *
 * Sets the contents of an #EShellSettings property of type #G_TYPE_BOOLEAN
 * to @v_boolean.  If @property_name is bound to a GConf key, the GConf key
 * will also be set to @v_boolean.
 **/
void
e_shell_settings_set_boolean (EShellSettings *shell_settings,
                              const gchar *property_name,
                              gboolean v_boolean)
{
	GObject *object;
	GValue value = { 0, };

	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));
	g_return_if_fail (property_name != NULL);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, v_boolean);
	g_object_set_property (object, property_name, &value);
	g_value_unset (&value);
}

/**
 * e_shell_settings_get_int:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 *
 * Returns the contents of an #EShellSettings property of type
 * #G_TYPE_INT.
 *
 * Returns: integer contents of @property_name
 **/
gint
e_shell_settings_get_int (EShellSettings *shell_settings,
                          const gchar *property_name)
{
	GObject *object;
	GValue value = { 0, };
	gint v_int;

	g_return_val_if_fail (E_IS_SHELL_SETTINGS (shell_settings), 0);
	g_return_val_if_fail (property_name != NULL, 0);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_INT);
	g_object_get_property (object, property_name, &value);
	v_int = g_value_get_int (&value);
	g_value_unset (&value);

	return v_int;
}

/**
 * e_shell_settings_set_int:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 * @v_int: integer value to be set
 *
 * Sets the contents of an #EShellSettings property of type #G_TYPE_INT
 * to @v_int.  If @property_name is bound to a GConf key, the GConf key
 * will also be set to @v_int.
 **/
void
e_shell_settings_set_int (EShellSettings *shell_settings,
                          const gchar *property_name,
                          gint v_int)
{
	GObject *object;
	GValue value = { 0, };

	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));
	g_return_if_fail (property_name != NULL);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, v_int);
	g_object_set_property (object, property_name, &value);
	g_value_unset (&value);
}

/**
 * e_shell_settings_get_string:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 *
 * Returns the contents of an #EShellSettings property of type
 * #G_TYPE_STRING.  The returned string should be freed using g_free().
 *
 * Returns: string contents of @property_name
 **/
gchar *
e_shell_settings_get_string (EShellSettings *shell_settings,
                             const gchar *property_name)
{
	GObject *object;
	GValue value = { 0, };
	gchar *v_string;

	g_return_val_if_fail (E_IS_SHELL_SETTINGS (shell_settings), NULL);
	g_return_val_if_fail (property_name != NULL, NULL);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_STRING);
	g_object_get_property (object, property_name, &value);
	v_string = g_value_dup_string (&value);
	g_value_unset (&value);

	return v_string;
}

/**
 * e_shell_settings_set_string:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 * @v_string: string to be set
 *
 * Sets the contents of an #EShellSettings property of type #G_TYPE_STRING
 * to @v_string.  If @property_name is bound to a GConf key, the GConf key
 * will also be set to @v_string.
 **/
void
e_shell_settings_set_string (EShellSettings *shell_settings,
                             const gchar *property_name,
                             const gchar *v_string)
{
	GObject *object;
	GValue value = { 0, };

	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));
	g_return_if_fail (property_name != NULL);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, v_string);
	g_object_set_property (object, property_name, &value);
	g_value_unset (&value);
}

/**
 * e_shell_settings_get_object:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 *
 * Returns the contents of an #EShellSettings property of type
 * #G_TYPE_OBJECT.  The caller owns the reference to the returned
 * object, and should call g_object_unref() when finished with it.
 *
 * Returns: a new reference to the object under @property_name
 **/
gpointer
e_shell_settings_get_object (EShellSettings *shell_settings,
                             const gchar *property_name)
{
	GObject *object;
	GValue value = { 0, };
	gpointer v_object;

	g_return_val_if_fail (E_IS_SHELL_SETTINGS (shell_settings), NULL);
	g_return_val_if_fail (property_name != NULL, NULL);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_OBJECT);
	g_object_get_property (object, property_name, &value);
	v_object = g_value_dup_object (&value);
	g_value_unset (&value);

	return v_object;
}

/**
 * e_shell_settings_set_object:
 * @shell_settings: an #EShellSettings
 * @property_name: an installed property name
 * @v_object: object to be set
 *
 * Sets the contents of an #EShellSettings property of type #G_TYPE_OBJECT
 * to @v_object.
 **/
void
e_shell_settings_set_object (EShellSettings *shell_settings,
                             const gchar *property_name,
                             gpointer v_object)
{
	GObject *object;
	GValue value = { 0, };

	g_return_if_fail (E_IS_SHELL_SETTINGS (shell_settings));
	g_return_if_fail (property_name != NULL);

	object = G_OBJECT (shell_settings);
	g_value_init (&value, G_TYPE_OBJECT);
	g_value_set_object (&value, v_object);
	g_object_set_property (object, property_name, &value);
	g_value_unset (&value);
}
