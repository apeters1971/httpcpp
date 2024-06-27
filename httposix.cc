/* -------------------------------------------------------------------------- */
#include <iostream>
#include <unistd.h>
#include <future>
/* -------------------------------------------------------------------------- */
#include "httplib.h"
#include "httposix.hh"
/* -------------------------------------------------------------------------- */

#define BUFFER_SIZE 256*1024

int HttPosixFileStreamer::Open(const std::string host,
			       int port,
			       bool ssl,
			       const std::string path) {
  //  std::cerr << "http(get): " <<  host << ":" << port << " " << path << std::endl;

  int retc = socketpair(AF_UNIX, SOCK_STREAM, 0, pipefd);
  //  int retc = pipe(pipefd);
  
  ft = std::make_unique<std::future<int>>(std::async(std::launch::async, httpGet, host, port, ssl, path, pipefd[1], this));
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
    size_t toread = ((len-total_r)>BUFFER_SIZE)? BUFFER_SIZE:(len-total_r);
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
int HttPosixFileStreamer::httpGet(std::string host, int port, bool ssl, std::string path, int fd, HttPosixFileStreamer* streamer){

  std::string uri = (ssl?std::string("https://"):std::string("http://")) + host + std::string(":") + std::to_string(port);
  httplib::Client cli(uri);

  const char* b=0;
  // Use your CA bundle
  if ((b = getenv("HTTPCPP_CA_BUNDLE"))) {
    cli.set_ca_cert_path(std::string(b));
  }

  // Disable cert verification

  if ((b = getenv("HTTPCPP_NO_VERIFY")) && ((std::string(b) == "off") || (std::string(b) == "false") || (std::string(b) == "1"))) {
    cli.enable_server_certificate_verification(false);
  }
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
  if (!res) {
    auto resp = new httplib::Response();
    resp->status = (int)res.error();
    streamer->setResponse(*resp);
    streamer->NotifyHeader();
  }
  ::close(fd);
  return 0;
} 

/* -------------------------------------------------------------------------- */
void HttPosixFileStreamer::setResponse(const httplib::Response& resp) {
  response = std::make_shared<httplib::Response> (resp);
  size = response->get_header_value_u64("Content-Length");
}

