#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>


// #define DEST_IP "127.0.0.1"
// #define DEST_PORT 5000
#define WAIT_TIME 10
#define MAX_NODES 26
#define CONNECT_MSG_SIZE 8
#define LS_COUNT_SIZE 1
#define LS_TUPLE_SIZE 13
#define LS_MAX_SIZE (LS_COUNT_SIZE + MAX_NODES * LS_TUPLE_SIZE) //1+26*13
#define LSP_HEADER_SIZE 9 //origin(1) , sequence(4), number of links(4)
#define LSP_LINK_SIZE 5
#define LSP_MAX_SIZE (LSP_HEADER_SIZE+MAX_NODES*LSP_LINK_SIZE)
#define LSP_PERIOD 15
#define GRAPH_PERIOD 20

typedef struct{
    char id;
    struct in_addr ip;
    int port;
    int cost;
}Entry;

typedef struct{
    int count;
    Entry entries[MAX_NODES];
}LinkState;

typedef struct{
    int known;
    int seq;
    int count;
    char neighbor[MAX_NODES];
    int cost[MAX_NODES];
}NodeLS;

int recv_all(int fd,char* buf,int len){
    int done=0;
    while(done<len){
        int n=recv(fd,buf+done,len-done,0);
        if(n==0){
            return 0;
        }else if (n<0){
            if(errno == EINTR){
                continue;
            }
            return -1;
        }
        done+=n;
    }
    return 1;
}

int send_all(int fd,char *buf,int len){
    int done=0;
    while(done<len){
        int n=send(fd,buf+done,len-done,0);
        if(n<0){
            if(errno == EINTR){
                continue;
            }
            return -1;
        }
        done+=n;
    }
    return 1;
}

int encode_connect(char *ip,int port,char out[CONNECT_MSG_SIZE]){
    struct in_addr addr;
    if(inet_pton(AF_INET,ip,&addr)!=1)return -1;
    memcpy(out,&addr.s_addr,4);
    memcpy(out+4,&port,4);
    return 0;
}

int decode_link_state(int fd,LinkState *state){
    unsigned char buf[LS_MAX_SIZE];
    int r =recv_all(fd,buf,1);
    if(r<=0)return r;
    int count =buf[0];
    if(count==0 || count>MAX_NODES)return -1;
    r=recv_all(fd,buf+1,count*LS_TUPLE_SIZE);
    if(r<=0)return r;
    state->count=count;
    for(int i=0;i<count;i++){
        int offset=1+i*LS_TUPLE_SIZE;
        int p,c;
        state->entries[i].id = (char)buf[offset];
        memcpy(&state->entries[i].ip.s_addr,buf+offset+1,4);
        memcpy(&p,buf+offset+5,4);
        memcpy(&c,buf+offset+9,4);
        state->entries[i].port = p;
        state->entries[i].cost = c;
    }
    return 1;
}

void update_local_graph(NodeLS graph[MAX_NODES],char self,LinkState *state){
    int me=self-'A';
    graph[me].known =1;
    graph[me].count=0;
    for(int i=0;i<state->count;i++){
        Entry *e=&state->entries[i];
        if(e->id == self)continue;
        int k= graph[me].count++;
        graph[me].neighbor[k]=e->id;
        graph[me].cost[k]=e->cost;
    }
}

int make_lsp(char self,int seq,NodeLS *local,unsigned char *buf){
    int origin = self-'A';
    buf[0]=(unsigned char) self;
    int seq_wire = seq;
    memcpy(buf+1,&seq_wire,4);
    int c_pass=local->count;
    memcpy(buf+5,&c_pass,4);
    int offset=9;
    for(int i=0;i<local->count;i++){
        buf[offset++]=(unsigned char)local->neighbor[i];
        int c = local->cost[i];
        memcpy(buf+offset,&c,4);
        offset+=4;
    }
    return offset;
}

