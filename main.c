#include "httplib.h"
#include <iostream>
#include <unistd.h>
#include <future>

int fileget(std::string host, int port, std::string path, int fd){
  httplib::Client cli(host, port);

  std::string body;

  httplib::Headers hd;
  cli.set_follow_location(true);

  auto res = cli.Get(
		     path,
		     hd,
		     [&](const httplib::Response &response) {
		       return true; // return 'false' if you want to cancel the request.
		     },
		     [&](const char *data, size_t data_length) {
		       //		       std::cerr << "[write] " << data_length << std::endl;
		       ::write(fd, data, data_length);
		       //		       std::cerr << "recv: " << data_length << std::endl;
		       return true; // return 'false' if you want to cancel the request.
		     });

  ::close(fd);
  return 0;
}

int main(int argc, char* arg[])
{
  if (argc != 4) {
    return -1;
  }
  std::string host = arg[1];
  int port = std::atoi(arg[2]);
  std::string path = arg[3];
  
  std::cerr << "http(get): " <<  host << ":" << port << " " << path << std::endl;

  int pipefd[2];
  int retc = pipe(pipefd);
  
  std::unique_ptr<std::future<int>> ft = std::make_unique<std::future<int>>(std::async(std::launch::async, fileget, host, port, path, pipefd[1]));

  size_t total_r=0;
  size_t r=0;
  do {
    char buffer[PIPE_BUF];
    r = ::read(pipefd[0], buffer, sizeof(buffer));
    //    std::cerr << "[read] " << r << std::endl;
    total_r += (r>0)?r:0;
  } while (r>0);

  ft->wait();
  std::cerr<< "[info]: read " << total_r << " bytes!" << std::endl;
}
