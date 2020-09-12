#include "blather.h"


simpio_t simpio_actual;
simpio_t *simpio = &simpio_actual;

client_t client_actual;
client_t *client = &client_actual;

pthread_t user_thread;          // thread managing user input
pthread_t background_thread;  // thread managing comm with chat server

join_t join;

sem_t *log_sem;

// Worker thread to manage user input, called via pthread
void *user_worker(void *arg){
  int sendfd = *((int *)arg);
  while(!simpio->end_of_input){ //end of input reached via ctrl+d
    simpio_reset(simpio); //ready to process new input
    iprintf(simpio, "");  // print prompt
    while(!simpio->line_ready && !simpio->end_of_input){          // read until line is complete
      simpio_get_char(simpio); //read user's typed input
    }
    if(simpio->line_ready){ //user finished typing a message
      // send client's msg
      mesg_t msg = {
        .kind = BL_MESG
      };
      strncpy(msg.name, join.name, MAXNAME);
      strncpy(msg.body, simpio->buf, MAXLINE);
      int bytes = write(sendfd, &msg, sizeof(mesg_t)); //send to server
      check_fail(bytes != sizeof(mesg_t), 1, "there was an issue sending that message\n");
    }
  }
  //client terminated
  mesg_t msg = {
    .kind = BL_DEPARTED
  };
  strncpy(msg.name, join.name, MAXPATH);
  int bytes_ = write(sendfd, &msg, sizeof(mesg_t));
  check_fail(bytes_ != sizeof(mesg_t), 1, "there was an issue leaving the server\n");

  pthread_cancel(background_thread); // kill the background thread
  return NULL;
}

// Worker thread to listen to the info from the server.
void *background_worker(void *arg){
  int sendfd = *((int *)arg); //just for pinging back to the server
	int recvfd = *((int *)arg+1); 
  int logfd = *((int *)arg+2);
  char buf[MAXLINE+MAXNAME+8];
  mesg_t msg;
  while(1) { //terminate once a shutdown message is received or user_worker says to
    int bytes = read(recvfd, &msg, sizeof(mesg_t)); //block thread until activity from server comes in
    check_fail(bytes != sizeof(mesg_t), 1, "there was an issue reading an incoming message\n");
    // mesg_t's can indicate all sorts of server/client activities
    if (msg.kind == BL_PING) {
      mesg_t msg = {
        .kind = BL_PING
      };
      strncpy(msg.name, join.name, MAXNAME);
      int bytes_ = write(sendfd, &msg, sizeof(mesg_t));
      check_fail(bytes_ != sizeof(mesg_t), 1, "ping failure\n");
    } else {
      iprintf(simpio, "%s", client_format_mesg(&msg, buf));
      if (msg.kind == BL_SHUTDOWN)
        break;

      if (DO_ADVANCED) {
        //handle the magic %last <num> chat command
        int num_last;
        if ((num_last = client_parse_last(msg.body))) {
          lseek(logfd,-sizeof(mesg_t)*num_last,SEEK_END);
          mesg_t logmsg;
          iprintf(simpio, "====================\n");
          iprintf(simpio, "LAST %d MESSAGES\n",num_last);
          for (int i = 0; i < num_last; i++) {
            int bytes__ = read(logfd, &logmsg, sizeof(mesg_t));
            check_fail(bytes__ != sizeof(mesg_t), 1, "failed to read from logfile\n");
            iprintf(simpio, "%s", client_format_mesg(&logmsg, buf));
          }
          iprintf(simpio, "====================\n");
        }

        //handle the magic %who chat command
        if (client_parse_who(msg.body)) {
          who_t who;
          sem_wait(log_sem);
          int bytes2 = pread(logfd,&who,sizeof(who_t),0);
          check_fail(bytes2 != sizeof(who_t), 1, "couldn't determine who is here\n");
          sem_post(log_sem);
          iprintf(simpio, "====================\n");
          iprintf(simpio,"%d CLIENTS\n", who.n_clients);
          for (int i = 0; i < who.n_clients; i++) {
            iprintf(simpio,"%d: %s\n", i, who.names[i]);
          }
          iprintf(simpio, "====================\n");
        }
      }
    }
  }
  //server shut down
  pthread_cancel(user_thread); // kill the user thread
  return NULL;
}

