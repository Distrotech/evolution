#include <glib.h>
#include <unistd.h>
#include <sys/socket.h>
#include "calserv.h"

int main(int argc, char *argv[])
{
  CSServer *serv;

  serv = cs_server_new();
  g_return_val_if_fail(serv, 1);

  cs_server_run(serv);

  cs_server_destroy(serv);

  return 0;
}
