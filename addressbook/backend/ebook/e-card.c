/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: 
 *   Arturo Espinosa (arturo@nuclecu.unam.mx)
 *   Nat Friedman    (nat@ximian.com)
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 1999 The Free Software Foundation
 */

#include <config.h>

#include "e-card.h"

#include <gal/widgets/e-unicode.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <bonobo/bonobo-i18n.h>
#include <gal/util/e-util.h>

#include <mimedir/mimedir-vcard.h>
#include "e-util/ename/e-name-western.h"
#include "e-util/ename/e-address-western.h"
#include "e-book.h"
#include "e-destination.h"

#if 0
#define is_a_prop_of(obj,prop) (isAPropertyOf ((obj),(prop)))
#define str_val(obj) (the_str = (vObjectValueType (obj))? fakeCString (vObjectUStringZValue (obj)) : calloc (1, 1))
#define has(obj,prop) (vo = isAPropertyOf ((obj), (prop)))
#endif

#define XEV_FILE_AS "X-EVOLUTION-FILE-AS"
#define XEV_WANTS_HTML "X-MOZILLA-HTML"
#define XEV_ARBITRARY "X-EVOLUTION-ARBITRARY"
#define XEV_LIST "X-EVOLUTION-LIST"
#define XEV_LIST_SHOW_ADDRESSES "X-EVOLUTION-LIST-SHOW_ADDRESSES"
#define XEV_RELATED_CONTACTS "X-EVOLUTION-RELATED_CONTACTS"

struct _ECardPrivate {
	gboolean         empty;         /* TRUE if the card was created from
					   the (invalid) vcard "" */

	MIMEDirVCard    *mimedir_vcard;

	EBook           *book;          /* The EBook this card is from.     */

	ECardName       *name;          /* The structured name.             */
	EList           *address;  	/* Delivery addresses (ECardDeliveryAddress *) */
	EList           *address_label; /* Delivery address labels
					 * (ECardAddrLabel *)               */

	EList           *phone;         /* Phone numbers (ECardPhone *)     */
	EList           *email;         /* Email addresses (char *)         */

	ECardDate       bday;	        /* The person's birthday.           */

	char            *note;


	char            *office;        /* The person's office.             */

	char            *manager;
	char            *assistant;

	char            *spouse;        /* The person's spouse.             */
	ECardDate       *anniversary;   /* The person's anniversary.        */

	char            *caluri;        /* Calendar URI                     */
	char            *fburl;         /* Free Busy URL                    */

	gint             timezone;      /* number of minutes from UTC as an int */

	ECardDate       *last_use;
	float            raw_use_score;

	char            *related_contacts;  /* EDestinationV (serialized) of related contacts. */

	EList           *categories;    /* Categories.                      */

	EList           *arbitrary;     /* Arbitrary fields.                */

	

	guint32         wants_html : 1;     /* Wants html mail. */
	guint32         wants_html_set : 1; /* Wants html mail. */
	guint32		list : 1; /* If the card corresponds to a contact list */
	guint32		list_show_addresses : 1; /* Whether to show the addresses
						    in the To: or Bcc: field */
};

/* Object property IDs */
enum {
	PROP_0,
	PROP_FILE_AS,
	PROP_FULL_NAME,
	PROP_NAME,
	PROP_ADDRESS,
	PROP_ADDRESS_LABEL,
	PROP_PHONE,
	PROP_EMAIL,
	PROP_BIRTH_DATE,
	PROP_URL,
	PROP_ORG,
	PROP_ORG_UNIT,
	PROP_OFFICE,
	PROP_TITLE,
	PROP_ROLE,
	PROP_MANAGER,
	PROP_ASSISTANT,
	PROP_NICKNAME,
	PROP_SPOUSE,
	PROP_ANNIVERSARY,
	PROP_MAILER,
	PROP_CALURI,
	PROP_FBURL,
	PROP_NOTE,
	PROP_RELATED_CONTACTS,
	PROP_CATEGORIES,
	PROP_CATEGORY_LIST,
	PROP_WANTS_HTML,
	PROP_WANTS_HTML_SET,
	PROP_EVOLUTION_LIST,
	PROP_EVOLUTION_LIST_SHOW_ADDRESSES,
	PROP_ARBITRARY,
	PROP_ID,
	PROP_LAST_USE,
	PROP_USE_SCORE,
};

static GObjectClass *parent_class;

static void e_card_init (ECard *card);
static void e_card_class_init (ECardClass *klass);

static void e_card_dispose (GObject *object);
static void e_card_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_card_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/**
 * e_card_get_type:
 * @void: 
 * 
 * Registers the &ECard class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the &ECard class.
 **/
GType
e_card_get_type (void)
{
	static GType card_type = 0;

	if (!card_type) {
		static const GTypeInfo card_info =  {
			sizeof (ECardClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) e_card_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (ECard),
			0,             /* n_preallocs */
			(GInstanceInitFunc) e_card_init,
		};

		card_type = g_type_register_static (G_TYPE_OBJECT, "ECard", &card_info, 0);
	}

	return card_type;
}

ECard *
e_card_new_with_default_charset (char *vcard, char *default_charset)
{
	ECard *card = g_object_new (E_TYPE_CARD, NULL);
	ECardPrivate *priv = card->priv;
	MIMEDirDateTime *datetime;
	GSList *email_list, *e;
	GError *error = NULL;

	if (!vcard || !*vcard) {
		priv->mimedir_vcard = mimedir_vcard_new ();
		priv->empty = TRUE;
	}
	else {
		priv->mimedir_vcard = mimedir_vcard_new_from_string (vcard, &error);
		priv->empty = FALSE;
	}

	if (error) {
		g_warning ("mimedir error: %s\n", error->message);
		g_warning ("while parsing vcard: %s\n", vcard);
		g_error_free (error);
		g_object_unref (card);
		return NULL;
	}

	g_object_get (priv->mimedir_vcard,
		      "birthday", &datetime,
		      "email-list", &email_list,
		      NULL);

	priv->email = e_list_new((EListCopyFunc) g_strdup, 
				 (EListFreeFunc) g_free,
				 NULL);

	for (e = email_list; e; e = g_slist_next (e)) {
		MIMEDirVCardEMail *email = e->data;
		char *address;

		g_object_get (email,
			      "address", &address,
			      NULL);

		e_list_append (priv->email, address);
	}

	if (datetime) {
		mimedir_datetime_get_date (datetime, (GDateYear*)&priv->bday.year, (GDateMonth*)&priv->bday.month, (GDateDay*)&priv->bday.day);
		g_object_unref (datetime);
	}

	return card;
}

/**
 * e_card_new:
 * @vcard: a string in vCard format
 *
 * Returns: a new #ECard that wraps the @vcard.
 */
ECard *
e_card_new (char *vcard)
{
	return e_card_new_with_default_charset (vcard, "UTF-8");
}

ECard *
e_card_duplicate(ECard *card)
{
	MIMEDirProfile *profile;
	ECard *new_card;

	new_card = g_object_new (E_TYPE_CARD, NULL);

	profile = mimedir_vcard_write_to_profile (card->priv->mimedir_vcard);
	new_card->priv->mimedir_vcard = mimedir_vcard_new_from_profile (profile, NULL);
	g_object_unref (profile);

	memcpy (&new_card->priv->bday, &card->priv->bday, sizeof (ECardDate));
	new_card->priv->email = e_list_duplicate (card->priv->email);

	if (card->priv->book) {
		new_card->priv->book = card->priv->book;
		g_object_ref (new_card->priv->book);
	}

	return new_card;
}

static void
e_card_get_today (GDate *dt)
{
	time_t now;
	struct tm *now_tm;
	if (dt == NULL)
		return;
	
	time (&now);
	now_tm = localtime (&now);

	g_date_set_dmy (dt, now_tm->tm_mday, now_tm->tm_mon + 1, now_tm->tm_year + 1900);
}

float
e_card_get_use_score(ECard *card)
{
	GDate today, last_use;
	gint days_since_last_use;
	ECardPrivate *priv;

	g_return_val_if_fail (card != NULL && E_IS_CARD (card), 0);

	priv = card->priv;

	if (priv->last_use == NULL)
		return 0.0;

	e_card_get_today (&today);
	g_date_set_dmy (&last_use, priv->last_use->day, priv->last_use->month, priv->last_use->year);

	days_since_last_use = g_date_get_julian (&today) - g_date_get_julian (&last_use);
	
	/* Apply a seven-day "grace period" to the use score decay. */
	days_since_last_use -= 7;
	if (days_since_last_use < 0)
		days_since_last_use = 0;

	return MAX (priv->raw_use_score, 0) * exp (- days_since_last_use / 30.0);
}

void
e_card_touch(ECard *card)
{
	GDate today;
	double use_score;
	ECardPrivate *priv;

	g_return_if_fail (card != NULL && E_IS_CARD (card));

	priv = card->priv;

	e_card_get_today (&today);
	use_score = e_card_get_use_score (card);

	if (priv->last_use == NULL)
		priv->last_use = g_new (ECardDate, 1);

	priv->last_use->day   = g_date_get_day (&today);
	priv->last_use->month = g_date_get_month (&today);
	priv->last_use->year  = g_date_get_year (&today);

	priv->raw_use_score   = use_score + 1.0;
}

/**
 * e_card_get_id:
 * @card: an #ECard
 *
 * Returns: a string representing the id of the card, which is unique
 * within its book.
 */
const char *
e_card_get_id (ECard *card)
{
	ECardPrivate *priv;
	char *id;

	g_return_val_if_fail (card && E_IS_CARD (card), NULL);

	priv = card->priv;

	g_object_get (priv->mimedir_vcard, "uid", &id, NULL);

	return id ? id : "";
}

/**
 * e_card_get_id:
 * @card: an #ECard
 * @id: a id in string format
 *
 * Sets the identifier of a card, which should be unique within its
 * book.
 */
void
e_card_set_id (ECard *card, const char *id)
{
	ECardPrivate *priv;

	g_return_if_fail (card && E_IS_CARD (card));

	priv = card->priv;

	g_object_set (priv->mimedir_vcard, "uid", id, NULL);
}

