/*
 * e-mail-identity-combo-box.c
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

#include "e-mail-identity-combo-box.h"

#define E_MAIL_IDENTITY_COMBO_BOX_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_MAIL_IDENTITY_COMBO_BOX, EMailIdentityComboBoxPrivate))

#define SOURCE_IS_MAIL_IDENTITY(source) \
	(e_source_has_extension ((source), E_SOURCE_EXTENSION_MAIL_IDENTITY))

struct _EMailIdentityComboBoxPrivate {
	ESourceRegistry *registry;
	guint refresh_idle_id;
};

enum {
	PROP_0,
	PROP_REGISTRY
};

enum {
	COLUMN_DISPLAY_NAME,
	COLUMN_UID
};

G_DEFINE_TYPE (
	EMailIdentityComboBox,
	e_mail_identity_combo_box,
	GTK_TYPE_COMBO_BOX)

static gboolean
mail_identity_combo_box_refresh_idle_cb (EMailIdentityComboBox *combo_box)
{
	/* The refresh function will clear the idle ID. */
	e_mail_identity_combo_box_refresh (combo_box);

	return FALSE;
}

static void
mail_identity_combo_box_registry_changed (ESourceRegistry *registry,
                                          ESource *source,
                                          EMailIdentityComboBox *combo_box)
{
	/* If the ESource in question has a "Mail Identity" extension,
	 * schedule a refresh of the tree model.  Otherwise ignore it.
	 * We use an idle callback to limit how frequently we refresh
	 * the tree model, in case the registry is emitting lots of
	 * signals at once. */

	if (!SOURCE_IS_MAIL_IDENTITY (source))
		return;

	if (combo_box->priv->refresh_idle_id > 0)
		return;

	combo_box->priv->refresh_idle_id = g_idle_add (
		(GSourceFunc) mail_identity_combo_box_refresh_idle_cb,
		combo_box);
}

static void
mail_identity_combo_box_activate_default (EMailIdentityComboBox *combo_box)
{
	ESource *source;
	ESourceRegistry *registry;

	registry = e_mail_identity_combo_box_get_registry (combo_box);
	source = e_source_registry_ref_default_mail_identity (registry);

	if (source != NULL) {
		const gchar *uid = e_source_get_uid (source);
		gtk_combo_box_set_active_id (GTK_COMBO_BOX (combo_box), uid);
		g_object_unref (source);
	}
}

static void
mail_identity_combo_box_set_registry (EMailIdentityComboBox *combo_box,
                                      ESourceRegistry *registry)
{
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));
	g_return_if_fail (combo_box->priv->registry == NULL);

	combo_box->priv->registry = g_object_ref (registry);

	g_signal_connect (
		registry, "source-added",
		G_CALLBACK (mail_identity_combo_box_registry_changed),
		combo_box);

	g_signal_connect (
		registry, "source-changed",
		G_CALLBACK (mail_identity_combo_box_registry_changed),
		combo_box);

	g_signal_connect (
		registry, "source-removed",
		G_CALLBACK (mail_identity_combo_box_registry_changed),
		combo_box);
}

