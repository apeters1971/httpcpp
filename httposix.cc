/* -------------------------------------------------------------------------- */
#include <iostream>
#include <unistd.h>
#include <future>
/* -------------------------------------------------------------------------- */
#include "httplib.h"
#include "httposix.hh"
/* -------------------------------------------------------------------------- */
int HttPosixFileStreamer::Open(const std::string host,
			       int port,
			       const std::string path) {
  std::cerr << "http(get): " <<  host << ":" << port << " " << path << std::endl;
  int retc = pipe(pipefd);
  
  ft = std::make_unique<std::future<int>>(std::async(std::launch::async, httpGet, host, port, path, pipefd[1], this));
  return pipefd[1];
}    

/* -------------------------------------------------------------------------- */
int HttPosixFileStreamer::Close() {
  ::close(pipefd[0]);
  ::close(pipefd[1]);
  ft->wait();
  return 0;
}

/* -------------------------------------------------------------------------- */
off_t HttPosixFileStreamer::Seek(off_t newoffset) {
  // allow only sequentail IO
  if (offset != newoffset) {
    errno = EINVAL;
    return -1;
  }
  return newoffset;
}

/* -------------------------------------------------------------------------- */
int HttPosixFileStreamer::Read(char* buffer, size_t len) {
  size_t total_r=0;
  size_t r=0;
  do {
    char buffer[PIPE_BUF];
    size_t toread = ((len-total_r)>PIPE_BUF)? PIPE_BUF:(len-total_r);
    //    std::cerr << "[toread] " << toread << std::endl;
    r = ::read(pipefd[0], buffer+total_r, toread);
    //    std::cerr << "[read] " << r << std::endl;
    total_r += (r>0)?r:0;
  } while ( (r>0) && (total_r<len));
  location += total_r;
  //  std::cerr<< "[info]: read " << total_r << " bytes!" << std::endl;
  return total_r;
}

/* -------------------------------------------------------------------------- */
int HttPosixFileStreamer::httpGet(std::string host, int port, std::string path, int fd, HttPosixFileStreamer* streamer){
  httplib::Client cli(host, port);
  std::string body;
  
  httplib::Headers hd;
  cli.set_follow_location(true);
  
  auto res = cli.Get(
		     path,
		     hd,
		     [&](const httplib::Response &resp) {
		       if (0) {
			 std::cerr << "Response: " << resp.status << std::endl;
		       }
		       streamer->setResponse(resp);
		       streamer->NotifyHeader();
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

/* -------------------------------------------------------------------------- */
void HttPosixFileStreamer::setResponse(const httplib::Response& resp) {
  response = std::make_shared<httplib::Response> (resp);
  size = response->get_header_value_u64("Content-Length");
}

