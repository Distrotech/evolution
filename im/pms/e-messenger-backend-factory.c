#include <config.h>
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-main.h>
#include <liboaf/liboaf.h>

#include "e-messenger-backend-dispatcher.h"

#define FACTORY_IID "OAFIID:GNOME_Evolution_Messenger_BackendDispatcherFactory"

static BonoboGenericFactory *factory = NULL;
static int running_objects = 0;

static void
factory_destroy_cb(BonoboObject *object)
{
	running_objects--;

	if (running_objects)
		return;

	bonobo_object_unref(BONOBO_OBJECT(factory));

	factory = NULL;

	gtk_main_quit();
} /* factory_destroy_cb */

static BonoboObject *
factory_cb(BonoboGenericFactory *factory, gpointer data)
{
	EMessengerBackendDispatcher *dispatcher;

	running_objects++;

	dispatcher = e_messenger_backend_dispatcher_new();

	gtk_signal_connect(
		GTK_OBJECT(dispatcher), "destroy",
		GTK_SIGNAL_FUNC(factory_destroy_cb), NULL);

	return BONOBO_OBJECT(dispatcher);
} /* factory_cb */

static void
factory_init(void)
{
	if (factory != NULL)
		return;

	factory = bonobo_generic_factory_new(FACTORY_IID, factory_cb, NULL);

	if (factory == NULL)
		g_error("Could not create the component factory");
} /* factory_init */

int
main(int argc, char *argv[])
{
	CORBA_ORB orb;

	gnome_init_with_popt_table("pms", VERSION, argc, argv,
				   oaf_popt_options, 0, NULL);

	orb = oaf_init(argc, argv);

	if (bonobo_init(orb, CORBA_OBJECT_NIL, CORBA_OBJECT_NIL) == FALSE)
		g_error("Could not initalize Bonobo");

	factory_init();

	bonobo_main();

	return 0;
} /* main */
