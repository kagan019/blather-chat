#include "blather.h"

client_t *server_get_client(server_t *server, int idx) {
// Gets a pointer to the client_t struct at the given index. If the
// index is beyond n_clients, the behavior of the function is
// unspecified and may cause a program crash.
  dbg_printf("Fetching client %d\n", idx);
  return server->client + idx;
}

void server_start(server_t *server, char *server_name, int perms) {
// Initializes and starts the server with the given name. A join fifo
// called "server_name.fifo" should be created. Removes any existing
// file of that name prior to creation. Opens the FIFO and stores its
// file descriptor in join_fd.
//
// ADVANCED: create the log file "server_name.log" and write the
// initial empty who_t contents to its beginning. Ensure that the
// log_fd is position for appending to the end of the file. Create the
// POSIX semaphore "/server_name.sem" and initialize it to 1 to
// control access to the who_t portion of the log.
// 
// LOG Messages:
// log_printf("BEGIN: server_start()\n");              // at beginning of function
// log_printf("END: server_start()\n");                // at end of function
  log_printf("BEGIN: server_start()\n");
  snprintf(server->server_name, MAXPATH, "%s", server_name);

  //open .fifo communication channel
  char fifoname[MAXPATH+5];
  snprintf(fifoname, MAXPATH+5, "%s.fifo", server->server_name);
  unlink(fifoname);
  mkfifo(fifoname, perms);
  server->join_fd = open(fifoname, O_RDWR, perms);
  check_fail(server->join_fd == -1, 1, "couldn't open fifo %s\n", fifoname); //for calls like these, need to fail fast and fail loudly
  server->join_ready = 0;
  server->n_clients = 0;

  if (DO_ADVANCED) {
    // open .log activity record
    char logname[MAXPATH+4];
    snprintf(logname, MAXPATH+4, "%s.log", server->server_name);
    server->log_fd = open(logname, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR); 
    check_fail(server->log_fd == -1, 1, "couldn't open logfile %s\n", logname);
    char semname[MAXPATH+5];
    snprintf(semname,MAXPATH+5,"/%s.sem", server->server_name); //this is actually unsafe. the sem can only have a name len up to 251
    server->log_sem = sem_open(semname, O_CREAT, S_IRUSR | S_IWUSR, 1);
    check_fail(server->log_sem == SEM_FAILED, 1, "couldn't open semaphore %s\n", semname);
    server_write_who(server); //document chat members
    lseek(server->log_fd,0,SEEK_END);
  }

  log_printf("END: server_start()\n");
}

void server_shutdown(server_t *server) {
// Shut down the server. Close the join FIFO and unlink (remove) it so
// that no further clients can join. Send a BL_SHUTDOWN message to all
// clients and proceed to remove all clients in any order.
// 
// ADVANCED: Close the log file. Close the log semaphore and unlink
// it.
//
// LOG Messages:
// log_printf("BEGIN: server_shutdown()\n");           // at beginning of function
// log_printf("END: server_shutdown()\n");             // at end of function
  log_printf("BEGIN: server_shutdown()\n");
  close(server->join_fd);
  char fifoname[MAXPATH+5];
  snprintf(fifoname, MAXPATH+5, "%s.fifo", server->server_name);
  unlink(fifoname);
  mesg_t shtdn_msg = {
    .kind = BL_SHUTDOWN,
  };
  server_broadcast(server, &shtdn_msg);
  for (int i = server->n_clients -1 ; i >= 0; i--) {
    server_remove_client(server, i);
  }
  if (DO_ADVANCED) {
    close(server->log_fd);
    sem_close(server->log_sem);
    char semname[MAXPATH+5];
    snprintf(semname,MAXPATH+5,"/%s.sem",server->server_name);
    sem_unlink(semname);
  }
  log_printf("END: server_shutdown()\n");
}

int server_add_client(server_t *server, join_t *join) {
// Adds a client to the server according to the parameter join which
// should have fileds such as name filed in.  The client data is
// copied into the client[] array and file descriptors are opened for
// its to-server and to-client FIFOs. Initializes the data_ready field
// for the client to 0. Returns 0 on success and non-zero if the
// server as no space for clients (n_clients == MAXCLIENTS).
//
// LOG Messages:
// log_printf("BEGIN: server_add_client()\n");         // at beginning of function
// log_printf("END: server_add_client()\n");           // at end of function
  log_printf("BEGIN: server_add_client()\n");
  if (server->n_clients == MAXCLIENTS)
    return 1;
  client_t *newclient = server_get_client(server, server->n_clients);
  newclient->data_ready = 0;
  newclient->last_contact_time = server->time_sec;
  strncpy(newclient->name, join->name, MAXNAME);
  strncpy(newclient->to_server_fname, join->to_server_fname, MAXPATH);
  newclient->to_server_fd = open(newclient->to_server_fname, O_RDWR, S_IRUSR | S_IWUSR );
  check_fail(newclient->to_server_fd == -1, 1, "couldn't open client %s's comm channel\n", newclient->name);
  strncpy(newclient->to_client_fname, join->to_client_fname, MAXPATH);
  newclient->to_client_fd = open(newclient->to_client_fname, O_RDWR, S_IRUSR | S_IWUSR );
  check_fail(newclient->to_client_fd == -1, 1, "couldn't open client %s's comm channel\n", newclient->name);
  server->n_clients++;
  log_printf("END: server_add_client()\n");
  return 0;
}