EBook *
e_card_get_book (ECard *card)
{
	ECardPrivate *priv;

	g_return_val_if_fail (card && E_IS_CARD (card), NULL);

	priv = card->priv;

	return priv->book;
}

void
e_card_set_book (ECard *card, EBook *book)
{
	ECardPrivate *priv;

	g_return_if_fail (card && E_IS_CARD (card));

	priv = card->priv;

	if (priv->book)
		g_object_unref (priv->book);
	priv->book = book;
	if (priv->book)
		g_object_ref (priv->book);
}

gchar *
e_card_date_to_string (ECardDate *dt)
{
	if (dt) 
		return g_strdup_printf ("%04d-%02d-%02d",
					CLAMP(dt->year, 1000, 9999),
					CLAMP(dt->month, 1, 12),
					CLAMP(dt->day, 1, 31));
	else
		return NULL;
}

#if 0
static VObject *
e_card_get_vobject (const ECard *card, gboolean assumeUTF8)
{
	VObject *vobj;
	
	vobj = newVObject (VCCardProp);

	ADD_PROP_VALUE(vobj, VCVersionProp, "2.1");

	if ( card->name && (card->name->prefix || card->name->given || card->name->additional || card->name->family || card->name->suffix) ) {
		VObject *nameprop;
		gboolean is_ascii = TRUE;
		gboolean has_return = FALSE;
		nameprop = addProp(vobj, VCNameProp);
		if ( card->name->prefix )
			ADD_PROP_VALUE_SET_IS_ASCII(nameprop, VCNamePrefixesProp, card->name->prefix);
		if ( card->name->given ) 
			ADD_PROP_VALUE_SET_IS_ASCII(nameprop, VCGivenNameProp, card->name->given);
		if ( card->name->additional )
			ADD_PROP_VALUE_SET_IS_ASCII(nameprop, VCAdditionalNamesProp, card->name->additional);
		if ( card->name->family )
			ADD_PROP_VALUE_SET_IS_ASCII(nameprop, VCFamilyNameProp, card->name->family);
		if ( card->name->suffix )
			ADD_PROP_VALUE_SET_IS_ASCII(nameprop, VCNameSuffixesProp, card->name->suffix);
		if (has_return)
			addProp(nameprop, VCQuotedPrintableProp);
		if (!(is_ascii || assumeUTF8))
			addPropValue (nameprop, "CHARSET", "UTF-8");
	}
	else if (card->name)
		addProp(vobj, VCNameProp);


	if ( card->address ) {
		EIterator *iterator = e_list_get_iterator(card->address);
		for ( ; e_iterator_is_valid(iterator) ;e_iterator_next(iterator) ) {
			VObject *addressprop;
			ECardDeliveryAddress *address = (ECardDeliveryAddress *) e_iterator_get(iterator);
			gboolean is_ascii = TRUE;
			gboolean has_return = FALSE;

			addressprop = addProp(vobj, VCAdrProp);
			
			set_address_flags (addressprop, address->flags);
			if (address->po)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCPostalBoxProp, address->po);
			if (address->ext)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCExtAddressProp, address->ext);
			if (address->street)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCStreetAddressProp, address->street);
			if (address->city)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCCityProp, address->city);
			if (address->region)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCRegionProp, address->region);
			if (address->code)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCPostalCodeProp, address->code);
			if (address->country)
				ADD_PROP_VALUE_SET_IS_ASCII(addressprop, VCCountryNameProp, address->country);

			if (has_return)
				addProp(addressprop, VCQuotedPrintableProp);
			if (!(is_ascii || assumeUTF8))
				addPropValue (addressprop, "CHARSET", "UTF-8");
		}
		g_object_unref(iterator);
	}

	if ( card->address_label ) {
		EIterator *iterator = e_list_get_iterator(card->address_label);
		for ( ; e_iterator_is_valid(iterator) ;e_iterator_next(iterator) ) {
			VObject *labelprop;
			ECardAddrLabel *address_label = (ECardAddrLabel *) e_iterator_get(iterator);
			if (address_label->data)
				labelprop = ADD_PROP_VALUE(vobj, VCDeliveryLabelProp, address_label->data);
			else
				labelprop = addProp(vobj, VCDeliveryLabelProp);
			
			set_address_flags (labelprop, address_label->flags);
		}
		g_object_unref(iterator);
	}

	if ( card->phone ) { 
		EIterator *iterator = e_list_get_iterator(card->phone);
		for ( ; e_iterator_is_valid(iterator) ;e_iterator_next(iterator) ) {
			VObject *phoneprop;
			ECardPhone *phone = (ECardPhone *) e_iterator_get(iterator);
			phoneprop = ADD_PROP_VALUE(vobj, VCTelephoneProp, phone->number);
			
			set_phone_flags (phoneprop, phone->flags);
		}
		g_object_unref(iterator);
	}

	if ( card->email ) { 
		EIterator *iterator = e_list_get_iterator(card->email);
		for ( ; e_iterator_is_valid(iterator) ;e_iterator_next(iterator) ) {
			VObject *emailprop;
			char *e = (char *) e_iterator_get(iterator);
			if (!strncmp (e, "<?xml", 5)) {
				EDestination *dest = e_destination_import (e);
				emailprop = ADD_PROP_VALUE(vobj, VCEmailAddressProp, e_destination_get_address (dest));
				g_object_unref (dest);
			}
			else {
				emailprop = ADD_PROP_VALUE(vobj, VCEmailAddressProp, e);
			}
			addProp (emailprop, VCInternetProp);
		}
		g_object_unref(iterator);
	}

	if ( card->bday ) {
		char *value;
		value = e_card_date_to_string (card->bday);
		ADD_PROP_VALUE(vobj, VCBirthDateProp, value);
		g_free(value);
	}

	if (card->org || card->org_unit) {
		VObject *orgprop;
		gboolean is_ascii = TRUE;
		gboolean has_return = FALSE;
		orgprop = addProp(vobj, VCOrgProp);
		
		if (card->org)
			ADD_PROP_VALUE_SET_IS_ASCII(orgprop, VCOrgNameProp, card->org);
		if (card->org_unit)
			ADD_PROP_VALUE_SET_IS_ASCII(orgprop, VCOrgUnitProp, card->org_unit);

		if (has_return)
			addProp(orgprop, VCQuotedPrintableProp);
		if (!(is_ascii || assumeUTF8))
			addPropValue (orgprop, "CHARSET", "UTF-8");
	}
	
	if (card->office)
		ADD_PROP_VALUE(vobj, "X-EVOLUTION-OFFICE", card->office);

	if (card->manager)
		ADD_PROP_VALUE(vobj, "X-EVOLUTION-MANAGER", card->manager);
	
	if (card->assistant)
		ADD_PROP_VALUE(vobj, "X-EVOLUTION-ASSISTANT", card->assistant);
	
	if (card->spouse)
		ADD_PROP_VALUE(vobj, "X-EVOLUTION-SPOUSE", card->spouse);

	if ( card->anniversary ) {
		char *value;
		value = e_card_date_to_string (card->anniversary);
		ADD_PROP_VALUE(vobj, "X-EVOLUTION-ANNIVERSARY", value);
		g_free(value);
	}

	if (card->caluri)
		addPropValueQP(vobj, "CALURI", card->caluri);

	if (card->fburl)
		ADD_PROP_VALUE(vobj, "FBURL", card->fburl);
	
	if (card->note) {
		VObject *noteprop;

		noteprop = ADD_PROP_VALUE(vobj, VCNoteProp, card->note);
	}

	if (card->last_use) {
		char *value;
		value = e_card_date_to_string (card->last_use);
		ADD_PROP_VALUE (vobj, "X-EVOLUTION-LAST-USE", value);
		g_free (value);
	}

	if (card->raw_use_score > 0) {
		char *value;
		value = g_strdup_printf ("%f", card->raw_use_score);
		ADD_PROP_VALUE (vobj, "X-EVOLUTION-USE-SCORE", value);
		g_free (value);
	}

	if (card->related_contacts && *card->related_contacts) {
		ADD_PROP_VALUE(vobj, XEV_RELATED_CONTACTS, card->related_contacts);
	}

	if (card->categories) {
		EIterator *iterator;
		int length = 0;
		char *string;
		char *stringptr;
		for (iterator = e_list_get_iterator(card->categories); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
			length += strlen(e_iterator_get(iterator)) + 1;
		}
		string = g_new(char, length + 1);
		stringptr = string;
		*stringptr = 0;
		for (e_iterator_reset(iterator); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
			strcpy(stringptr, e_iterator_get(iterator));
			stringptr += strlen(stringptr);
			*stringptr = ',';
			stringptr++;
			*stringptr = 0;
		}
		if (stringptr > string) {
			stringptr --;
			*stringptr = 0;
		}
		ADD_PROP_VALUE (vobj, "CATEGORIES", string);
		g_free(string);
	}

	if (card->wants_html_set) {
		ADD_PROP_VALUE (vobj, XEV_WANTS_HTML, card->wants_html ? "TRUE" : "FALSE");
	}

	if (card->list) {
		ADD_PROP_VALUE (vobj, XEV_LIST, "TRUE");
		ADD_PROP_VALUE (vobj, XEV_LIST_SHOW_ADDRESSES, card->list_show_addresses ? "TRUE" : "FALSE");
	}

	if (card->arbitrary) {
		EIterator *iterator;
		for (iterator = e_list_get_iterator(card->arbitrary); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
			const ECardArbitrary *arbitrary = e_iterator_get(iterator);
			VObject *arb_object;
			if (arbitrary->value) {
				arb_object = ADD_PROP_VALUE (vobj, XEV_ARBITRARY, arbitrary->value);
			} else {
				arb_object = addProp (vobj, XEV_ARBITRARY);
			}
			if (arbitrary->type) {
				ADD_PROP_VALUE (arb_object, "TYPE", arbitrary->type);
			}
			if (arbitrary->key) {
				addProp (arb_object, arbitrary->key);
			}
		}
	}

	addPropValueQP (vobj, VCUniqueStringProp, (card->id ? card->id : ""));

	return vobj;
}
#endif

