#include "calserv.h"

CSServer *
cs_server_new(void)
{
  CSServer *rv;

  rv = g_new0(CSServer, 1);

  rv->mainloop = g_main_new();
}

void
cs_server_run(CSServer *server)
{
  g_return_if_fail(server);

  g_main_run(server->mainloop);
}

void
cs_server_destroy(CSServer *server)
{
  g_return_if_fail(server);
  
  g_main_quit(server->mainloop);
  g_main_destroy(server->mainloop);

  close(server->servfd);
}
