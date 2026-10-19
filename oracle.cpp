#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <math.h>

#define MYPORT 5000
#define BACKLOG 2
#define WAIT_TIME 10

std::pair<std::string, int> decode_connect_msg(char buf[8]){
  std::pair<std::string,int> msg={"",0};
  for(int i=0;i<4;i++){
    int  a= buf[i];
    char tmp_buf[10];
    snprintf(tmp_buf,sizeof(tmp_buf),"%d",a);
    std::string tmp_string(tmp_buf);
    msg.first+=tmp_string;
    if(i!=3)msg.first+=".";
  }

  memcpy(&(msg.second),buf+4,sizeof(msg.second));
  return msg;
}

void encode_link_state(std::vector<std::pair<std::pair<char,std::string>,std::pair<int,int>>> msgs,char buf[339]){
    buf[0]=(char)(msgs.size());
    for(int i=0;i<msgs.size();i++){
      buf[1+9*i]=msgs[i].first.first;
      std::string ip_tmp=msgs[i].first.second;
      for(int k=0;k<4;k++){
        std::string tmp="";
        int j=0;
        while(ip_tmp[j]!='.' && j<ip_tmp.size()){
          tmp+=ip_tmp[j];
          j++;
        }
        buf[2+9*i+k]=(char)(std::stoi(tmp));
      }
      memcpy(buf+8+9*i,&(msgs[i].second.first),sizeof(int));
      memcpy(buf+12+9*i,&msgs[i].second.second,sizeof(int));//check here 
    }
}

std::vector<std::vector<int>> get_costs(char* file_name){
  std::ifstream fin(file_name);

  std::string line;
  int N = 0;
  int n;
  std::vector<std::vector<int>> costs;
  if(fin.is_open()){
    while(true){
      std::getline(fin, line);
      if(line.front() == '#' || line.empty())continue;
      else break;
    }
    std::stringstream ss(line);
    std::vector<int> temp;
    while(ss >> n){
      temp.push_back(n);
      N++;
    }
    N++;
    costs.push_back(temp);
    for(int i=0;i<N-2;i++){
      while(true){
        std::getline(fin, line);
        if(line.front() == '#' || line.empty())continue;
        else break;
      }
      std::stringstream ss(line);
      temp.clear();
      while(ss >> n){
        temp.push_back(n);
      }
      costs.push_back(temp);
    }
  }
  return costs;
}

void parse_file(std::ifstream& fin,std::vector<std::vector<int>>&costs,int&N){
  int n;
  std::string line;
  if(fin.is_open()){
    while(true){
      std::getline(fin, line);
      if(line.front() == '#' || line.empty())continue;
      else break;
    }
    std::stringstream ss(line);
    std::vector<int> temp;
    while(ss >> n){
      temp.push_back(n);
      N++;
    }
    N++;
    costs.push_back(temp);
    for(int i=0;i<N-2;i++){
      while(true){
        std::getline(fin, line);
        if(line.front() == '#' || line.empty())continue;
        else break;
      }
      std::stringstream ss(line);
      temp.clear();
      while(ss >> n){
        temp.push_back(n);
      }
      costs.push_back(temp);
    }
  }
}

int main(int argc, char* argv[]){

  if(argc != 2){
    std::cout << "bad bad";
    return -1;
  }

  std::ifstream fin(argv[1]);

  if(!fin){
    std::cout << "file bad";
    return -1;
  }

  int N = 0;
  std::vector<std::vector<int>> costs; //
  parse_file(fin,costs,N);

  int sockfd; //, new_fd;
  struct sockaddr_in my_addr;
  std::vector<sockaddr_in> vn_addrs(N);
  unsigned int sin_size;
  sockfd = socket(PF_INET, SOCK_STREAM, 0);

  std::vector<int> vn_fds(N,0);

  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(MYPORT);
  my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  memset(&(my_addr.sin_zero), '\0', 8);
  
  bind(sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr));
  listen(sockfd, 10);
  sin_size = sizeof(struct sockaddr_in);

  std::vector<std::pair<std::string, int>> vn_details(N); //{ip_address,udpport}

  for(int i=0;i<N;i++){
    vn_fds[i] = accept(sockfd, (struct sockaddr *)&(vn_addrs[i]), &sin_size);

    char connect_buf[8];
    int recieved_bytes=0;
    while(recieved_bytes<8){
      int just_recieved=recv(vn_fds[i], connect_buf+recieved_bytes, 8-recieved_bytes, 0);
      if(just_recieved<=0){
       std::cout<<"connection with "<<"A"+i<<" is closed or there is an error while recieving the connect message";
        break;
      }
      recieved_bytes+=just_recieved;
    }

    vn_details[i] = decode_connect_msg(connect_buf);
  }

  for(int i=0;i<N;i++){
    std::vector<std::pair<std::pair<char,std::string>,std::pair<int,int>>> msgs;
    msgs.push_back({{'A'+i,vn_details[i].first},{vn_details[i].second,0}});
    for(int j=0;j<costs[i].size();j++){
      msgs.push_back({{'A'+i+j+1,vn_details[i+j+1].first},{vn_details[i+j+1].second,costs[i][j]}});
    }
    char send_buffer[339];
    encode_link_state(msgs,send_buffer);
    send(vn_fds[i],send_buffer,339,0);
  }

  while(true){
    std::vector<std::vector<int>> tmp;
    int number_of_nodes=0;
    int N_ = 0;
    parse_file(fin,tmp,N_);
    if(tmp!=costs){
     std::vector<std::pair<std::pair<char,std::string>,std::pair<int,int>>> msgs;
      for(int i=0;i<N;i++){
        for(int j=0;j<costs[i].size();j++){
          if(tmp[i][j]!=costs[i][j]){
            msgs.push_back({{'A'+i+j+1,vn_details[i+j+1].first},{vn_details[i+j+1].second,tmp[i][j]}});
          }
        }
        char send_buffer[339];
        encode_link_state(msgs,send_buffer);
        send(vn_fds[i],send_buffer,339,0);
      }
      costs=tmp;
    }
    sleep(WAIT_TIME);
  }




  
  // char *msg = "hello world\0";
  // int len, bytes_sent;

  // len = strlen(msg);
  // // bytes_sent = send(new_fd, msg, len, 0);
  
  // printf("\nbytes sent: %d\n", bytes_sent);

  // // close(new_fd);
  // close(sockfd);
}