/*
 * 
 * Authors: Srinivasa Ragavan <sragavan@novell.com>
 *
 * */

#include <stdio.h>
#include <glib.h>
#include <string.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include "evo-dbus.h"
#include <dbind.h>
#include <dbind-any.h>
#include "camel-object-remote.h"
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define d(x) x

static DBindContext *main_ctx = NULL;
static GPrivate *per_thread_ctx = NULL;

static void
per_thread_private_destroy (gpointer ctx)
{
	dbind_context_free (ctx);
}

int
evolution_dbus_init (void)
{
	if (main_ctx)
		return 0;

	main_ctx = dbind_create_context (DBUS_BUS_SESSION, NULL);
	/* Set up this connection to work in a GLib event loop */
	dbus_connection_setup_with_g_main (main_ctx->cnx, NULL);

	if (!main_ctx) {
		g_warning ("DBind main context setup failed\n");
		return -1;
	}

	d(printf("DBind context setup: done\n"));

	per_thread_ctx = g_private_new (per_thread_private_destroy);

	return 0;
}

DBindContext *
evolution_dbus_peek_main_context (void)
{
	if (!main_ctx)
		evolution_dbus_init ();
	return main_ctx;
}

/*
 * Sadly we need one per thread to make dbus reliable.
 */
DBindContext *
evolution_dbus_peek_context (void)
{
	DBindContext *ctx;

	if (!main_ctx)
		evolution_dbus_init ();

	ctx = (DBindContext *)g_private_get (per_thread_ctx);
	if (!ctx) {
		ctx = dbind_create_context (DBUS_BUS_SESSION, NULL);
		g_private_set (per_thread_ctx, ctx);
	}

	return ctx;
}
