#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>


#define DEST_IP "127.0.0.1"
#define DEST_PORT 5000

int main(){
  int sockfd;
  struct sockaddr_in dest_addr;
  sockfd = socket(PF_INET, SOCK_STREAM, 0);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(DEST_PORT);
  dest_addr.sin_addr.s_addr = inet_addr(DEST_IP);
  memset(&(dest_addr.sin_zero),'\0',8);
  connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(struct sockaddr));

  char msg[32];
  int bytes_recv;
  bytes_recv = recv(sockfd, msg, 32, 0);

  printf("\n%s\nbytes received: %d", msg, bytes_recv);


  close(sockfd);
}