/**
 * e_card_get_vcard:
 * @card: an #ECard
 *
 * Returns: a string in vCard format, which is wrapped by the @card.
 */
char *
e_card_get_vcard (ECard *card)
{
	ECardPrivate *priv = card->priv;

	/* XXX make sure the MIMEDirVCard is synced */
	return mimedir_vcard_write_to_string (priv->mimedir_vcard);
}

char *
e_card_get_vcard_assume_utf8 (ECard *card)
{
	return e_card_get_vcard (card);
}

/**
 * e_card_list_get_vcard:
 * @list: a list of #ECards
 *
 * Returns: a string in vCard format.
 */
char *
e_card_list_get_vcard (const GList *list)
{
#ifdef MIMEDIR_WORK
	VObject *vobj;

	char *temp, *ret_val;

	vobj = NULL;

	for (; list; list = list->next) {
		VObject *tempvobj;
		ECard *card = list->data;

		tempvobj = e_card_get_vobject (card, FALSE);
		addList (&vobj, tempvobj);
	}
	temp = writeMemVObjects(NULL, NULL, vobj);
	ret_val = g_strdup(temp);
	free(temp);
	cleanVObjects(vobj);
	return ret_val;
#endif
}

#ifdef MIMEDIR_WORK
static void
parse_name(ECard *card, VObject *vobj, char *default_charset)
{
	e_card_name_unref(card->name);

	card->name = e_card_name_new();

	card->name->family     = e_v_object_get_child_value (vobj, VCFamilyNameProp,      default_charset);
	card->name->given      = e_v_object_get_child_value (vobj, VCGivenNameProp,       default_charset);
	card->name->additional = e_v_object_get_child_value (vobj, VCAdditionalNamesProp, default_charset);
	card->name->prefix     = e_v_object_get_child_value (vobj, VCNamePrefixesProp,    default_charset);
	card->name->suffix     = e_v_object_get_child_value (vobj, VCNameSuffixesProp,    default_charset);
}

/* Deal with charset */
static void
parse_bday(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		if ( card->bday )
			g_free(card->bday);
		card->bday = g_new(ECardDate, 1);
		*(card->bday) = e_card_date_from_string(str);
		free(str);
	}
}

static void
parse_phone(ECard *card, VObject *vobj, char *default_charset)
{
	ECardPhone *next_phone = e_card_phone_new ();
	EList *list;

	assign_string(vobj, default_charset, &(next_phone->number));
	next_phone->flags = get_phone_flags(vobj);

	g_object_get(card,
		     "phone", &list,
		     NULL);
	e_list_append(list, next_phone);
	e_card_phone_unref (next_phone);
}

static void
parse_address(ECard *card, VObject *vobj, char *default_charset)
{
	ECardDeliveryAddress *next_addr = e_card_delivery_address_new ();
	EList *list;

	next_addr->flags   = get_address_flags (vobj);
	next_addr->po      = e_v_object_get_child_value (vobj, VCPostalBoxProp,     default_charset);
	next_addr->ext     = e_v_object_get_child_value (vobj, VCExtAddressProp,    default_charset);
	next_addr->street  = e_v_object_get_child_value (vobj, VCStreetAddressProp, default_charset);
	next_addr->city    = e_v_object_get_child_value (vobj, VCCityProp,          default_charset);
	next_addr->region  = e_v_object_get_child_value (vobj, VCRegionProp,        default_charset);
	next_addr->code    = e_v_object_get_child_value (vobj, VCPostalCodeProp,    default_charset);
	next_addr->country = e_v_object_get_child_value (vobj, VCCountryNameProp,   default_charset);

	g_object_get(card,
		     "address", &list,
		     NULL);
	e_list_append(list, next_addr);
	e_card_delivery_address_unref (next_addr);
}

static void
parse_address_label(ECard *card, VObject *vobj, char *default_charset)
{
	ECardAddrLabel *next_addr = e_card_address_label_new ();
	EList *list;

	next_addr->flags   = get_address_flags (vobj);
	assign_string(vobj, default_charset, &next_addr->data);

	g_object_get(card,
		     "address_label", &list,
		     NULL);
	e_list_append(list, next_addr);
	e_card_address_label_unref (next_addr);
}

static void
parse_office(ECard *card, VObject *vobj, char *default_charset)
{
	if ( card->office )
		g_free(card->office);
	assign_string(vobj, default_charset, &(card->office));
}

static void
parse_manager(ECard *card, VObject *vobj, char *default_charset)
{
	if ( card->manager )
		g_free(card->manager);
	assign_string(vobj, default_charset, &(card->manager));
}

static void
parse_assistant(ECard *card, VObject *vobj, char *default_charset)
{
	if ( card->assistant )
		g_free(card->assistant);
	assign_string(vobj, default_charset, &(card->assistant));
}

static void
parse_spouse(ECard *card, VObject *vobj, char *default_charset)
{
	if ( card->spouse )
		g_free(card->spouse);
	assign_string(vobj, default_charset, &(card->spouse));
}

/* Deal with charset */
static void
parse_anniversary(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		if (card->anniversary)
			g_free(card->anniversary);
		card->anniversary = g_new(ECardDate, 1);
		*(card->anniversary) = e_card_date_from_string(str);
		free(str);
	}
}

static void
parse_caluri(ECard *card, VObject *vobj, char *default_charset)
{
 	g_free(card->caluri);
 	assign_string(vobj, default_charset, &(card->caluri));
}

static void
parse_fburl(ECard *card, VObject *vobj, char *default_charset)
{
	g_free(card->fburl);
	assign_string(vobj, default_charset, &(card->fburl));
}

static void
parse_note(ECard *card, VObject *vobj, char *default_charset)
{
	g_free(card->note);
	assign_string(vobj, default_charset, &(card->note));
}

static void
parse_related_contacts(ECard *card, VObject *vobj, char *default_charset)
{
	g_free(card->related_contacts);
	assign_string(vobj, default_charset, &(card->related_contacts));
}

static void
add_list_unique(ECard *card, EList *list, char *string)
{
	char *temp = e_strdup_strip(string);
	EIterator *iterator;

	if (!*temp) {
		g_free(temp);
		return;
	}
	for ( iterator = e_list_get_iterator(list); e_iterator_is_valid(iterator); e_iterator_next(iterator)) {
		if (!strcmp(e_iterator_get(iterator), temp)) {
			break;
		}
	}
	if (!e_iterator_is_valid(iterator)) {
		e_list_append(list, temp);
	}
	g_free(temp);
	g_object_unref(iterator);
}

static void
do_parse_categories(ECard *card, char *str)
{
	int length = strlen(str);
	char *copy = g_new(char, length + 1);
	int i, j;
	EList *list;
	g_object_get(card,
		     "category_list", &list,
		     NULL);
	for (i = 0, j = 0; str[i]; i++, j++) {
		switch (str[i]) {
		case '\\':
			i++;
			if (str[i]) {
				copy[j] = str[i];
			} else
				i--;
			break;
		case ',':
			copy[j] = 0;
			add_list_unique(card, list, copy);
			j = -1;
			break;
		default:
			copy[j] = str[i];
			break;
		}
	}
	copy[j] = 0;
	add_list_unique(card, list, copy);
	g_free(copy);
}

/* Deal with charset */
static void
parse_categories(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		do_parse_categories(card, str);
		free(str);
	}
}

/* Deal with charset */
static void
parse_wants_html(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		if (!strcasecmp(str, "true")) {
			card->wants_html = TRUE;
			card->wants_html_set = TRUE;
		}
		if (!strcasecmp(str, "false")) {
			card->wants_html = FALSE;
			card->wants_html_set = TRUE;
		}
		free(str);
	}
}

/* Deal with charset */
static void
parse_list(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		if (!strcasecmp(str, "true")) {
			card->list = TRUE;
		}
		if (!strcasecmp(str, "false")) {
			card->list = FALSE;
		}
		free(str);
	}
}

/* Deal with charset */
static void
parse_list_show_addresses(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		if (!strcasecmp(str, "true")) {
			card->list_show_addresses = TRUE;
		}
		if (!strcasecmp(str, "false")) {
			card->list_show_addresses = FALSE;
		}
		free(str);
	}
}

typedef union ValueItem {
    const char *strs;
    const wchar_t *ustrs;
    unsigned int i;
    unsigned long l;
    void *any;
    VObject *vobj;
} ValueItem;

struct VObject {
    VObject *next;
    const char *id;
    VObject *prop;
    unsigned short valType;
    ValueItem val;
};

static void
parse_arbitrary(ECard *card, VObject *vobj, char *default_charset)
{
	ECardArbitrary *arbitrary = e_card_arbitrary_new();
	VObjectIterator iterator;
	EList *list;
	for ( initPropIterator (&iterator, vobj); moreIteration(&iterator); ) {
		VObject *temp = nextVObject(&iterator);
		const char *name = vObjectName(temp);
		if (name && !strcmp(name, "TYPE")) {
			g_free(arbitrary->type);
			assign_string(temp, default_charset, &(arbitrary->type));
		} else {
			g_free(arbitrary->key);
			arbitrary->key = g_strdup(name);
		}
	}

	assign_string(vobj, default_charset, &(arbitrary->value));
	
	g_object_get(card,
		     "arbitrary", &list,
		     NULL);
	e_list_append(list, arbitrary);
	e_card_arbitrary_unref(arbitrary);
}

static void
parse_id(ECard *card, VObject *vobj, char *default_charset)
{
	g_free(card->id);
	assign_string(vobj, default_charset, &(card->id));
}

/* Deal with charset */
static void
parse_last_use(ECard *card, VObject *vobj, char *default_charset)
{
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		if ( card->last_use )
			g_free(card->last_use);
		card->last_use = g_new(ECardDate, 1);
		*(card->last_use) = e_card_date_from_string(str);
		free(str);
	}
}