int main(int argc, char **argv) {
	check_fail(argc < 3, 0, "usage: %s <server name> <user name>\n", argv[0]);
  if (getenv("BL_ADVANCED"))
    DO_ADVANCED = 1;

  //set up communication channels between this client and the server; 
  //one going towards the server and one coming back 
	snprintf(join.name, MAXNAME, "%s", argv[2]);
	pid_t id = getpid();
	snprintf(join.to_client_fname, MAXPATH, "%d.client.fifo", id);
	snprintf(join.to_server_fname, MAXPATH, "%d.server.fifo", id);
	
	mkfifo(join.to_client_fname, S_IRUSR | S_IWUSR);
  int recvfd = open(join.to_client_fname, O_RDWR, S_IRUSR | S_IWUSR);
  check_fail(recvfd == -1, 1, "failed to open comms channel\n");
  mkfifo(join.to_server_fname, S_IRUSR | S_IWUSR);
  int sendfd = open(join.to_server_fname, O_RDWR, S_IRUSR | S_IWUSR);
  check_fail(sendfd == -1, 1, "failed to open comms channel\n");

  char server_name[MAXPATH];
	snprintf(server_name, MAXPATH, "%s", argv[1]);

  //send the join_t request to the server.
  char fifo_name[MAXPATH+5];
	snprintf(fifo_name, MAXPATH+5, "%s.fifo", server_name);
	int joinfd = open(fifo_name, O_RDWR, S_IRUSR | S_IWUSR);
	int bytes = write(joinfd, &join, sizeof(join_t));
  check_fail(bytes != sizeof(join_t), 1, "failed to join server\n");
	//now we can send chat messages

  int logfd = -1;
  char log_name[MAXPATH+4];
  char sem_name[MAXPATH+5];
  if (DO_ADVANCED) {
    //open the log file to emit %last <num> commands
    snprintf(log_name, MAXPATH+5, "%s.log", server_name);
    logfd = open(log_name, O_RDONLY);
    check_fail(logfd == -1, 1, "logging failure\n");

    //open the semaphore for safely reading the concurrently maintained who_t in the above log file
    snprintf(sem_name,MAXPATH+5,"/%s.sem",server_name); //this is actually unsafe. the sem can only have a name len up to 251
    log_sem = sem_open(sem_name, 0, S_IRUSR | S_IWUSR);
    check_fail(log_sem == SEM_FAILED, 1, "there was an issue interacting with the server\n");
  }

  //set up client's terminal UX
	char prompt[MAXNAME+3];
	snprintf(prompt, MAXNAME+3, "%s>> ",join.name); // create a prompt string
	simpio_set_prompt(simpio, prompt);              // set the prompt
	simpio_reset(simpio);                           // ready for new input
	simpio_noncanonical_terminal_mode();            // set the terminal into a compatible mode

  // this process now splits into two threads, mentioned above
  int fds[4] = {sendfd, recvfd, logfd};
	pthread_create(&user_thread, NULL, user_worker, (void *)fds);     // start user thread to read input
	pthread_create(&background_thread, NULL, background_worker, (void *)fds);
	pthread_join(user_thread, NULL);
	pthread_join(background_thread, NULL);
  // the threads will always terminate together
	
  //the client has exited, either because the chosen server closed
  //or because ctrl+d end of input has been reached
  close(recvfd);
  unlink(join.to_client_fname);
  close(sendfd);
  unlink(join.to_server_fname);
  close(logfd);
  sem_close(log_sem);
	simpio_reset_terminal_mode(); // return terminal to saved previous settings
	printf("\n");
}
