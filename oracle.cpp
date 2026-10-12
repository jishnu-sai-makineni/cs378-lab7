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

#define MYPORT 5000
#define BACKLOG 2

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



  int sockfd, new_fd;
  struct sockaddr_in my_addr;
  struct sockaddr_in their_addr;
  unsigned int sin_size;
  sockfd = socket(PF_INET, SOCK_STREAM, 0);

  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(MYPORT);
  my_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  memset(&(my_addr.sin_zero), '\0', 8);
  bind(sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr));
  listen(sockfd, 10);

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