/* Deal with charset */
static void
parse_use_score(ECard *card, VObject *vobj, char *default_charset)
{
	card->raw_use_score = 0;
	
	if ( vObjectValueType (vobj) ) {
		char *str = fakeCString (vObjectUStringZValue (vobj));
		card->raw_use_score = MAX(0, atof (str));
		free (str);
	}
}

static void
parse_attribute(ECard *card, VObject *vobj, char *default_charset)
{
	ParsePropertyFunc function = g_hash_table_lookup(E_CARD_GET_CLASS(card)->attribute_jump_table, vObjectName(vobj));
	if ( function )
		function(card, vobj, default_charset);
}

static void
parse(ECard *card, VObject *vobj, char *default_charset)
{
	VObjectIterator iterator;
	initPropIterator(&iterator, vobj);
	while(moreIteration (&iterator)) {
		parse_attribute(card, nextVObject(&iterator), default_charset);
	}
	if (!card->name) {
		card->name = e_card_name_from_string(card->fname);
	}
	if (!card->file_as) {
		ECardName *name = card->name;
		char *strings[3], **stringptr;
		char *string;
		stringptr = strings;
		if (name->family && *name->family)
			*(stringptr++) = name->family;
		if (name->given && *name->given)
			*(stringptr++) = name->given;
		*stringptr = NULL;
		string = g_strjoinv(", ", strings);
		card->file_as = string;
	}
}
#endif

