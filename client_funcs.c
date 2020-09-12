#include "blather.h"


// ADDED for consistency between bl_client and bl_showlog
char *client_format_mesg(mesg_t *msg, char buf[MAXLINE+MAXNAME+8]) {
  switch(msg->kind) { //what sort of message did we recieve? print accordingly
    case BL_MESG: //a user (perhaps this one) entered a chat message
      snprintf(buf, MAXLINE+MAXNAME+8, "[%s] : %s\n", msg->name, msg->body);
    break;
    case BL_JOINED: //another user joined the chat
      snprintf(buf, MAXLINE+MAXNAME+8, "-- %s JOINED --\n", msg->name);
    break;
    case BL_DEPARTED: //another user left
      snprintf(buf, MAXLINE+MAXNAME+8, "-- %s DEPARTED --\n", msg->name);
    break;
    case BL_SHUTDOWN: //the server closed. time to exit
      snprintf(buf, MAXLINE+MAXNAME+8, "!!! server is shutting down !!!\n");
    break;
    case BL_DISCONNECTED: //another user was disconnected (ping timed out)
      snprintf(buf, MAXLINE+MAXNAME+8, "-- %s DISCONNECTED --\n", msg->name);
    break;
    default: 
      return NULL;
  }
  return buf;
}

//ADDED to determine whether a message is of the form '%last <num>'.
//Returns num, or 0 if the message does not match the pattern.
int client_parse_last(char *msg_body) {
  if (strncmp(msg_body, "%last ", 6) == 0) {
    return atoi(msg_body + 6);
  }
  return 0;
}

//ADDED to determine whether a message is of the form '%who'.
//Returns num, or 0 if the buf does not match the pattern.
int client_parse_who(char *buf) {
  if (strncmp(buf, "%who", 4) == 0) {
    return 1;
  }
  return 0;
}