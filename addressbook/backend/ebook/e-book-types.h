/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * A client-side GObject which exposes the
 * Evolution:BookListener interface.
 *
 * Author:
 *   Nat Friedman (nat@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifndef __E_BOOK_TYPES_H__
#define __E_BOOK_TYPES_H__

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	E_BOOK_STATUS_OK,
	E_BOOK_STATUS_INVALID_ARG,
	E_BOOK_STATUS_BUSY,
	E_BOOK_STATUS_REPOSITORY_OFFLINE,
	E_BOOK_STATUS_NO_SUCH_BOOK,
	E_BOOK_STATUS_URI_NOT_LOADED,
	E_BOOK_STATUS_URI_ALREADY_LOADED,
	E_BOOK_STATUS_PERMISSION_DENIED,
	E_BOOK_STATUS_CARD_NOT_FOUND,
	E_BOOK_STATUS_CARD_ID_ALREADY_EXISTS,
	E_BOOK_STATUS_PROTOCOL_NOT_SUPPORTED,
	E_BOOK_STATUS_CANCELLED,
	E_BOOK_STATUS_COULD_NOT_CANCEL,
	E_BOOK_STATUS_AUTHENTICATION_FAILED,
	E_BOOK_STATUS_AUTHENTICATION_REQUIRED,
	E_BOOK_STATUS_TLS_NOT_AVAILABLE,
	E_BOOK_STATUS_CORBA_EXCEPTION,
	E_BOOK_STATUS_OTHER_ERROR
} EBookStatus;


typedef enum {
	E_BOOK_VIEW_STATUS_OK,
	E_BOOK_VIEW_STATUS_TIME_LIMIT_EXCEEDED,
	E_BOOK_VIEW_STATUS_SIZE_LIMIT_EXCEEDED,
	E_BOOK_VIEW_STATUS_INVALID_QUERY,
	E_BOOK_VIEW_STATUS_QUERY_REFUSED,
	E_BOOK_VIEW_STATUS_OTHER_ERROR
} EBookViewStatus;

G_END_DECLS

#endif /* ! __E_BOOK_TYPES_H__ */
