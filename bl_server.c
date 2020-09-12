#include "blather.h"

volatile int sigalarm = 0;
volatile int sigterm = 0;
static void handle_signals(int signum) {
  //server has been signalled
  if (signum == SIGTERM || signum == SIGINT)
    sigterm = 1; //time to quit gracefully
  else if (signum == SIGALRM)
    sigalarm = 1; //time to ping clients
}

void *spawn_server_write_who_as_thread(void *server){
  server_write_who((server_t *)server);
  return NULL;
}

int main(int argc, char **argv) {
  check_fail(argc < 2,0,"usage: %s <desired server name>\n", argv[0]);
  if (getenv("BL_ADVANCED"))
    DO_ADVANCED = 1;

  //install signal hander to indicate (1) when the server should stop and (2) when it should ping clients
  struct sigaction sa;
  sa.sa_handler = handle_signals;
  sa.sa_flags = SA_RESTART;  //make certain system calls restartable across signals
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL); //ctrl+c for graceful shutdown
  sigaction(SIGTERM, &sa, NULL); //SIGKILL and SIGSTOP will still ungracefully halt execution
  sigaction(SIGALRM, &sa, NULL);

  server_t server;
  server_start(&server, argv[1], S_IRUSR | S_IWUSR);

  if (DO_ADVANCED) {
    alarm(1); //alarm for pinging 
  }

  //loop forever unless signalled to stop
  while(!sigterm) {
    dbg_printf("At the top of main loop\n");
    if (sigalarm) {
      sigalarm = 0;
      dbg_printf("Alarm went off\n");
      server_tick(&server);
      server_ping_clients(&server);
      server_remove_disconnected(&server, 5);
      pthread_t write_who;
      pthread_create(&write_who, NULL, 
        spawn_server_write_who_as_thread, (void *)&server); //server_write_who in its own thread 
      alarm(1);                                             //because of the blocking semaphore
    }
    server_check_sources(&server);
    dbg_printf("Finished checking sources\n");
    if (server_join_ready(&server))
      server_handle_join(&server);
    dbg_printf("Checking %d clients\n", server.n_clients);
    for (int i = 0; i < server.n_clients;i++)
      if(server_client_ready(&server, i))
        server_handle_client(&server, i);
  }
  server_shutdown(&server);
  return 0;
}