/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors:
 *   Chris Toshok <toshok@ximian.com>
 *
 * Copyright (C) 2003 Ximian, Inc.
 */

#ifndef __E_CONTACT_H__
#define __E_CONTACT_H__

#include <time.h>
#include <glib-object.h>
#include <stdio.h>
#include <ebook/e-vcard.h>

#define E_TYPE_CONTACT            (e_contact_get_type ())
#define E_CONTACT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CONTACT, EContact))
#define E_CONTACT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CONTACT, EContactClass))
#define E_IS_CONTACT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CONTACT))
#define E_IS_CONTACT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CONTACT))
#define E_CONTACT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), E_TYPE_CONTACT, EContactClass))

typedef struct _EContact EContact;
typedef struct _EContactClass EContactClass;
typedef struct _EContactPrivate EContactPrivate;

typedef enum {

	E_CONTACT_UID = 1,     	 /* string field */
	E_CONTACT_FILE_AS,     	 /* string field */

	/* Name fields */
	E_CONTACT_FULL_NAME,   	 /* string field */
	E_CONTACT_NAME,        	 /* structured field (EContactName) */
	E_CONTACT_GIVEN_NAME,  	 /* synthetic field */
	E_CONTACT_FAMILY_NAME, 	 /* synthetic field */
	E_CONTACT_NICKNAME,    	 /* string field */

	/* Email fields */
	E_CONTACT_EMAIL,       	 /* Multi-valued */
	E_CONTACT_EMAIL_1,     	 /* synthetic field */
	E_CONTACT_EMAIL_2,     	 /* synthetic field */
	E_CONTACT_EMAIL_3,     	 /* synthetic field */

	/* Address fields */
	E_CONTACT_ADDRESS,       /* Multi-valued structured (EContactAddress) */
	E_CONTACT_ADDRESS_HOME,  /* synthetic structured field (EContactAddress) */
	E_CONTACT_ADDRESS_WORK,  /* synthetic structured field (EContactAddress) */
	E_CONTACT_ADDRESS_OTHER, /* synthetic structured field (EContactAddress) */

	/* Instant Messaging fields */
	E_CONTACT_IM_AIM,     	 /* Multi-valued */
	E_CONTACT_IM_JABBER,  	 /* Multi-valued */
	E_CONTACT_IM_YAHOO,   	 /* Multi-valued */
	E_CONTACT_IM_MSN,     	 /* Multi-valued */
	E_CONTACT_IM_ICQ,     	 /* Multi-valued */

	/* Organizational fields */
	E_CONTACT_ORG,        	 /* string field */
	E_CONTACT_ORG_UNIT,   	 /* string field */
	E_CONTACT_OFFICE,     	 /* string field */
	E_CONTACT_TITLE,      	 /* string field */
	E_CONTACT_ROLE,       	 /* string field */
	E_CONTACT_MANAGER,    	 /* string field */
	E_CONTACT_ASSISTANT,  	 /* string field */

	/* Web fields */
	E_CONTACT_HOMEPAGE_URL,  /* string field */

	E_CONTACT_FIELD_LAST
} EContactField;

typedef struct {
	char *family;
	char *given;
	char *additional;
	char *prefixes;
	char *suffixes;
} EContactName;

struct _EContact {
	EVCard parent;

	EContactPrivate *priv;
};

struct _EContactClass {
	EVCardClass parent_class;

	/* Padding for future expansion */
	void (*_ebook_reserved0) (void);
	void (*_ebook_reserved1) (void);
	void (*_ebook_reserved2) (void);
	void (*_ebook_reserved3) (void);
	void (*_ebook_reserved4) (void);
};

GType                   e_contact_get_type (void);

EContact*               e_contact_new             (void);
EContact*               e_contact_new_from_vcard  (const char *vcard);

gpointer                e_contact_get             (EContact *contact, EContactField field_id);
const gpointer          e_contact_get_const       (EContact *contact, EContactField field_id);
void                    e_contact_set             (EContact *contact, EContactField field_id, gpointer value);

/* destructors for structured values */
void                    e_contact_name_free       (EContactName *name);


const char*             e_contact_field_name      (EContactField field_id);
const char*             e_contact_pretty_name     (EContactField field_id);


#endif /* __E_CONTACT_H__ */