int decode_lsp(unsigned char *buf, int len,char *origin,int*seq,NodeLS *out){
    *origin = (char) buf[0];
    memcpy(seq,buf+1,4);
    int count;
    memcpy(&count,buf+5,4);
    out->known=1;
    out->seq =*seq;
    out->count=count;
    int off = 9;
    for(int i=0;i<count;i++){
        char id=(char)buf[off++];
        int cost;
        memcpy(&cost,buf+off,4);
        off+=4;
        out->neighbor[i]=id;
        out->cost[i]=cost;
    }
    return 0;
}

static int same_endpoint(const struct sockaddr_in *a, const Entry *e) {
    return a->sin_addr.s_addr == e->ip.s_addr && ntohs(a->sin_port) == e->port;
}

static void flood_lsp(int udp, const LinkState *local_state, char self,
                      const unsigned char *packet, size_t packet_len,
                      const struct sockaddr_in *exclude) {
    for (int i = 0; i < local_state->count; ++i) {
        const Entry *e = &local_state->entries[i];
        if (e->id == self || e->cost == 0) continue;
        struct sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_addr = e->ip;
        to.sin_port = htons(e->port);
        if (exclude && same_endpoint(exclude, e)) continue;
        ssize_t n = sendto(udp, packet, packet_len, 0,
                           (struct sockaddr *)&to, sizeof(to));
        if (n < 0) perror("sendto LSP");
    }
}

void print_graph(NodeLS graph[MAX_NODES]){
    int highest =-1;
    for(int i=0;i<MAX_NODES;i++){
        if(graph[i].known==1){
            highest=i;
        }
    }
    if(highest<0)return;
    printf("\nknown link-state graph (unknown = ? ; no link =-1):\n");
    printf("   ");
    for(int j=0;j<=highest;j++){
        printf("%c ",'A'+j);
    }
    putchar('\n');
    for(int i=0;i<=highest;i++){
        printf("%c: ",'A'+i);
        for(int j=0;j<=highest;j++){
            if(i==j){
                printf("0 ");
                continue;
            }
            if(!graph[i].known){
                printf("? ");
                continue;
            }
            int found=0;
            for(int k=0;k<graph[i].count;k++){
                if(graph[i].neighbor[k]=='A'+j){
                    printf("%d ",graph[i].cost[k]);
                    found=1;
                    break;
                }
            }
            if(!found){
                printf("-1 ");
            }
        }
        putchar('\n');
    }
    fflush(stdout);
}