static void
mail_identity_combo_box_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			mail_identity_combo_box_set_registry (
				E_MAIL_IDENTITY_COMBO_BOX (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_identity_combo_box_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_mail_identity_combo_box_get_registry (
				E_MAIL_IDENTITY_COMBO_BOX (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mail_identity_combo_box_dispose (GObject *object)
{
	EMailIdentityComboBoxPrivate *priv;

	priv = E_MAIL_IDENTITY_COMBO_BOX_GET_PRIVATE (object);

	if (priv->registry != NULL) {
		g_signal_handlers_disconnect_matched (
			priv->registry, G_SIGNAL_MATCH_DATA,
			0, 0, NULL, NULL, object);
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	if (priv->refresh_idle_id > 0) {
		g_source_remove (priv->refresh_idle_id);
		priv->refresh_idle_id = 0;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_mail_identity_combo_box_parent_class)->
		dispose (object);
}

static void
mail_identity_combo_box_constructed (GObject *object)
{
	GtkListStore *list_store;
	GtkComboBox *combo_box;
	GtkCellLayout *cell_layout;
	GtkCellRenderer *cell_renderer;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_mail_identity_combo_box_parent_class)->
		constructed (object);

	combo_box = GTK_COMBO_BOX (object);
	cell_layout = GTK_CELL_LAYOUT (object);

	list_store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_combo_box_set_model (combo_box, GTK_TREE_MODEL (list_store));
	gtk_combo_box_set_id_column (combo_box, COLUMN_UID);
	g_object_unref (list_store);

	cell_renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (cell_layout, cell_renderer, TRUE);
	gtk_cell_layout_add_attribute (
		cell_layout, cell_renderer, "text", COLUMN_DISPLAY_NAME);

	e_mail_identity_combo_box_refresh (E_MAIL_IDENTITY_COMBO_BOX (object));
}

static void
e_mail_identity_combo_box_class_init (EMailIdentityComboBoxClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EMailIdentityComboBoxPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mail_identity_combo_box_set_property;
	object_class->get_property = mail_identity_combo_box_get_property;
	object_class->dispose = mail_identity_combo_box_dispose;
	object_class->constructed = mail_identity_combo_box_constructed;

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			NULL,
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_mail_identity_combo_box_init (EMailIdentityComboBox *combo_box)
{
	combo_box->priv = E_MAIL_IDENTITY_COMBO_BOX_GET_PRIVATE (combo_box);
}

GtkWidget *
e_mail_identity_combo_box_new (ESourceRegistry *registry)
{
	g_return_val_if_fail (E_IS_SOURCE_REGISTRY (registry), NULL);

	return g_object_new (
		E_TYPE_MAIL_IDENTITY_COMBO_BOX,
		"registry", registry, NULL);
}

void
e_mail_identity_combo_box_refresh (EMailIdentityComboBox *combo_box)
{
	ESourceRegistry *registry;
	GtkTreeModel *tree_model;
	GtkComboBox *gtk_combo_box;
	ESource *source;
	GList *list, *link;
	GHashTable *address_table;
	const gchar *extension_name;
	const gchar *saved_uid;

	g_return_if_fail (E_IS_MAIL_IDENTITY_COMBO_BOX (combo_box));

	if (combo_box->priv->refresh_idle_id > 0) {
		g_source_remove (combo_box->priv->refresh_idle_id);
		combo_box->priv->refresh_idle_id = 0;
	}

	gtk_combo_box = GTK_COMBO_BOX (combo_box);
	tree_model = gtk_combo_box_get_model (gtk_combo_box);

	/* This is an interned string, which means it's safe
	 * to use even after clearing the combo box model. */
	saved_uid = gtk_combo_box_get_active_id (gtk_combo_box);

	gtk_list_store_clear (GTK_LIST_STORE (tree_model));

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	registry = e_mail_identity_combo_box_get_registry (combo_box);
	list = e_source_registry_list_enabled (registry, extension_name);

	/* Build a hash table of GQueues by email address so we can
	 * spot duplicate email addresses.  Then if the GQueue for a
	 * given email address has multiple elements, we use a more
	 * verbose description in the combo box. */

	address_table = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_queue_free);

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESourceMailIdentity *extension;
		GQueue *queue;
		const gchar *address;

		source = E_SOURCE (link->data);
		extension = e_source_get_extension (source, extension_name);
		address = e_source_mail_identity_get_address (extension);

		if (address == NULL)
			continue;

		queue = g_hash_table_lookup (address_table, address);
		if (queue == NULL) {
			queue = g_queue_new ();
			g_hash_table_insert (
				address_table,
				g_strdup (address), queue);
		}

		g_queue_push_tail (queue, source);
	}

	for (link = list; link != NULL; link = g_list_next (link)) {
		ESourceMailIdentity *extension;
		GtkTreeIter iter;
		GQueue *queue;
		GString *string;
		const gchar *address;
		const gchar *display_name;
		const gchar *name;
		const gchar *uid;

		source = E_SOURCE (link->data);

		extension = e_source_get_extension (source, extension_name);
		name = e_source_mail_identity_get_name (extension);
		address = e_source_mail_identity_get_address (extension);

		if (name == NULL || address == NULL)
			continue;

		queue = g_hash_table_lookup (address_table, address);

		display_name = e_source_get_display_name (source);
		uid = e_source_get_uid (source);

		string = g_string_sized_new (512);
		g_string_append_printf (string, "%s <%s>", name, address);

		/* Show the account name for duplicate email addresses. */
		if (queue != NULL && g_queue_get_length (queue) > 1)
			g_string_append_printf (string, " (%s)", display_name);

		gtk_list_store_append (GTK_LIST_STORE (tree_model), &iter);

		gtk_list_store_set (
			GTK_LIST_STORE (tree_model), &iter,
			COLUMN_DISPLAY_NAME, string->str,
			COLUMN_UID, uid, -1);

		g_string_free (string, TRUE);
	}

	g_hash_table_destroy (address_table);

	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Try and restore the previous selected source, or else pick
	 * the default identity of the default mail account.  If even
	 * that fails, just pick the first item. */

	if (saved_uid != NULL)
		gtk_combo_box_set_active_id (gtk_combo_box, saved_uid);

	if (gtk_combo_box_get_active_id (gtk_combo_box) == NULL)
		mail_identity_combo_box_activate_default (combo_box);

	if (gtk_combo_box_get_active_id (gtk_combo_box) == NULL)
		gtk_combo_box_set_active (gtk_combo_box, 0);
}

ESourceRegistry *
e_mail_identity_combo_box_get_registry (EMailIdentityComboBox *combo_box)
{
	g_return_val_if_fail (E_IS_MAIL_IDENTITY_COMBO_BOX (combo_box), NULL);

	return combo_box->priv->registry;
}
