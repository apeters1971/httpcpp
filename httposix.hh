/* -------------------------------------------------------------------------- */
#include <iostream>
#include <unistd.h>
#include <future>
#include <atomic>
#include <mutex>
#include <condition_variable>
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
#pragma once
/* -------------------------------------------------------------------------- */
namespace httplib {
  class Response;
}

class HttPosixFileStreamer {
 public:
  int Open(const std::string host,
	   int port,
	   const std::string path);
  int Close();
  off_t Seek(off_t newoffset);
  int Read(char* buffer, size_t len);

  HttPosixFileStreamer() {
    pipefd[0]=-1;
    pipefd[1]=-1;
    location = 0;
    size = 0;
  }
  ~HttPosixFileStreamer() {
    Close();
  }

  void setResponse(const httplib::Response& resp);
  std::shared_ptr<httplib::Response> Response() { return response;}
  
  size_t Size() { return size;}

  void NotifyHeader() {
    {
      std::lock_guard<std::mutex> lock(mtx);
      ready = true;
    }
    cv.notify_one(); 
  }
  
  void WaitHeader() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return ready; }); // Wait until ready is true
  }
  
private:
  
  std::unique_ptr<std::future<int>> ft;
  int pipefd[2];
  off_t location;
  std::shared_ptr<httplib::Response> response;
  off_t offset;
  std::atomic<size_t> size;
  
  std::mutex mtx;
  std::condition_variable cv;
  bool ready;
  
  static int httpGet(std::string host, int port, std::string path, int fd, HttPosixFileStreamer* streamer );
};
