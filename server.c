#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>

#define MYPORT 5000
#define BACKLOG 2

int main(){
  int sockfd, new_fd;
  struct sockaddr_in my_addr;
  struct sockaddr_in their_addr;
  int sin_size;
  sockfd = socket(PF_INET, SOCK_STREAM, 0);

  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(MYPORT);
  my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  memset(&(my_addr.sin_zero), '\0', 8);
  bind(sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr));
  listen(sockfd, BACKLOG);

  sin_size = sizeof(struct sockaddr_in);
  new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
  
  char *msg = "hello world\0";
  int len, bytes_sent;

  len = strlen(msg);
  bytes_sent = send(new_fd, msg, len, 0);
  
  printf("\nbytes sent: %d\n", bytes_sent);

  close(new_fd);
  close(sockfd);
}

