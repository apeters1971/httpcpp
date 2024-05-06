#include "httplib.h"
#include "httposix.hh"
#include "httprogress.hh"
#include "uri.hh"

// Function to display usage information
void displayUsage(const std::string& programName) {
  std::cerr << "Usage: " << programName << " <url>" << std::endl;
}

int main(int argc, char* argv[]) {
  // Check if there is exactly one argument
  if (argc != 2) {
    displayUsage(argv[0]);
    return 1; // Return error code 1
  }
  
  // Extract the argument from command line
  std::string argument = argv[1];
  
  // Check the argument and perform corresponding action
  if (argument == "-h" || argument == "--help") {
    // Display usage information
        displayUsage(argv[0]);
  } else {
    try {
      uri geturi(argument);
      fprintf(stderr,"%s: host:%s port:%u path:%s\n",
	      argv[0],
	      geturi.get_host().c_str(),
	      geturi.get_port(),
	      geturi.get_path().c_str());

      httprogress progress(geturi.get_host(), geturi.get_basename());
      progress.start();
      std::unique_ptr<HttPosixFileStreamer> streamer(new HttPosixFileStreamer);
      int fd = streamer->Open(geturi.get_host(), geturi.get_port(), geturi.get_path());

      streamer->WaitHeader();
      
      size_t total_r=0;
      size_t total_s=streamer->Size();
      size_t r=0;
      
      if (0) {
	std::cerr<< "header" << std::endl;
	for (auto i:streamer->Response()->headers) {
	  std::cerr << i.first << ":" << i.second << std::endl;
	}
      }
      if (0) {
	std::cerr << "total size " << streamer->Size() << std::endl;
      }
      do {
	char buffer[PIPE_BUF];
	r = streamer->Read(buffer, sizeof(buffer));
	progress.take();
	progress.print(total_r, total_s);
	//	std::cerr << "[read] " << r << std::endl;
	total_r += (r>0)?r:0;
      } while (r>0);
      progress.stop();
    } catch (...) {
      std::cerr << "Invalid uri: " << argument << std::endl;
      return 1; // Return error code 1
    }
  }
  
  return 0; // Return success
}
