/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-contact.c
 *
 * Copyright (C) 2003 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Chris Toshok (toshok@ximian.com)
 */

#include <glib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>
#include "e-contact.h"

struct _EContactPrivate {
};

#define E_CONTACT_FIELD_TYPE_STRING    0x0001   /* used for simple single valued attributes */
/*E_CONTACT_FIELD_TYPE_FLOAT*/
#define E_CONTACT_FIELD_TYPE_GLIST     0x0002   /* used for multivalued attributes - the elements are of type EVCardAttribute */
#define E_CONTACT_FIELD_TYPE_STRUCT    0x0004   /* used for structured types (N and ADR properties, in particular) */
#define E_CONTACT_FIELD_TYPE_SYNTHETIC 0x8000   /* used when there isn't a corresponding vcard field (such as email_1) */

typedef struct {
	guint16 t;

	EContactField field_id;
	const char *vcard_field_name;
	const char *field_name;      /* non translated */
	const char *pretty_name;     /* translated */
	
	const char *default_string;

	gboolean read_only;
} EContactFieldInfo;

#define STRING_FIELD(id,vc,n,pn,df,ro) { E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (df), (ro) }
#define GLIST_FIELD(id,vc,n,pn,ro)     { E_CONTACT_FIELD_TYPE_GLIST, (id), (vc), (n), (pn), NULL, (ro) }
#define STRUCT_FIELD(id,vc,n,pn,ro)    { E_CONTACT_FIELD_TYPE_STRUCT, (id), (vc), (n), (pn), NULL, (ro) }
#define SYNTH_STR_FIELD(id,n,pn,df,ro) { E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (n), (pn), (df), (ro) }
#define SYNTH_STRUCT_FIELD(id,n,pn,ro) { E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRUCT, (id), (n), (pn), (ro) }

static EContactFieldInfo field_info[] = {
 	STRING_FIELD (E_CONTACT_UID,        EVC_UID,       "id",         N_("Unique ID"),  NULL, FALSE),
	STRING_FIELD (E_CONTACT_FILE_AS,    EVC_X_FILE_AS, "file_as",    N_("File As"),    NULL, FALSE),

	/* Name fields */
	STRING_FIELD    (E_CONTACT_FULL_NAME,  EVC_FN,       "full_name",   N_("Full Name"),   NULL, FALSE),
	STRUCT_FIELD    (E_CONTACT_NAME,       EVC_N,        "name",        N_("Name"),              FALSE),
	SYNTH_STR_FIELD (E_CONTACT_GIVEN_NAME,               "given_name",  N_("Given Name"),  NULL, FALSE),
	SYNTH_STR_FIELD (E_CONTACT_FAMILY_NAME,              "family_name", N_("Family Name"), NULL, FALSE),
	STRING_FIELD    (E_CONTACT_NICKNAME,   EVC_NICKNAME, "nickname",    N_("Nickname"),    NULL, FALSE),

	/* Address fields */
	GLIST_FIELD        (E_CONTACT_ADDRESS,      EVC_ADR,    "address",       N_("Address List"),  FALSE),
	SYNTH_STRUCT_FIELD (E_CONTACT_ADDRESS_HOME,             "address_home",  N_("Home Address"),  FALSE),
	SYNTH_STRUCT_FIELD (E_CONTACT_ADDRESS_WORK,             "address_work",  N_("Work Address"),  FALSE),
	SYNTH_STRUCT_FIELD (E_CONTACT_ADDRESS_OTHER,            "address_other", N_("Other Address"), FALSE),

	/* Email fields */
	GLIST_FIELD     (E_CONTACT_EMAIL,      EVC_EMAIL, "email",      N_("Email List"),       FALSE),
	SYNTH_STR_FIELD (E_CONTACT_EMAIL_1,               "email_1",    N_("Email 1"),    NULL, FALSE),
	SYNTH_STR_FIELD (E_CONTACT_EMAIL_2,               "email_2",    N_("Email 2"),    NULL, FALSE),
	SYNTH_STR_FIELD (E_CONTACT_EMAIL_3,               "email_3",    N_("Email 3"),    NULL, FALSE),

	/* Instant messaging fields */
	GLIST_FIELD (E_CONTACT_IM_AIM,    EVC_X_AIM,    "im_aim",    N_("AIM Screen Name List"), FALSE),
	GLIST_FIELD (E_CONTACT_IM_JABBER, EVC_X_JABBER, "im_jabber", N_("Jabber Id List"), FALSE),
	GLIST_FIELD (E_CONTACT_IM_YAHOO,  EVC_X_YAHOO,  "im_yahoo",  N_("Yahoo! Screen Name List"), FALSE),
	GLIST_FIELD (E_CONTACT_IM_MSN,    EVC_X_MSN,    "im_msn",    N_("MSN Screen Name List"), FALSE),
	GLIST_FIELD (E_CONTACT_IM_ICQ,    EVC_X_ICQ,    "im_icq",    N_("ICQ Id List"), FALSE),

	/* Organizational fields */
	STRING_FIELD    (E_CONTACT_ORG,       EVC_ORG,       "org",       N_("Organization"),        NULL, FALSE),
	SYNTH_STR_FIELD (E_CONTACT_ORG_UNIT,                 "org_unit",  N_("Organizational Unit"), NULL, FALSE),
	SYNTH_STR_FIELD (E_CONTACT_OFFICE,                   "office",    N_("Office"),              NULL, FALSE),
	STRING_FIELD    (E_CONTACT_TITLE,     EVC_TITLE,     "title",     N_("Title"),               NULL, FALSE),
	STRING_FIELD    (E_CONTACT_ROLE,      EVC_ROLE,      "role",      N_("Role"),                NULL, FALSE),
	STRING_FIELD    (E_CONTACT_MANAGER,   EVC_X_MANAGER, "manager",   N_("Manager"),             NULL, FALSE),
	STRING_FIELD    (E_CONTACT_ASSISTANT, EVC_X_MANAGER, "assistant", N_("Assistant"),           NULL, FALSE),

	/* Web fields */
	STRING_FIELD (E_CONTACT_HOMEPAGE_URL, EVC_URL, "homepage_url", N_("Homepage"), NULL, FALSE),
};

