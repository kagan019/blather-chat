#include "blather.h"

int main(int argc, char **argv) {
  check_fail(argc < 2, 0, "usage: %s <filename>\n", argv[0]);

  int rdfd = open(argv[1], O_RDONLY);
  check_fail(rdfd == -1, 1, "couldn't open file");
  who_t who;
  int bytes = read(rdfd, &who, sizeof(who_t));
  check_fail(bytes != sizeof(who_t), 1, "a read error occurred. check that the file is legit\n");
  printf("%d CLIENTS\n", who.n_clients);
  for (int i = 0; i < who.n_clients; i++) {
    printf("%d: %s\n", i, who.names[i]);
  }
  printf("MESSAGES\n");
  mesg_t msg;
  char buf[MAXLINE+MAXNAME+8];
  while((bytes = read(rdfd, &msg, sizeof(mesg_t)))) {
    check_fail(bytes != sizeof(mesg_t), 1, "an unexpected read error occurred\n");
    printf("%s", client_format_mesg(&msg, buf));
  }
  close(rdfd);
}