int server_remove_client(server_t *server, int idx) {
// Remove the given client likely due to its having departed or
// disconnected. Close fifos associated with the client and remove
// them.  Shift the remaining clients to lower indices of the client[]
// preserving their order in the array; decreases n_clients.
  client_t *client = server_get_client(server, idx);
  dbg_printf("Removing client %d, '%s'\n", idx, client->name);
  close(client->to_server_fd);
  unlink(client->to_server_fname);
  close(client->to_client_fd);
  unlink(client->to_client_fname);
  for (int i = idx+1; i < server->n_clients; i++) {
    server->client[i-1] = server->client[i]; 
  }
  server->n_clients--;
  return 0;
}

int server_broadcast(server_t *server, mesg_t *mesg) {
// Send the given message to all clients connected to the server by
// writing it to the file descriptors associated with them.
//
// ADVANCED: Log the broadcast message unless it is a PING which
// should not be written to the log.
  dbg_printf("broadcasting message #%d from user %s\n", mesg->kind, mesg->name);
  for (int i = 0; i < server->n_clients; i++) {
    client_t *cur = server_get_client(server, i);
    int bytes = write(cur->to_client_fd, mesg, sizeof(mesg_t)); 
    check_fail(bytes != sizeof(mesg_t), 1, "an issue messaging the client '%s' occurred\n", cur->name);
  }
  if (DO_ADVANCED && mesg->kind != BL_PING) {
    server_log_message(server, mesg);
  }
  return 0;
}

void server_check_sources(server_t *server) {
// Checks all sources of data for the server to determine if any are
// ready for reading. Sets the servers join_ready flag and the
// data_ready flags of each of client if data is ready for them.
// Makes use of the poll() system call to efficiently determine
// which sources are ready.
// 
// LOG Messages:
// log_printf("BEGIN: server_check_sources()\n");             // at beginning of function
// log_printf("poll()'ing to check %d input sources\n",...);  // prior to poll() call
// log_printf("poll() completed with return value %d\n",...); // after poll() call
// log_printf("poll() interrupted by a signal\n");            // if poll interrupted by a signal
// log_printf("join_ready = %d\n",...);                       // whether join queue has data
// log_printf("client %d '%s' data_ready = %d\n",...)         // whether client has data ready
// log_printf("END: server_check_sources()\n");               // at end of function
  log_printf("BEGIN: server_check_sources()\n");
  struct pollfd pfds[MAXCLIENTS + 1]; //clients + join_fd  
  pfds[0].fd = server->join_fd;
  pfds[0].events = POLLIN;                                
  for (int i = 0; i < server->n_clients; i++) {
    pfds[i+1].fd = server->client[i].to_server_fd;                                    
    pfds[i+1].events = POLLIN;                
  }           
  log_printf("poll()'ing to check %d input sources\n",server->n_clients+1);
  int ret = poll(pfds, server->n_clients + 1, -1);
  log_printf("poll() completed with return value %d\n",ret);
  if (ret == -1 && errno == EINTR) {
    log_printf("poll() interrupted by a signal\n");
    log_printf("END: server_check_sources()\n");  
    return;
  }
  check_fail(ret == -1, 1, "the sever is having trouble with its comms channels\n");
  
  // indicate which sources have been reported as ready by poll()
  if (pfds[0].revents & POLLIN) {
    server->join_ready = 1;
  }
  log_printf("join_ready = %d\n",server->join_ready);
  int j=0;
  for(int i = 1; i < server->n_clients + 1; i++) {
    client_t *cur = server_get_client(server, i-1);
    if( pfds[i].revents & POLLIN ){                            
      cur->data_ready = 1;
      j++;
    }
    log_printf("client %d '%s' data_ready = %d\n",i-1,cur->name,cur->data_ready);
  }     
  log_printf("END: server_check_sources()\n");           
}

int server_join_ready(server_t *server) {
// Return the join_ready flag from the server which indicates whether
// a call to server_handle_join() is safe.
  return server->join_ready;
}

int server_handle_join(server_t *server){
// Call this function only if server_join_ready() returns true. Read a 
// join request and add the new client to the server. After finishing,
// set the servers join_ready flag to 0.
//
// LOG Messages:
// log_printf("BEGIN: server_handle_join()\n");               // at beginnning of function
// log_printf("join request for new client '%s'\n",...);      // reports name of new client
// log_printf("END: server_handle_join()\n");                 // at end of function
  log_printf("BEGIN: server_handle_join()\n");
  join_t join;
  int bytes = read(server->join_fd, &join, sizeof(join_t));
  check_fail(bytes != sizeof(join_t), 1, "an join error occurred\n");
  log_printf("join request for new client '%s'\n",join.name);
  server_add_client(server, &join);
  server->join_ready = 0;
  mesg_t msg = {
    .kind = BL_JOINED,
  };
  strncpy(msg.name, join.name, MAXNAME);
  server_broadcast(server, &msg);
  log_printf("END: server_handle_join()\n");
  return 0;
}