#undef STRING_FIELD
#undef SYNTH_STR_FIELD
#undef GLIST_FIELD
#undef STRUCT_FIELD

static GObjectClass *parent_class;

static void e_contact_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_contact_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void
e_contact_dispose (GObject *object)
{
	EContact *ec = E_CONTACT (object);

	if (!ec->priv)
		return;

	/* XXX free instance specific stuff */

	g_free (ec->priv);
	ec->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_contact_class_init (EContactClass *klass)
{
	GObjectClass *object_class;
	int i;

	object_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_ref (E_TYPE_VCARD);

	object_class->dispose = e_contact_dispose;
	object_class->set_property = e_contact_set_property;
	object_class->get_property = e_contact_get_property;

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		GParamSpec *pspec = NULL;
		if (field_info[i].t & E_CONTACT_FIELD_TYPE_STRING)
			pspec = g_param_spec_string (field_info[i].field_name,
						     _(field_info[i].pretty_name),
						     "" /* XXX blurb */,
						     field_info[i].default_string,
						     field_info[i].read_only ? G_PARAM_READABLE : G_PARAM_READWRITE);
		else if (field_info[i].t & E_CONTACT_FIELD_TYPE_GLIST
			 || field_info[i].t & E_CONTACT_FIELD_TYPE_STRUCT)
			pspec = g_param_spec_pointer (field_info[i].field_name,
						      _(field_info[i].pretty_name),
						      "" /* XXX blurb */,
						      field_info[i].read_only ? G_PARAM_READABLE : G_PARAM_READWRITE);
		else {
			g_warning ("unhandled field type 0x%02x", field_info[i].t);
			continue;
		}

		g_object_class_install_property (object_class, field_info[i].field_id,
						 pspec);
	}
}

static void
e_contact_init (EContact *ec)
{
	ec->priv = g_new0 (EContactPrivate, 1);
}

GType
e_contact_get_type (void)
{
	static GType contact_type = 0;

	if (!contact_type) {
		static const GTypeInfo contact_info =  {
			sizeof (EContactClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) e_contact_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EContact),
			0,             /* n_preallocs */
			(GInstanceInitFunc) e_contact_init,
		};

		contact_type = g_type_register_static (E_TYPE_VCARD, "EContact", &contact_info, 0);
	}

	return contact_type;
}

/* Set_arg handler for the contact */
static void
e_contact_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	EContact *contact = E_CONTACT (object);

	if (prop_id < 1 || prop_id >= E_CONTACT_FIELD_LAST) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}
}

static GList *
e_contact_get_email_list (EContact *contact)
{
	GList *rv = NULL;
	GList *attrs, *l;

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (l = attrs; l; l = l->next) {
		EVCardAttribute *attr = l->data;
		const char *name, *group;

		group = e_vcard_attribute_get_group (attr);
		name = e_vcard_attribute_get_name (attr);

		/* all the attributes we care about should be in group "" */
		if ((!group || !*group) && !strcmp (name, EVC_EMAIL)) {
			GList *v = e_vcard_attribute_get_values (attr);

			rv = g_list_append (rv, v ? g_strdup (v->data) : NULL);
		}
	}

	return rv;
}

static EVCardAttribute*
e_contact_get_first_attr (EContact *contact, const char *attr_name)
{
	GList *attrs, *l;

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (l = attrs; l; l = l->next) {
		EVCardAttribute *attr = l->data;
		const char *name, *group;

		group = e_vcard_attribute_get_group (attr);
		name = e_vcard_attribute_get_name (attr);

		/* all the attributes we care about should be in group "" */
		if ((!group || !*group) && !strcmp (name, attr_name))
			return attr;
	}

	return NULL;
}

