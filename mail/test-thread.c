/* Tests the multithreaded UI code */

#include "config.h"
#include <unistd.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <libgnomeui/libgnomeui.h>
#include <stdio.h>
#include "mail-threads.h"

#ifdef USE_BROKEN_THREADS

static void op_1( gpointer in, gpointer op, CamelException *ex );
static void op_2( gpointer in, gpointer op, CamelException *ex );
static void op_3( gpointer in, gpointer op, CamelException *ex );
static void op_4( gpointer in, gpointer op, CamelException *ex );
static void op_5( gpointer in, gpointer op, CamelException *ex );
static void done( gpointer in, gpointer op, CamelException *ex );
static void exception( gpointer in, gpointer op, CamelException *ex );
static gboolean queue_ops( void );

const mail_operation_spec spec1 = { "Show The Crawling Progress Bar of Doom", "Crawling",
				    0, NULL, op_1, done };
const mail_operation_spec spec2 = { "Explore The Mysterious Message Setter", "Exploring",
				    0, NULL, op_2, done };
const mail_operation_spec spec3 = { "Dare The Error Dialog of No Return", "Daring",
				    0, NULL, op_3, done };
const mail_operation_spec spec4 = { "Queue Filler", "Queueing",
				    0, NULL, op_4, NULL };
const mail_operation_spec spec5 = { "Avoid the Dastardly Password Stealer", "Avoiding",
				    0, NULL, op_5, done };
const mail_operation_spec spec6 = { "Exception on setup", "Exceptioning",
				    0, exception, op_4, NULL };
const mail_operation_spec spec7 = { "Exception during op", "Exceptioning",
				    0, NULL, exception, NULL };
const mail_operation_spec spec8 = { "Exception in cleanup", "Exceptioning",
				    0, NULL, op_4, exception };

static gboolean queue_ops( void )
{
	int i;

	g_message( "Top of queue_ops" );

	mail_operation_queue( &spec1, "op1 finished", FALSE );
	mail_operation_queue( &spec2, "op2 finished", FALSE );
	mail_operation_queue( &spec3, "op3 finished", FALSE );

	for( i = 0; i < 3; i++ ) {
		mail_operation_queue( &spec4, GINT_TO_POINTER( i ), FALSE );
	}

	g_message( "Waiting for finish..." );
	mail_operation_wait_for_finish();

	g_message( "Ops done -- queue some more!" );

	mail_operation_queue( &spec1, "done a second time", FALSE );

	g_message( "Waiting for finish again..." );
	mail_operation_wait_for_finish();

	g_message( "Ops done -- more, more!" );

	mail_operation_queue( &spec5, "passwords stolen", FALSE );

	for( i = 0; i < 3; i++ ) {
		mail_operation_queue( &spec4, GINT_TO_POINTER( i ), FALSE );
	}

	mail_operation_queue( &spec6, NULL, FALSE );
	mail_operation_queue( &spec7, NULL, FALSE );
	mail_operation_queue( &spec8, NULL, FALSE );

	g_message( "Waiting for finish AGAIN..." );
	mail_operation_wait_for_finish();
	g_message( "Ops done again. Exiting 0" );
	gtk_exit( 0 );
	return FALSE;
}

static void exception( gpointer in, gpointer op, CamelException *ex )
{
	camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM, "I don't feel like it.");
}

static void op_1( gpointer in, gpointer op, CamelException *ex )
{
	gfloat pct;

	mail_op_show_progressbar();
	mail_op_set_message( "Watch the progress bar!" );

	for( pct = 0.0; pct < 1.0; pct += 0.2 ) {
		sleep( 1 );
		mail_op_set_percentage( pct );
	}
}

static void op_2( gpointer in, gpointer op, CamelException *ex )
{
	int i;

	mail_op_hide_progressbar();
	for( i = 5; i > 0; i-- ) {
		mail_op_set_message( "%d", i );
		sleep( 1 );
	}

	mail_op_set_message( "BOOOM!" );
	sleep( 1 );
}

static void op_3( gpointer in, gpointer op, CamelException *ex )
{
	gfloat pct;

	mail_op_show_progressbar();
	mail_op_set_message( "Frobulating the foosamatic" );

	for( pct = 0.0; pct < 0.3; pct += 0.1 ) {
		mail_op_set_percentage( pct );
		sleep( 1 );
	}

	mail_op_error( "Oh no! The foosamatic was booby-trapped!" );
	sleep( 1 );
}

static void op_4( gpointer in, gpointer op, CamelException *ex )
{
	mail_op_hide_progressbar();
	mail_op_set_message( "Filler # %d", GPOINTER_TO_INT( in ) );
	sleep( 1 );
}

static void op_5( gpointer in, gpointer op, CamelException *ex )
{
	gchar *pass;
	gboolean ret;

	mail_op_show_progressbar();
	mail_op_set_percentage( 0.5 );

	ret = mail_op_get_password( "What is your super-secret password?", TRUE, &pass );

	if( ret == FALSE )
		mail_op_set_message( "Oh no, you cancelled! : %s", pass );
	else
		mail_op_set_message( "\"%s\", you said?", pass );

	sleep( 1 );
}

static void done( gpointer in, gpointer op, CamelException *ex )
{
	g_message( "Operation done: %s", (gchar *) in );
}

int main( int argc, char **argv )
{
	g_thread_init( NULL );
	gnome_init( "test-thread", "0.0", argc, argv );
	gtk_idle_add( (GtkFunction) queue_ops, NULL );
	gtk_main();
	return 0;
}

#else

int main( int argc, char **argv )
{
	g_message( "Threads aren't enabled, so they cannot be tested." );
	return 0;
}

#endif