int main(int argc, char* argv[]){

    if(argc != 5){
        printf("\nRequired 3 arguments: <on-ip-addr> <on-port-num> <own-udp-ip> <own-udp-port>\n");
        return -1;
    }

    char* dest_ip = argv[1];
    int dest_port = atoi(argv[2]);
    char* own_ip = argv[3];
    int own_port = atoi(argv[4]);

    int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0) { perror("UDP socket"); return 1; }
    struct sockaddr_in own_addr;
    memset(&own_addr, 0, sizeof(own_addr));
    own_addr.sin_family = AF_INET; own_addr.sin_port = htons(own_port);
    /* Listen on every local interface; argv[3] is still the IP we advertise to peers. */
    own_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(udp_fd, (struct sockaddr *)&own_addr, sizeof(own_addr)) < 0) {
        perror("UDP bind"); close(udp_fd); return 1;
    }


    int sockfd;
    struct sockaddr_in dest_addr;
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    dest_addr.sin_addr.s_addr = inet_addr(dest_ip);
    memset(&(dest_addr.sin_zero),'\0',8);
    int ret = connect(sockfd, (struct sockaddr*)&dest_addr, sizeof(struct sockaddr));
    // printf("\nconnect returns: %d\n", ret);
    char connect_msg[8];
    encode_connect(own_ip,own_port,connect_msg);
    send_all(sockfd,connect_msg, 8);
    printf("Connected to Oracle; UDP endpoint %s:%d\n",own_ip,own_port);
    
    LinkState local_state;
    memset(&local_state,0,sizeof(local_state));
    NodeLS graph[MAX_NODES];
    memset(graph,0,sizeof(graph));
    char self =0;
    int local_seq=0;
    time_t last_lsp =0,last_print=0;
    int have_state=0;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds); FD_SET(sockfd, &rfds); FD_SET(udp_fd, &rfds);
        int maxfd = sockfd > udp_fd ? sockfd : udp_fd;
        struct timeval tv = {1, 0};
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }
        time_t now = time(NULL);

        if (FD_ISSET(sockfd, &rfds)) {
            LinkState next;
            int rc = decode_link_state(sockfd, &next);
            if (rc <= 0) {
                fprintf(stderr, "Oracle TCP connection closed or malformed LINK-STATE\n"); break;
            }
            char new_self = 0;
            for (int i = 0; i < next.count; ++i)
                if (next.entries[i].cost == 0) {
                    if (new_self != 0) { fprintf(stderr, "Multiple zero-cost entries in LINK-STATE\n"); new_self = 0; break; }
                    new_self = next.entries[i].id;
                }
            if (!new_self) { fprintf(stderr, "LINK-STATE has no unique self entry\n"); continue; }
            int changed = !have_state || new_self != self || next.count != local_state.count;
            if (!changed) {
                for (int i = 0; i < next.count; ++i) {
                    if (next.entries[i].id != local_state.entries[i].id ||
                        next.entries[i].ip.s_addr != local_state.entries[i].ip.s_addr ||
                        next.entries[i].port != local_state.entries[i].port ||
                        next.entries[i].cost != local_state.entries[i].cost) { changed = 1; break; }
                }
            }
            local_state = next; self = new_self; have_state = 1;
            update_local_graph(graph, self, &local_state);
            printf("Neighbourhood: ");
            for (int i = 0; i < local_state.count; ++i) {
                if (i) printf(",");
                printf("%c=%u", local_state.entries[i].id, (unsigned)local_state.entries[i].cost);
            }
            putchar('\n'); fflush(stdout);
            if (changed) last_lsp = 0; /* originate immediately after topology changes */
        }

        if (FD_ISSET(udp_fd, &rfds)) {
            unsigned char packet[LSP_MAX_SIZE];
            struct sockaddr_in from; socklen_t from_len = sizeof(from);
            ssize_t n = recvfrom(udp_fd, packet, sizeof(packet), 0,
                                 (struct sockaddr *)&from, &from_len);
            if (n >= 0 && have_state) {
                char origin; int seq; NodeLS decoded;
                memset(&decoded, 0, sizeof(decoded));
                if (decode_lsp(packet, (size_t)n, &origin, &seq, &decoded) == 0) {
                    int idx = origin-'A';
                    if (idx >= 0 && origin != self && (!graph[idx].known || seq > graph[idx].seq)) {
                        graph[idx] = decoded;
                        printf("Learned LSP from %c, sequence %u\n", origin, seq); fflush(stdout);
                        /* Flood the accepted, newer LSP to all neighbours except its sender. */
                        flood_lsp(udp_fd, &local_state, self, packet, (size_t)n, &from);
                    }
                }
            }
        }

        if (have_state && (last_lsp == 0 || now - last_lsp >= LSP_PERIOD)) {
            unsigned char packet[LSP_MAX_SIZE];
            int len = make_lsp(self, ++local_seq, &graph[self-'A'], packet);
            if (len > 0) {
                flood_lsp(udp_fd, &local_state, self, packet, (size_t)len, NULL);
                /* Also record our latest self-originated LSP. */
                graph[self-'A'].known = 1;
                graph[self-'A'].seq = local_seq;
            }
            last_lsp = now;
        }
        if (have_state && (last_print == 0 || now - last_print >= GRAPH_PERIOD)) {
            print_graph(graph);
            last_print = now;
        }
    }
    close(sockfd); close(udp_fd); return 0;
}