static void
e_contact_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	EContact *contact = E_CONTACT (object);
	int i;
	EContactFieldInfo *info = NULL;

	if (prop_id < 1 || prop_id >= E_CONTACT_FIELD_LAST) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (field_info); i++) {
		if (field_info[i].field_id == prop_id) {
			info = &field_info[i];
			break;
		}
	}

	if (!info) {
		g_warning ("unknown field %d", prop_id);
		return;
	}
	
	if (info->t & E_CONTACT_FIELD_TYPE_SYNTHETIC) {
		switch (info->field_id) {
		case E_CONTACT_EMAIL_1:
		case E_CONTACT_EMAIL_2:
		case E_CONTACT_EMAIL_3: {
			GList *emails = e_contact_get_email_list (contact);

			g_value_set_string (value, g_list_nth_data (emails, info->field_id - E_CONTACT_EMAIL_1));
					
			g_list_foreach (emails, (GFunc)g_free, NULL);
			g_list_free (emails);
			break;
		case E_CONTACT_GIVEN_NAME:
		case E_CONTACT_FAMILY_NAME: {
			EContactName *name = e_contact_get (contact, E_CONTACT_NAME);

			g_value_set_string (value, info->field_id == E_CONTACT_GIVEN_NAME ? name->given : name->family);

			e_contact_name_free (name);
			break;
		}
		}
		default:
			g_warning ("unhandled synthetic field 0x%02x", info->field_id);
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRUCT) {
		switch (info->field_id) {
		case E_CONTACT_NAME: {
			EVCardAttribute *attr = e_contact_get_first_attr (contact, EVC_N);
			EContactName *name = g_new0 (EContactName, 1);
			if (attr) {
				GList *p = e_vcard_attribute_get_values (attr);
				name->family     = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
				name->given      = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
				name->additional = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
				name->prefixes   = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
				name->suffixes   = g_strdup (p && p->data ? p->data : "");
			}
			
			g_value_set_pointer (value, name);
			break;
		}
		default:
			g_warning ("unhandled structured field 0x%02x", info->field_id);
		}		
	}
	else {
		GList *attrs, *l;
		GList *rv = NULL; /* used for lists */

		attrs = e_vcard_get_attributes (E_VCARD (contact));

		for (l = attrs; l; l = l->next) {
			EVCardAttribute *attr = l->data;
			const char *name, *group;

			group = e_vcard_attribute_get_group (attr);
			name = e_vcard_attribute_get_name (attr);

			/* all the attributes we care about should be in group "" */
			if ((!group || !*group) && !strcmp (name, info->vcard_field_name)) {

				if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
					GList *v;
					v = e_vcard_attribute_get_values (attr);

					g_value_set_string (value, v ? v->data : NULL);
					break;
				}
				else if (info->t & E_CONTACT_FIELD_TYPE_GLIST) {
					rv = g_list_append (rv, e_vcard_attribute_copy (attr));

					g_value_set_pointer (value, rv);
				}
			}
		}
	}
}



EContact*
e_contact_new (void)
{
	return e_contact_new_from_vcard ("");
}

EContact*
e_contact_new_from_vcard  (const char *vcard)
{
	EContact *contact = g_object_new (E_TYPE_CONTACT, NULL);

	e_vcard_construct (E_VCARD (contact), vcard);

	return contact;
}

EContact*
e_contact_duplicate (EContact *contact)
{
	char *vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	EContact *c = e_contact_new_from_vcard (vcard);

	g_free (vcard);

	return c;
}

const char *
e_contact_field_name (EContactField field_id)
{
	int i;

	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, NULL);

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (field_id == field_info[i].field_id)
			return field_info[i].field_name;
	}

	g_warning ("unknown field id %d", field_id);
	return "";
}

const char *
e_contact_pretty_name (EContactField field_id)
{
	int i;

	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, NULL);

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (field_id == field_info[i].field_id)
			return _(field_info[i].pretty_name);
	}

	g_warning ("unknown field id %d", field_id);
	return "";
}

EContactField
e_contact_field_id (const char *field_name)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (!strcmp (field_info[i].field_name, field_name))
			return field_info[i].field_id;
	}

	g_warning ("unknown field name `%s'", field_name);
	return 0;
}

gpointer
e_contact_get (EContact *contact, EContactField field_id)
{
	gpointer value;

	g_return_val_if_fail (contact && E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, NULL);

	g_object_get (contact,
		      e_contact_field_name (field_id), &value,
		      NULL);

	return value;
}

/* XXX this won't work for structure/list types... */
static void
free_const_data (gpointer data, GObject *where_object_was)
{
	g_free (data);
}

const gpointer
e_contact_get_const (EContact *contact, EContactField field_id)
{
	gpointer value = e_contact_get (contact, field_id);

	g_object_weak_ref (G_OBJECT (contact), free_const_data, value);

	return value;
}

void
e_contact_set (EContact *contact, EContactField field_id, gpointer value)
{
	g_return_if_fail (contact && E_IS_CONTACT (contact));
	g_return_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST);

	g_object_set (contact,
		      e_contact_field_name (field_id), value,
		      NULL);
}

void
e_contact_name_free (EContactName *name)
{
	g_free (name->family);
	g_free (name->given);
	g_free (name->additional);
	g_free (name->prefixes);
	g_free (name->suffixes);

	g_free (name);
}