static void
e_card_class_init (ECardClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_ref (G_TYPE_OBJECT);

	object_class->dispose = e_card_dispose;
	object_class->get_property = e_card_get_property;
	object_class->set_property = e_card_set_property;

	g_object_class_install_property (object_class, PROP_FILE_AS, 
					 g_param_spec_string ("file_as",
							      _("File As"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_FULL_NAME, 
					 g_param_spec_string ("full_name",
							      _("Full Name"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_NAME, 
					 g_param_spec_pointer ("name",
							       _("Name"),
							       /*_( */"XXX blurb" /*)*/,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ADDRESS, 
					 g_param_spec_object ("address",
							      _("Address"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_LIST,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_ADDRESS_LABEL, 
					 g_param_spec_object ("address_label",
							      _("Address Label"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_LIST,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_PHONE, 
					 g_param_spec_object ("phone",
							      _("Phone"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_LIST,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_EMAIL, 
					 g_param_spec_object ("email",
							      _("Email"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_LIST,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_BIRTH_DATE, 
					 g_param_spec_pointer ("birth_date",
							       _("Birth date"),
							       /*_( */"XXX blurb" /*)*/,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_URL, 
					 g_param_spec_string ("url",
							      _("URL"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ORG, 
					 g_param_spec_string ("org",
							      _("Organization"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ORG_UNIT, 
					 g_param_spec_string ("org_unit",
							      _("Organizational Unit"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_OFFICE, 
					 g_param_spec_string ("office",
							      _("Office"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_TITLE, 
					 g_param_spec_string ("title",
							      _("Title"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ROLE, 
					 g_param_spec_string ("role",
							      _("Role"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_MANAGER, 
					 g_param_spec_string ("manager",
							      _("Manager"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ASSISTANT, 
					 g_param_spec_string ("assistant",
							      _("Assistant"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_NICKNAME, 
					 g_param_spec_string ("nickname",
							      _("Nickname"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_SPOUSE, 
					 g_param_spec_string ("spouse",
							      _("Spouse"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ANNIVERSARY, 
					 g_param_spec_pointer ("anniversary",
							       _("Anniversary"),
							       /*_( */"XXX blurb" /*)*/,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_MAILER, 
					 g_param_spec_string ("mailer",
							      _("Mailer"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_CALURI, 
					 g_param_spec_string ("caluri",
							      _("Calendar URI"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_FBURL, 
					 g_param_spec_string ("fburl",
							      _("Free/Busy URL"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_NOTE, 
					 g_param_spec_string ("note",
							      _("Note"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_RELATED_CONTACTS, 
					 g_param_spec_string ("related_contacts",
							      _("Related Contacts"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_CATEGORIES, 
					 g_param_spec_string ("categories",
							      _("Categories"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_CATEGORY_LIST, 
					 g_param_spec_object ("category list",
							      _("Category List"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_LIST,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_WANTS_HTML, 
					 g_param_spec_boolean ("wants_html",
							       _("Wants HTML"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_WANTS_HTML_SET, 
					 g_param_spec_boolean ("wants_html_set",
							       _("Wants HTML set"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READABLE));

	g_object_class_install_property (object_class, PROP_EVOLUTION_LIST, 
					 g_param_spec_boolean ("list",
							       _("List"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_EVOLUTION_LIST_SHOW_ADDRESSES, 
					 g_param_spec_boolean ("list_show_addresses",
							       _("List Show Addresses"),
							       /*_( */"XXX blurb" /*)*/,
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ARBITRARY, 
					 g_param_spec_object ("arbitrary",
							      _("Arbitrary"),
							      /*_( */"XXX blurb" /*)*/,
							      E_TYPE_LIST,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_ID, 
					 g_param_spec_string ("id",
							      _("ID"),
							      /*_( */"XXX blurb" /*)*/,
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_LAST_USE, 
					 g_param_spec_pointer ("last_use",
							       _("Last Use"),
							       /*_( */"XXX blurb" /*)*/,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_USE_SCORE, 
					 /* XXX at some point we
					    should remove
					    LAX_VALIDATION and figure
					    out some hard min & max
					    scores. */
					 g_param_spec_float ("use_score",
							     _("Use Score"),
							     /*_( */"XXX blurb" /*)*/,
							     0.0,
							     0.0,
							     0.0,
							     G_PARAM_READWRITE | G_PARAM_LAX_VALIDATION));
}

ECardPhone *
e_card_phone_new (void)
{
	ECardPhone *newphone = g_new(ECardPhone, 1);

	newphone->ref_count = 1;
	newphone->number = NULL;
	newphone->flags = 0;
	
	return newphone;
}

void
e_card_phone_unref (ECardPhone *phone)
{
	if (phone) {
		phone->ref_count --;
		if (phone->ref_count == 0) {
			g_free(phone->number);
			g_free(phone);
		}
	}
}

ECardPhone *
e_card_phone_ref (const ECardPhone *phone)
{
	ECardPhone *phone_mutable = (ECardPhone *) phone;
	if (phone_mutable)
		phone_mutable->ref_count ++;
	return phone_mutable;
}

ECardPhone *
e_card_phone_copy (const ECardPhone *phone)
{
	if ( phone ) {
		ECardPhone *phone_copy = e_card_phone_new();
		phone_copy->number = g_strdup(phone->number);
		phone_copy->flags  = phone->flags;
		return phone_copy;
	} else
		return NULL;
}

ECardDeliveryAddress *
e_card_delivery_address_new (void)
{
	ECardDeliveryAddress *newaddr = g_new(ECardDeliveryAddress, 1);

	newaddr->ref_count = 1;
	newaddr->po      = NULL;
	newaddr->ext     = NULL;
	newaddr->street  = NULL;
	newaddr->city    = NULL;
	newaddr->region  = NULL;
	newaddr->code    = NULL;
	newaddr->country = NULL;
	newaddr->flags   = 0;

	return newaddr;
}

void
e_card_delivery_address_unref (ECardDeliveryAddress *addr)
{
	if ( addr ) {
		addr->ref_count --;
		if (addr->ref_count == 0) {
			g_free(addr->po);
			g_free(addr->ext);
			g_free(addr->street);
			g_free(addr->city);
			g_free(addr->region);
			g_free(addr->code);
			g_free(addr->country);
			g_free(addr);
		}
	}
}

ECardDeliveryAddress *
e_card_delivery_address_ref (const ECardDeliveryAddress *addr)
{
	ECardDeliveryAddress *addr_mutable = (ECardDeliveryAddress *) addr;
	if (addr_mutable)
		addr_mutable->ref_count ++;
	return addr_mutable;
}

ECardDeliveryAddress *
e_card_delivery_address_copy (const ECardDeliveryAddress *addr)
{
	if ( addr ) {
		ECardDeliveryAddress *addr_copy = e_card_delivery_address_new ();
		addr_copy->po      = g_strdup(addr->po     );
		addr_copy->ext     = g_strdup(addr->ext    );
		addr_copy->street  = g_strdup(addr->street );
		addr_copy->city    = g_strdup(addr->city   );
		addr_copy->region  = g_strdup(addr->region );
		addr_copy->code    = g_strdup(addr->code   );
		addr_copy->country = g_strdup(addr->country);
		addr_copy->flags   = addr->flags;
		return addr_copy;
	} else
		return NULL;
}

gboolean
e_card_delivery_address_is_empty (const ECardDeliveryAddress *addr)
{
	return (((addr->po      == NULL) || (*addr->po      == 0)) &&
		((addr->ext     == NULL) || (*addr->ext     == 0)) &&
		((addr->street  == NULL) || (*addr->street  == 0)) &&
		((addr->city    == NULL) || (*addr->city    == 0)) &&
		((addr->region  == NULL) || (*addr->region  == 0)) &&
		((addr->code    == NULL) || (*addr->code    == 0)) &&
		((addr->country == NULL) || (*addr->country == 0)));
}

ECardDeliveryAddress *
e_card_delivery_address_from_label(const ECardAddrLabel *label)
{
	ECardDeliveryAddress *addr = e_card_delivery_address_new ();
	EAddressWestern *western = e_address_western_parse (label->data);
	
	addr->po      = g_strdup (western->po_box     );
	addr->ext     = g_strdup (western->extended   );
	addr->street  = g_strdup (western->street     );
	addr->city    = g_strdup (western->locality   );
	addr->region  = g_strdup (western->region     );
	addr->code    = g_strdup (western->postal_code);
	addr->country = g_strdup (western->country    );
	addr->flags   = label->flags;
	
	e_address_western_free(western);
	
	return addr;
}

char *
e_card_delivery_address_to_string(const ECardDeliveryAddress *addr)
{
	char *strings[5], **stringptr = strings;
	char *line1, *line22, *line2;
	char *final;
	if (addr->po && *addr->po)
		*(stringptr++) = addr->po;
	if (addr->street && *addr->street)
		*(stringptr++) = addr->street;
	*stringptr = NULL;
	line1 = g_strjoinv(" ", strings);
	stringptr = strings;
	if (addr->region && *addr->region)
		*(stringptr++) = addr->region;
	if (addr->code && *addr->code)
		*(stringptr++) = addr->code;
	*stringptr = NULL;
	line22 = g_strjoinv(" ", strings);
	stringptr = strings;
	if (addr->city && *addr->city)
		*(stringptr++) = addr->city;
	if (line22 && *line22)
		*(stringptr++) = line22;
	*stringptr = NULL;
	line2 = g_strjoinv(", ", strings);
	stringptr = strings;
	if (line1 && *line1)
		*(stringptr++) = line1;
	if (addr->ext && *addr->ext)
		*(stringptr++) = addr->ext;
	if (line2 && *line2)
		*(stringptr++) = line2;
	if (addr->country && *addr->country)
		*(stringptr++) = addr->country;
	*stringptr = NULL;
	final = g_strjoinv("\n", strings);
	g_free(line1);
	g_free(line22);
	g_free(line2);
	return final;
}

ECardAddrLabel *
e_card_delivery_address_to_label    (const ECardDeliveryAddress *addr)
{
	ECardAddrLabel *label;
	label = e_card_address_label_new();
	label->flags = addr->flags;
	label->data = e_card_delivery_address_to_string(addr);

	return label;
}

ECardAddrLabel *
e_card_address_label_new (void)
{
	ECardAddrLabel *newaddr = g_new(ECardAddrLabel, 1);

	newaddr->ref_count = 1;
	newaddr->data = NULL;
	newaddr->flags = 0;
	
	return newaddr;
}

void
e_card_address_label_unref (ECardAddrLabel *addr)
{
	if (addr) {
		addr->ref_count --;
		if (addr->ref_count == 0) {
			g_free(addr->data);
			g_free(addr);
		}
	}
}

ECardAddrLabel *
e_card_address_label_ref (const ECardAddrLabel *addr)
{
	ECardAddrLabel *addr_mutable = (ECardAddrLabel *) addr;
	if (addr_mutable)
		addr_mutable->ref_count ++;
	return addr_mutable;
}

ECardAddrLabel *
e_card_address_label_copy (const ECardAddrLabel *addr)
{
	if ( addr ) {
		ECardAddrLabel *addr_copy = e_card_address_label_new ();
		addr_copy->data  = g_strdup(addr->data);
		addr_copy->flags = addr->flags;
		return addr_copy;
	} else
		return NULL;
}

ECardName *e_card_name_new(void)
{
	ECardName *newname  = g_new(ECardName, 1);

	newname->ref_count  = 1;
	newname->prefix     = NULL;
	newname->given      = NULL;
	newname->additional = NULL;
	newname->family     = NULL;
	newname->suffix     = NULL;

	return newname;
}

void
e_card_name_unref(ECardName *name)
{
	if (name) {
		name->ref_count --;
		if (name->ref_count == 0) {
			g_free (name->prefix);
			g_free (name->given);
			g_free (name->additional);
			g_free (name->family);
			g_free (name->suffix);
			g_free (name);
		}
	}
}

ECardName *
e_card_name_ref(const ECardName *name)
{
	ECardName *name_mutable = (ECardName *) name;
	if (name_mutable)
		name_mutable->ref_count ++;
	return name_mutable;
}

ECardName *
e_card_name_copy(const ECardName *name)
{
	if (name) {
		ECardName *newname = e_card_name_new ();
               
		newname->prefix = g_strdup(name->prefix);
		newname->given = g_strdup(name->given);
		newname->additional = g_strdup(name->additional);
		newname->family = g_strdup(name->family);
		newname->suffix = g_strdup(name->suffix);

		return newname;
	} else
		return NULL;
}


char *
e_card_name_to_string(const ECardName *name)
{
	char *strings[6], **stringptr = strings;

	g_return_val_if_fail (name != NULL, NULL);

	if (name->prefix && *name->prefix)
		*(stringptr++) = name->prefix;
	if (name->given && *name->given)
		*(stringptr++) = name->given;
	if (name->additional && *name->additional)
		*(stringptr++) = name->additional;
	if (name->family && *name->family)
		*(stringptr++) = name->family;
	if (name->suffix && *name->suffix)
		*(stringptr++) = name->suffix;
	*stringptr = NULL;
	return g_strjoinv(" ", strings);
}

ECardName *
e_card_name_from_string(const char *full_name)
{
	ECardName *name = e_card_name_new ();
	ENameWestern *western = e_name_western_parse (full_name);
	
	name->prefix     = g_strdup (western->prefix);
	name->given      = g_strdup (western->first );
	name->additional = g_strdup (western->middle);
	name->family     = g_strdup (western->last  );
	name->suffix     = g_strdup (western->suffix);
	
	e_name_western_free(western);
	
	return name;
}

ECardArbitrary *
e_card_arbitrary_new(void)
{
	ECardArbitrary *arbitrary = g_new(ECardArbitrary, 1);
	arbitrary->ref_count = 1;
	arbitrary->key = NULL;
	arbitrary->type = NULL;
	arbitrary->value = NULL;
	return arbitrary;
}

void
e_card_arbitrary_unref(ECardArbitrary *arbitrary)
{
	if (arbitrary) {
		arbitrary->ref_count --;
		if (arbitrary->ref_count == 0) {
			g_free(arbitrary->key);
			g_free(arbitrary->type);
			g_free(arbitrary->value);
			g_free(arbitrary);
		}
	}
}

ECardArbitrary *
e_card_arbitrary_copy(const ECardArbitrary *arbitrary)
{
	if (arbitrary) {
		ECardArbitrary *arb_copy = e_card_arbitrary_new ();
		arb_copy->key = g_strdup(arbitrary->key);
		arb_copy->type = g_strdup(arbitrary->type);
		arb_copy->value = g_strdup(arbitrary->value);
		return arb_copy;
	} else
		return NULL;
}

ECardArbitrary *
e_card_arbitrary_ref(const ECardArbitrary *arbitrary)
{
	ECardArbitrary *arbitrary_mutable = (ECardArbitrary *) arbitrary;
	if (arbitrary_mutable)
		arbitrary_mutable->ref_count ++;
	return arbitrary_mutable;
}

/* EMail matching */
static gboolean
e_card_email_match_single_string (const gchar *a, const gchar *b)
{
	const gchar *xa = NULL, *xb = NULL;
	gboolean match = TRUE;

	for (xa=a; *xa && *xa != '@'; ++xa);
	for (xb=b; *xb && *xb != '@'; ++xb);

	if (xa-a != xb-b || *xa != *xb || g_ascii_strncasecmp (a, b, xa-a))
		return FALSE;

	if (*xa == '\0')
		return TRUE;
	
	/* Find the end of the string, then walk through backwards comparing.
	   This is so that we'll match joe@foobar.com and joe@mail.foobar.com.
	*/
	while (*xa)
		++xa;
	while (*xb)
		++xb;

	while (match && *xa != '@' && *xb != '@') {
		match = (tolower (*xa) == tolower (*xb));
		--xa;
		--xb;
	}

	match = match && ((tolower (*xa) == tolower (*xb)) || (*xa == '.') || (*xb == '.'));

	return match;
}

gboolean
e_card_email_match_string (const ECard *card, const gchar *str)
{
	ECardPrivate *priv;
	EIterator *iter;
	
	g_return_val_if_fail (card && E_IS_CARD (card), FALSE);
	g_return_val_if_fail (str != NULL, FALSE);

	priv = card->priv;

	if (!priv->email)
		return FALSE;

	iter = e_list_get_iterator (priv->email);
	for (e_iterator_reset (iter); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		if (e_card_email_match_single_string (e_iterator_get (iter), str))
			return TRUE;
	}
	g_object_unref (iter);

	return FALSE;
}

gint
e_card_email_find_number (const ECard *card, const gchar *email)
{
	ECardPrivate *priv;
	EIterator *iter;
	gint count = 0;

	g_return_val_if_fail (E_IS_CARD (card), -1);
	g_return_val_if_fail (email != NULL, -1);

	priv = card->priv;

	if (!priv->email)
		return -1;

	iter = e_list_get_iterator (priv->email);
	for (e_iterator_reset (iter); e_iterator_is_valid (iter); e_iterator_next (iter)) {
		if (!g_ascii_strcasecmp (e_iterator_get (iter), email))
			goto finished;
		++count;
	}
	count = -1;

 finished:
	g_object_unref (iter);

	return count;
}

/*
 * ECard lifecycle management and vCard loading/saving.
 */

static void
e_card_dispose (GObject *object)
{
	ECard *card = E_CARD(object);
	ECardPrivate *priv = card->priv;

	if (priv) {
		if (priv->book)
			g_object_unref (priv->book);
		if (priv->name)
			e_card_name_unref(priv->name);

		g_free(priv->office);
		g_free(priv->manager);
		g_free(priv->assistant);
		g_free(priv->spouse);
		g_free(priv->anniversary);
		g_free(priv->caluri);
		g_free(priv->fburl);
		g_free(priv->note);
		g_free(priv->related_contacts);

		if (priv->categories)
			g_object_unref (priv->categories);
		if (priv->email)
			g_object_unref (priv->email);
		if (priv->phone)
			g_object_unref (priv->phone);
		if (priv->address)
			g_object_unref (priv->address);
		if (priv->address_label)
			g_object_unref (priv->address_label);

		g_free (card->priv);
		card->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}


/* Set_arg handler for the card */
static void
e_card_set_property (GObject *object,
		     guint prop_id,
		     const GValue *value,
		     GParamSpec *pspec)
{
	ECard *card;
	ECardPrivate *priv;

	card = E_CARD (object);

	priv = card->priv;

	switch (prop_id) {
	case PROP_FILE_AS: 
		{
			const char *file_as = g_value_get_string (value);

			mimedir_vcard_set_custom_attribute (priv->mimedir_vcard, XEV_FILE_AS, (char*)(file_as ? file_as : ""));
		}
		break;

	case PROP_FULL_NAME:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "name", value);
		e_card_name_unref (priv->name);
		priv->name = e_card_name_from_string (g_value_get_string (value));
		break;
	case PROP_NAME:
#ifdef MIMEDIR_WORK
		e_card_name_unref (priv->name);
		priv->name = e_card_name_ref(g_value_get_pointer (value));
		if (priv->name == NULL)
			priv->name = e_card_name_new();
		if (priv->fname == NULL) {
			priv->fname = e_card_name_to_string(priv->name);
		}
		if (priv->file_as == NULL) {
			ECardName *name = priv->name;
			char *strings[3], **stringptr;
			char *string;
			stringptr = strings;
			if (name->family && *name->family)
				*(stringptr++) = name->family;
			if (name->given && *name->given)
				*(stringptr++) = name->given;
			*stringptr = NULL;
			string = g_strjoinv(", ", strings);
			priv->file_as = string;
		}
#endif
		break;
	case PROP_CATEGORIES:
		if (priv->categories)
			g_object_unref(priv->categories);
		priv->categories = NULL;
#if MIMEDIR_WORK
		if (g_value_get_string (value))
			do_parse_categories(card, (char*)g_value_get_string (value));
#endif
		break;
	case PROP_CATEGORY_LIST:
		if (priv->categories)
			g_object_unref(priv->categories);
		priv->categories = E_LIST(g_value_get_object(value));
		if (priv->categories)
			g_object_ref(priv->categories);
		break;
	case PROP_BIRTH_DATE:
		{
			MIMEDirDateTime *datetime;
			if (g_value_get_pointer (value)) {
				memcpy (&priv->bday, g_value_get_pointer (value), sizeof (ECardDate));
			}
			else {
				memset (&priv->bday, 0, sizeof (ECardDate));
			}

			datetime = mimedir_datetime_new_from_date (priv->bday.year, priv->bday.month, priv->bday.day);

			g_object_set (priv->mimedir_vcard,
				      "birthday", datetime,
				      NULL);
			g_object_unref (datetime);
		}
		break;
	case PROP_URL:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "url", value);
		break;
	case PROP_ORG:
		{
			GSList *current_org_list;

			/*
			 * as far as we're concerned:
			 * first element in org list = org.
			 * second element in org list = org unit.
			 * third element ... = ignored.
			 */
			
			g_object_get (priv->mimedir_vcard,
				      "organization-list", &current_org_list,
				      NULL);

			current_org_list = mimedir_utils_copy_string_slist (current_org_list);

			if (current_org_list) {
				/* ick, in place modification */
				g_free (current_org_list->next->data);
				current_org_list->data = g_strdup (g_value_get_string (value));
			}
			else {
				current_org_list = g_slist_append (NULL, g_strdup (g_value_get_string (value)));
			}

			g_object_set (priv->mimedir_vcard,
				      "organization-list", current_org_list,
				      NULL);

			mimedir_utils_free_string_slist (current_org_list);
		}
		break;
	case PROP_ORG_UNIT:
		{
			GSList *current_org_list;

			/*
			 * as far as we're concerned:
			 * first element in org list = org.
			 * second element in org list = org unit.
			 * third element ... = ignored.
			 */
			
			g_object_get (priv->mimedir_vcard,
				      "organization-list", &current_org_list,
				      NULL);

			current_org_list = mimedir_utils_copy_string_slist (current_org_list);

			if (!current_org_list) {
				current_org_list = g_slist_append (NULL, g_strdup (""));
			}

			if (current_org_list->next) {
				/* ick, in place modification */
				g_free (current_org_list->next->data);
				current_org_list->next->data = g_strdup (g_value_get_string (value));
			}
			else {
				current_org_list = g_slist_append (current_org_list, g_strdup (g_value_get_string (value)));
			}

			g_object_set (priv->mimedir_vcard,
				      "organization-list", current_org_list,
				      NULL);

			mimedir_utils_free_string_slist (current_org_list);
		}
		break;
	case PROP_OFFICE:
		g_free(priv->office);
		priv->office = g_strdup(g_value_get_string(value));
		break;
	case PROP_TITLE:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "jobtitle", value);
		break;
	case PROP_ROLE:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "role", value);
		break;
	case PROP_MANAGER:
		g_free(priv->manager);
		priv->manager = g_strdup(g_value_get_string(value));
		break;
	case PROP_ASSISTANT:
		g_free(priv->assistant);
		priv->assistant = g_strdup(g_value_get_string(value));
		break;
	case PROP_NICKNAME:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "nickname", value);
		break;
	case PROP_SPOUSE:
		g_free(priv->spouse);
		priv->spouse = g_strdup(g_value_get_string(value));
		break;
	case PROP_ANNIVERSARY:
		g_free(priv->anniversary);
		if (g_value_get_pointer (value)) {
			priv->anniversary = g_new (ECardDate, 1);
			memcpy (priv->anniversary, g_value_get_pointer (value), sizeof (ECardDate));
		} else {
			priv->anniversary = NULL;
		}
		break;
	case PROP_MAILER:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "mailer", value);
		break;
	case PROP_CALURI:
		g_free(priv->caluri);
		priv->caluri = g_strdup(g_value_get_string(value));
		break;
	case PROP_FBURL:
		g_free(priv->fburl);
		priv->fburl = g_strdup(g_value_get_string(value));
		break;
	case PROP_NOTE:
		g_free (priv->note);
		priv->note = g_strdup(g_value_get_string(value));
		break;
	case PROP_RELATED_CONTACTS:
		g_free (priv->related_contacts);
		priv->related_contacts = g_strdup(g_value_get_string(value));
		break;
	case PROP_WANTS_HTML:
		priv->wants_html = g_value_get_boolean (value);
		priv->wants_html_set = TRUE;
		break;
	case PROP_ARBITRARY:
		if (priv->arbitrary)
			g_object_unref(priv->arbitrary);
		priv->arbitrary = E_LIST(g_value_get_pointer(value));
		if (priv->arbitrary)
			g_object_ref(priv->arbitrary);
		break;
	case PROP_ID:
		g_object_set_property (G_OBJECT (priv->mimedir_vcard), "uid", value);
		break;
	case PROP_LAST_USE:
		g_free(priv->last_use);
		if (g_value_get_pointer (value)) {
			priv->last_use = g_new (ECardDate, 1);
			memcpy (priv->last_use, g_value_get_pointer (value), sizeof (ECardDate));
		} else {
			priv->last_use = NULL;
		}
		break;
	case PROP_USE_SCORE:
		priv->raw_use_score = g_value_get_float (value);
		break;
	case PROP_EVOLUTION_LIST:
		priv->list = g_value_get_boolean (value);
		break;
	case PROP_EVOLUTION_LIST_SHOW_ADDRESSES:
		priv->list_show_addresses = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* Get_arg handler for the card */
static void
e_card_get_property (GObject *object,
		     guint prop_id,
		     GValue *value,
		     GParamSpec *pspec)
{
	ECardPrivate *priv;
	ECard *card;

	card = E_CARD (object);

	priv = card->priv;

	switch (prop_id) {
	case PROP_FILE_AS:
		g_value_set_string (value, mimedir_vcard_get_custom_attribute (priv->mimedir_vcard, XEV_FILE_AS));
		break;
	case PROP_FULL_NAME:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "name", value);
		break;
	case PROP_NAME:
		g_value_set_pointer (value, priv->name);
		break;
	case PROP_ADDRESS:
		if (!priv->address)
			priv->address = e_list_new((EListCopyFunc) e_card_delivery_address_ref,
						   (EListFreeFunc) e_card_delivery_address_unref,
						   NULL);
		g_value_set_object (value, priv->address);
		break;
	case PROP_ADDRESS_LABEL:
		if (!priv->address_label)
			priv->address_label = e_list_new((EListCopyFunc) e_card_address_label_ref,
							 (EListFreeFunc) e_card_address_label_unref,
							 NULL);
		g_value_set_object (value, priv->address_label);
		break;
	case PROP_PHONE:
		if (!priv->phone)
			priv->phone = e_list_new((EListCopyFunc) e_card_phone_ref,
						 (EListFreeFunc) e_card_phone_unref,
						 NULL);
		g_value_set_object (value, priv->phone);
		break;
	case PROP_EMAIL:
		g_value_set_object (value, priv->email);
		break;
	case PROP_CATEGORIES:
		{
			int i;
			char ** strs;
			int length;
			EIterator *iterator;
			if (!priv->categories)
				priv->categories = e_list_new((EListCopyFunc) g_strdup, 
							      (EListFreeFunc) g_free,
							      NULL);
			length = e_list_length(priv->categories);
			strs = g_new(char *, length + 1);
			for (iterator = e_list_get_iterator(priv->categories), i = 0; e_iterator_is_valid(iterator); e_iterator_next(iterator), i++) {
				strs[i] = (char *)e_iterator_get(iterator);
			}
			strs[i] = 0;
			g_value_set_string(value, g_strjoinv(", ", strs));
			g_free(strs);
		}
		break;
	case PROP_CATEGORY_LIST:
		if (!priv->categories)
			priv->categories = e_list_new((EListCopyFunc) g_strdup, 
						      (EListFreeFunc) g_free,
						      NULL);
		g_value_set_object (value, priv->categories);
		break;
	case PROP_BIRTH_DATE:
		g_value_set_pointer (value, &priv->bday);
		break;
	case PROP_URL:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "url", value);
		break;
	case PROP_ORG:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "organization", value);
		break;
	case PROP_ORG_UNIT:
		{
			GSList *org_list;
			char *s;

			g_object_get (priv->mimedir_vcard,
				      "organization-list", &org_list,
				      NULL);

			if (org_list && org_list->next)
				s = (char*)org_list->next->data;
			else
				s = NULL;

			g_value_set_string (value, s);
		}
		break;
	case PROP_OFFICE:
		g_value_set_string (value, priv->office);
		break;
	case PROP_TITLE:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "jobtitle", value);
		break;
	case PROP_ROLE:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "role", value);
		break;
	case PROP_MANAGER:
		g_value_set_string (value, priv->manager);
		break;
	case PROP_ASSISTANT:
		g_value_set_string (value, priv->assistant);
		break;
	case PROP_NICKNAME:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "nickname", value);
		break;
	case PROP_SPOUSE:
		g_value_set_string (value, priv->spouse);
		break;
	case PROP_ANNIVERSARY:
		g_value_set_pointer (value, priv->anniversary);
		break;
	case PROP_MAILER:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "mailer", value);
		break;
	case PROP_CALURI:
		g_value_set_string (value, priv->caluri);
		break;
	case PROP_FBURL:
		g_value_set_string (value, priv->fburl);
		break;
	case PROP_NOTE:
		g_value_set_string (value, priv->note);
		break;
	case PROP_RELATED_CONTACTS:
		g_value_set_string (value, priv->related_contacts);
		break;
	case PROP_WANTS_HTML:
		g_value_set_boolean (value, priv->wants_html);
		break;
	case PROP_WANTS_HTML_SET:
		g_value_set_boolean (value, priv->wants_html_set);
		break;
	case PROP_ARBITRARY:
		if (!priv->arbitrary)
			priv->arbitrary = e_list_new((EListCopyFunc) e_card_arbitrary_ref,
						     (EListFreeFunc) e_card_arbitrary_unref,
						     NULL);

		g_value_set_object (value, priv->arbitrary);
		break;
	case PROP_ID:
		g_object_get_property (G_OBJECT (priv->mimedir_vcard), "uid", value);
		break;
	case PROP_LAST_USE:
		g_value_set_pointer (value, priv->last_use);
		break;
	case PROP_USE_SCORE:
		g_value_set_float (value, e_card_get_use_score (card));
		break;
	case PROP_EVOLUTION_LIST:
		g_value_set_boolean (value, priv->list);
		break;
	case PROP_EVOLUTION_LIST_SHOW_ADDRESSES:
		g_value_set_boolean (value, priv->list_show_addresses);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


/**
 * e_card_init:
 */
static void
e_card_init (ECard *card)
{
	ECardPrivate *priv;

	card->priv = priv = g_new0 (ECardPrivate, 1);
	
	priv->name                = NULL;
	priv->phone               = NULL;
	priv->address             = NULL;
	priv->address_label       = NULL;
	priv->office              = NULL;
	priv->manager             = NULL;
	priv->assistant           = NULL;
	priv->spouse              = NULL;
	priv->anniversary         = NULL;
	priv->caluri              = NULL;
	priv->fburl               = NULL;
	priv->note                = NULL;
	priv->related_contacts    = NULL;
	priv->categories          = NULL;
	priv->wants_html          = FALSE;
	priv->wants_html_set      = FALSE;
	priv->list                = FALSE;
	priv->list_show_addresses = FALSE;
	priv->arbitrary           = NULL;
	priv->last_use            = NULL;
	priv->raw_use_score       = 0;

	memset (&priv->bday, 0, sizeof (ECardDate));
}

GList *
e_card_load_cards_from_file_with_default_charset(const char *filename, char *default_charset)
{
#ifdef MIMEDIR_WORK
	VObject *vobj = Parse_MIME_FromFileName((char *) filename);
	GList *list = NULL;
	while(vobj) {
		VObject *next;
		ECard *card = g_object_new (E_TYPE_CARD, NULL);
		parse(card, vobj, default_charset);
		next = nextVObjectInList(vobj);
		cleanVObject(vobj);
		vobj = next;
		list = g_list_prepend(list, card);
	}
	list = g_list_reverse(list);
	return list;
#endif
}

GList *
e_card_load_cards_from_file(const char *filename)
{
	return e_card_load_cards_from_file_with_default_charset (filename, "UTF-8");
}

GList *
e_card_load_cards_from_string_with_default_charset(const char *str, char *default_charset)
{
#ifdef MIMEDIR_WORK
	VObject *vobj = Parse_MIME(str, strlen (str));
	GList *list = NULL;
	while(vobj) {
		VObject *next;
		ECard *card = g_object_new (E_TYPE_CARD, NULL);
		parse(card, vobj, default_charset);
		next = nextVObjectInList(vobj);
		cleanVObject(vobj);
		vobj = next;
		list = g_list_prepend(list, card);
	}
	list = g_list_reverse(list);
	return list;
#endif
}

GList *
e_card_load_cards_from_string(const char *str)
{
	return e_card_load_cards_from_string_with_default_charset (str, "UTF-8");
}

void
e_card_free_empty_lists (ECard *card)
{
	ECardPrivate *priv = card->priv;

	if (priv->address && e_list_length (priv->address) == 0) {
		g_object_unref (priv->address);
		priv->address = NULL;
	}

	if (priv->address_label && e_list_length (priv->address_label) == 0) {
		g_object_unref (priv->address_label);
		priv->address_label = NULL;
	}

	if (priv->phone && e_list_length (priv->phone) == 0) {
		g_object_unref (priv->phone);
		priv->phone = NULL;
	}

	if (priv->categories && e_list_length (priv->categories) == 0) {
		g_object_unref (priv->categories);
		priv->categories = NULL;
	}

	if (priv->arbitrary && e_list_length (priv->arbitrary) == 0) {
		g_object_unref (priv->arbitrary);
		priv->arbitrary = NULL;
	}
}

ECardDate
e_card_date_from_string (const char *str)
{
	ECardDate date;
	int length;

	date.year = 0;
	date.month = 0;
	date.day = 0;

	length = strlen(str);
	
	if (length == 10 ) {
		date.year = str[0] * 1000 + str[1] * 100 + str[2] * 10 + str[3] - '0' * 1111;
		date.month = str[5] * 10 + str[6] - '0' * 11;
		date.day = str[8] * 10 + str[9] - '0' * 11;
	} else if ( length == 8 ) {
		date.year = str[0] * 1000 + str[1] * 100 + str[2] * 10 + str[3] - '0' * 1111;
		date.month = str[4] * 10 + str[5] - '0' * 11;
		date.day = str[6] * 10 + str[7] - '0' * 11;
	}
	
	return date;
}

#ifdef MIMEDIR_WORK
static struct { 
	char *id;
	ECardPhoneFlags flag;
} phone_pairs[] = {
	{ VCPreferredProp,         E_CARD_PHONE_PREF },
	{ VCWorkProp,              E_CARD_PHONE_WORK },
	{ VCHomeProp,              E_CARD_PHONE_HOME },
	{ VCVoiceProp,             E_CARD_PHONE_VOICE },
	{ VCFaxProp,               E_CARD_PHONE_FAX },
	{ VCMessageProp,           E_CARD_PHONE_MSG },
	{ VCCellularProp,          E_CARD_PHONE_CELL },
	{ VCPagerProp,             E_CARD_PHONE_PAGER },
	{ VCBBSProp,               E_CARD_PHONE_BBS },
	{ VCModemProp,             E_CARD_PHONE_MODEM },
	{ VCCarProp,               E_CARD_PHONE_CAR },
	{ VCISDNProp,              E_CARD_PHONE_ISDN },
	{ VCVideoProp,             E_CARD_PHONE_VIDEO },
	{ "X-EVOLUTION-ASSISTANT", E_CARD_PHONE_ASSISTANT },
	{ "X-EVOLUTION-CALLBACK",  E_CARD_PHONE_CALLBACK  },
	{ "X-EVOLUTION-RADIO",     E_CARD_PHONE_RADIO     },
	{ "X-EVOLUTION-TELEX",     E_CARD_PHONE_TELEX     },
	{ "X-EVOLUTION-TTYTDD",    E_CARD_PHONE_TTYTDD    },
};

static ECardPhoneFlags
get_phone_flags (VObject *vobj)
{
	ECardPhoneFlags ret = 0;
	int i;

	for (i = 0; i < sizeof(phone_pairs) / sizeof(phone_pairs[0]); i++) {
		if (isAPropertyOf (vobj, phone_pairs[i].id)) {
			ret |= phone_pairs[i].flag;
		}
	}
	
	return ret;
}

static void
set_phone_flags (VObject *vobj, ECardPhoneFlags flags)
{
	int i;

	for (i = 0; i < sizeof(phone_pairs) / sizeof(phone_pairs[0]); i++) {
		if (flags & phone_pairs[i].flag) {
				addProp (vobj, phone_pairs[i].id);
		}
	}
}

static struct { 
	char *id;
	ECardAddressFlags flag;
} addr_pairs[] = {
	{ VCDomesticProp, E_CARD_ADDR_DOM },
	{ VCInternationalProp, E_CARD_ADDR_INTL },
	{ VCPostalProp, E_CARD_ADDR_POSTAL },
	{ VCParcelProp, E_CARD_ADDR_PARCEL },
	{ VCHomeProp, E_CARD_ADDR_HOME },
	{ VCWorkProp, E_CARD_ADDR_WORK },
	{ "PREF", E_CARD_ADDR_DEFAULT },
};

static ECardAddressFlags
get_address_flags (VObject *vobj)
{
	ECardAddressFlags ret = 0;
	int i;
	
	for (i = 0; i < sizeof(addr_pairs) / sizeof(addr_pairs[0]); i++) {
		if (isAPropertyOf (vobj, addr_pairs[i].id)) {
			ret |= addr_pairs[i].flag;
		}
	}
	
	return ret;
}

static void
set_address_flags (VObject *vobj, ECardAddressFlags flags)
{
	int i;
	
	for (i = 0; i < sizeof(addr_pairs) / sizeof(addr_pairs[0]); i++) {
		if (flags & addr_pairs[i].flag) {
			addProp (vobj, addr_pairs[i].id);
		}
	}
}
#endif

#include <Evolution-Composer.h>

#define COMPOSER_OAFID "OAFIID:GNOME_Evolution_Mail_Composer"

void
e_card_list_send (GList *cards, ECardDisposition disposition)
{
#if PENDING_PORT_WORK
	GNOME_Evolution_Composer composer_server;
	CORBA_Environment ev;

	if (cards == NULL)
		return;

	CORBA_exception_init (&ev);
	
	composer_server = bonobo_activation_activate_from_id (COMPOSER_OAFID, 0, NULL, &ev);

	if (disposition == E_CARD_DISPOSITION_AS_TO) {
		GNOME_Evolution_Composer_RecipientList *to_list, *cc_list, *bcc_list;
		CORBA_char *subject;
		int to_i, bcc_i;
		GList *iter;
		gint to_length = 0, bcc_length = 0;

		/* Figure out how many addresses of each kind we have. */
		for (iter = cards; iter != NULL; iter = g_list_next (iter)) {
			ECard *card = E_CARD (iter->data);
			if (e_card_evolution_list (card)) {
				gint len = card->email ? e_list_length (card->email) : 0;
				if (e_card_evolution_list_show_addresses (card))
					to_length += len;
				else
					bcc_length += len;
			} else {
				if (card->email != NULL)
					++to_length;
			}
		}

		/* Now I have to make a CORBA sequences that represents a recipient list with
		   the right number of entries, for the cards. */
		to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		to_list->_maximum = to_length;
		to_list->_length = to_length;
		if (to_length > 0) {
			to_list->_buffer = CORBA_sequence_GNOME_Evolution_Composer_Recipient_allocbuf (to_length);
		}

		cc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		cc_list->_maximum = cc_list->_length = 0;
		
		bcc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		bcc_list->_maximum = bcc_length;
		bcc_list->_length = bcc_length;
		if (bcc_length > 0) {
			bcc_list->_buffer = CORBA_sequence_GNOME_Evolution_Composer_Recipient_allocbuf (bcc_length);
		}

		to_i = 0;
		bcc_i = 0;
		while (cards != NULL) {
			ECard *card = cards->data;
			EIterator *iterator;
			gchar *name, *addr;
			gboolean is_list, is_hidden, free_name_addr;
			GNOME_Evolution_Composer_Recipient *recipient;

			if (card->email != NULL) {

				is_list = e_card_evolution_list (card);
				is_hidden = is_list && !e_card_evolution_list_show_addresses (card);
			
				for (iterator = e_list_get_iterator (card->email); e_iterator_is_valid (iterator); e_iterator_next (iterator)) {
					
					if (is_hidden) {
						recipient = &(bcc_list->_buffer[bcc_i]);
						++bcc_i;
					} else {
						recipient = &(to_list->_buffer[to_i]);
						++to_i;
					}
					
					name = "";
					addr = "";
					free_name_addr = FALSE;
					if (e_iterator_is_valid (iterator)) {
						
						if (is_list) {
							/* We need to decode the list entries, which are XMLified EDestinations. */
							EDestination *dest = e_destination_import (e_iterator_get (iterator));
							if (dest != NULL) {
								name = g_strdup (e_destination_get_name (dest));
								addr = g_strdup (e_destination_get_email (dest));
								free_name_addr = TRUE;
								g_object_unref (dest);
							}
							
						} else { /* is just a plain old card */
							if (card->name)
								name = e_card_name_to_string (card->name);
							addr = g_strdup ((char *) e_iterator_get (iterator));
							free_name_addr = TRUE;
						}
					}
					
					recipient->name    = CORBA_string_dup (name ? name : "");
					recipient->address = CORBA_string_dup (addr ? addr : "");
					
					if (free_name_addr) {
						g_free ((gchar *) name);
						g_free ((gchar *) addr);
					}
					
					/* If this isn't a list, we quit after the first (i.e. the default) address. */
					if (!is_list)
						break;
					
				}
				g_object_unref (iterator);
			}

			cards = g_list_next (cards);
		}

		subject = CORBA_string_dup ("");

		GNOME_Evolution_Composer_setHeaders (composer_server, "", to_list, cc_list, bcc_list, subject, &ev);
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_printerr ("gui/e-meeting-edit.c: I couldn't set the composer headers via CORBA! Aagh.\n");
			CORBA_exception_free (&ev);
			return;
		}

		CORBA_free (to_list);
		CORBA_free (cc_list);
		CORBA_free (bcc_list);
		CORBA_free (subject);
	}

	if (disposition == E_CARD_DISPOSITION_AS_ATTACHMENT) {
		CORBA_char *content_type, *filename, *description;
		GNOME_Evolution_Composer_AttachmentData *attach_data;
		CORBA_boolean show_inline;
		char *tempstr;

		GNOME_Evolution_Composer_RecipientList *to_list, *cc_list, *bcc_list;
		CORBA_char *subject;
		
		content_type = CORBA_string_dup ("text/x-vcard");
		filename = CORBA_string_dup ("");

		if (cards->next) {
			description = CORBA_string_dup (_("Multiple VCards"));
		} else {
			char *file_as;

			g_object_get(cards->data,
				     "file_as", &file_as,
				     NULL);

			tempstr = g_strdup_printf (_("VCard for %s"), file_as);
			description = CORBA_string_dup (tempstr);
			g_free (tempstr);
		}

		show_inline = FALSE;

		tempstr = e_card_list_get_vcard (cards);
		attach_data = GNOME_Evolution_Composer_AttachmentData__alloc();
		attach_data->_maximum = attach_data->_length = strlen (tempstr);
		attach_data->_buffer = CORBA_sequence_CORBA_char_allocbuf (attach_data->_length);
		strcpy (attach_data->_buffer, tempstr);
		g_free (tempstr);

		GNOME_Evolution_Composer_attachData (composer_server, 
						     content_type, filename, description,
						     show_inline, attach_data,
						     &ev);
	
		if (ev._major != CORBA_NO_EXCEPTION) {
			g_printerr ("gui/e-meeting-edit.c: I couldn't attach data to the composer via CORBA! Aagh.\n");
			CORBA_exception_free (&ev);
			return;
		}
	
		CORBA_free (content_type);
		CORBA_free (filename);
		CORBA_free (description);
		CORBA_free (attach_data);

		to_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		to_list->_maximum = to_list->_length = 0;
		
		cc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		cc_list->_maximum = cc_list->_length = 0;

		bcc_list = GNOME_Evolution_Composer_RecipientList__alloc ();
		bcc_list->_maximum = bcc_list->_length = 0;

		if (!cards || cards->next) {
			subject = CORBA_string_dup ("Contact information");
		} else {
			ECard *card = cards->data;
			const gchar *tempstr2;

			tempstr2 = NULL;
			g_object_get(card,
				     "file_as", &tempstr2,
				     NULL);
			if (!tempstr2 || !*tempstr2)
				g_object_get(card,
					     "full_name", &tempstr2,
					     NULL);
			if (!tempstr2 || !*tempstr2)
				g_object_get(card,
					     "org", &tempstr2,
					     NULL);
			if (!tempstr2 || !*tempstr2) {
				EList *list;
				EIterator *iterator;
				g_object_get(card,
					     "email", &list,
					     NULL);
				iterator = e_list_get_iterator (list);
				if (e_iterator_is_valid (iterator)) {
					tempstr2 = e_iterator_get (iterator);
				}
				g_object_unref (iterator);
			}

			if (!tempstr2 || !*tempstr2)
				tempstr = g_strdup_printf ("Contact information");
			else
				tempstr = g_strdup_printf ("Contact information for %s", tempstr2);
			subject = CORBA_string_dup (tempstr);
			g_free (tempstr);
		}
		
		GNOME_Evolution_Composer_setHeaders (composer_server, "", to_list, cc_list, bcc_list, subject, &ev);

		CORBA_free (to_list);
		CORBA_free (cc_list);
		CORBA_free (bcc_list);
		CORBA_free (subject);
	}

	GNOME_Evolution_Composer_show (composer_server, &ev);

	if (ev._major != CORBA_NO_EXCEPTION) {
		g_printerr ("gui/e-meeting-edit.c: I couldn't show the composer via CORBA! Aagh.\n");
		CORBA_exception_free (&ev);
		return;
	}

	CORBA_exception_free (&ev);
#endif
}

void
e_card_send (ECard *card, ECardDisposition disposition)
{
	GList *list;
	list = g_list_prepend (NULL, card);
	e_card_list_send (list, disposition);
	g_list_free (list);
}

gboolean
e_card_evolution_list (ECard *card)
{
	ECardPrivate *priv;

	g_return_val_if_fail (card && E_IS_CARD (card), FALSE);

	priv = card->priv;

	return priv->list;
}

gboolean
e_card_evolution_list_show_addresses (ECard *card)
{
	ECardPrivate *priv;

	g_return_val_if_fail (card && E_IS_CARD (card), FALSE);

	priv = card->priv;

	return priv->list_show_addresses;
}

typedef struct _CardLoadData CardLoadData;
struct _CardLoadData {
	gchar *card_id;
	ECardCallback cb;
	gpointer closure;
};

static void
get_card_cb (EBook *book, EBookStatus status, ECard *card, gpointer closure)
{
	CardLoadData *data = (CardLoadData *) closure;

	if (data->cb != NULL) {
		if (status == E_BOOK_STATUS_SUCCESS)
			data->cb (card, data->closure);
		else
			data->cb (NULL, data->closure);
	}

	g_free (data->card_id);
	g_free (data);
}

static void
card_load_cb (EBook *book, EBookStatus status, gpointer closure)
{
	CardLoadData *data = (CardLoadData *) closure;

	if (status == E_BOOK_STATUS_SUCCESS)
		e_book_get_card (book, data->card_id, get_card_cb, closure);
	else {
		data->cb (NULL, data->closure);
		g_free (data->card_id);
		g_free (data);
	}
}

void
e_card_load_uri (const gchar *book_uri, const gchar *uid, ECardCallback cb, gpointer closure)
{
	CardLoadData *data;
	EBook *book;
	
	data          = g_new (CardLoadData, 1);
	data->card_id = g_strdup (uid);
	data->cb      = cb;
	data->closure = closure;

	book = e_book_new ();
	e_book_load_uri (book, book_uri, card_load_cb, data);
}
