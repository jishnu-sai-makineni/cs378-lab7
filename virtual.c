#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>


// #define DEST_IP "127.0.0.1"
// #define DEST_PORT 5000
#define WAIT_TIME 10

int main(int argc, char* argv[]){

    if(argc != 5){
        printf("\nRequired 3 arguments: <on-ip-addr> <on-port-num> <own-udp-ip> <own-udp-port>\n");
        return -1;
    }

    char* dest_ip = argv[1];
    int dest_port = atoi(argv[2]);
    int own_ip = argv[3];
    int udp_port = atoi(argv[4]);



    int sockfd;
    struct sockaddr_in dest_addr;
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    dest_addr.sin_addr.s_addr = inet_addr(dest_ip);
    memset(&(dest_addr.sin_zero),'\0',8);
    int ret = connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(struct sockaddr));
    // printf("\nconnect returns: %d\n", ret);

    send(sockfd, encode_connect_message(own_ip, udp_port), 8, 0);

    char buf[8];
    int red = 0;
    while(1){
        sleep(WAIT_TIME);
        
        red = 0;
        while(red < sizeof(buf)){
            ssize_t n = recv(sockfd, buf + red, sizeof(buf) - red, 0);
            
            if(n > 0){
                red += n;
                continue;
            }
            if(n == 0){
                printf("\nconnection closed\n");
                return -1;
            }
            if(errno == EAGAIN || errno == EWOULDBLOCK){
                break;
            }
            printf("\nreached end of checks\n");
            return -1;
        }
        if(red < 8)continue;

        
        // ssize_t n = recv(sockfd,buf,sizeof(buf),MSG_PEEK|MSG_DONTWAIT);

    }

    


    char msg[32];
    memset(msg, '\0', 32);
    int bytes_recv;
    bytes_recv = recv(sockfd, msg, 32, 0);

    printf("\n%s\nbytes received: %d", msg, bytes_recv);



    close(sockfd);
}