int server_client_ready(server_t *server, int idx) {
// Return the data_ready field of the given client which indicates
// whether the client has data ready to be read from it.
  return server_get_client(server, idx)->data_ready;
}

int server_handle_client(server_t *server, int idx) {
// Process a message from the specified client. This function should
// only be called if server_client_ready() returns true. Read a
// message from to_server_fd and analyze the message kind. Departure
// and Message types should be broadcast to all other clients.  Ping
// responses should only change the last_contact_time below. Behavior
// for other message types is not specified. Clear the client's
// data_ready flag so it has value 0.
//
// ADVANCED: Update the last_contact_time of the client to the current
// server time_sec.
//
// LOG Messages:
// log_printf("BEGIN: server_handle_client()\n");           // at beginning of function
// log_printf("client %d '%s' DEPARTED\n",                  // indicates client departed
// log_printf("client %d '%s' MESSAGE '%s'\n",              // indicates client message
// log_printf("END: server_handle_client()\n");             // at end of function 
  log_printf("BEGIN: server_handle_client()\n");
  client_t *client = server_get_client(server, idx);
  client->data_ready = 0;
  mesg_t msg;
  int bytes = read(client->to_server_fd, &msg, sizeof(mesg_t));
  check_fail(bytes != sizeof(mesg_t), 1, "a messaging error occured with client '%s'\n", client->name);
  if (msg.kind == BL_MESG) {
    server_broadcast(server, &msg);
    log_printf("client %d '%s' MESSAGE '%s'\n", idx,msg.name,msg.body);
  }
  else if (msg.kind == BL_DEPARTED) {
    server_remove_client(server, idx);
    server_broadcast(server, &msg);
    log_printf("client %d '%s' DEPARTED\n", idx,msg.name);
  }
  else if (msg.kind == BL_PING) {
    client->last_contact_time = server->time_sec;
    log_printf("client %d '%s' PINGED\n", idx,msg.name);
  }
  log_printf("END: server_handle_client()\n");
  return 0;
}
void server_tick(server_t *server){
// ADVANCED: Increment the time for the server
  server->time_sec++;
  server->time_sec %= 1000; //so that the integer will never overflow. 
                            //doesn't interfere with timeout calculations.
}

void server_ping_clients(server_t *server) {
// ADVANCED: Ping all clients in the server by broadcasting a ping.
  mesg_t msg = {
    .kind = BL_PING
  };
  server_broadcast(server, &msg);
}

void server_remove_disconnected(server_t *server, int disconnect_secs) {
// ADVANCED: Check all clients to see if they have contacted the
// server recently. Any client with a last_contact_time field equal to
// or greater than the parameter disconnect_secs should be
// removed. Broadcast that the client was disconnected to remaining
// clients.  Process clients from lowest to highest and take care of
// loop indexing as clients may be removed during the loop
// necessitating index adjustments.
  for (int i = 0; i < server->n_clients; i++) {
    client_t *cur = server_get_client(server, i);
    if (server->time_sec - cur->last_contact_time >= disconnect_secs) {
      mesg_t msg = {
        .kind = BL_DISCONNECTED
      };
      strncpy(msg.name,cur->name,MAXNAME);
      server_remove_client(server, i);
      server_broadcast(server, &msg);
      log_printf("client %d '%s' DISCONNECTED\n", i,msg.name);
      i--;
    }
  }
}

void server_write_who(server_t *server) {
// ADVANCED: Write the current set of clients logged into the server
// to the BEGINNING the log_fd. Ensure that the write is protected by
// locking the semaphore associated with the log file. Since it may
// take some time to complete this operation (acquire semaphore then
// write) it should likely be done in its own thread to preven the
// main server operations from stalling.  For threaded I/O, consider
// using the pwrite() function to write to a specific location in an
// open file descriptor which will not alter the position of log_fd so
// that appends continue to write to the end of the file.
  who_t who = {
    .n_clients = server->n_clients
  };
  for (int i = 0; i < server->n_clients; i++) {
    snprintf(who.names[i],MAXNAME+1,"%s",server_get_client(server, i)->name);
  }
  sem_wait(server->log_sem);
  int bytes = pwrite(server->log_fd, &who, sizeof(who_t), 0);
  check_fail(bytes != sizeof(who_t), 1, "a status logging error occured\n");
  sem_post(server->log_sem);
}

void server_log_message(server_t *server, mesg_t *mesg) {
// ADVANCED: Write the given message to the end of log file associated
// with the server.
  int bytes = write(server->log_fd, mesg, sizeof(mesg_t));
  check_fail(bytes != sizeof(mesg_t), 1, "a record logging error occured\n");
}