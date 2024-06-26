

#include "httplib.h"
#include "httposix.hh"
#include "httprogress.hh"
#include "uri.hh"

// Function to display usage information
void displayUsage(const std::string& programName) {
  std::cerr << "Usage: " << programName << " <url> <local-out-path>" << std::endl;
}

int main(int argc, char* argv[]) {
  // Check if there is exactly one argument
  if (argc != 3) {
    displayUsage(argv[0]);
    return 1; // Return error code 1
  }
  
  // Extract the argument from command line
  std::string argument = argv[1];
  std::string outfile = argv[2];
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
      int fd = streamer->Open(geturi.get_host(), geturi.get_port(), (geturi.get_scheme() == "https"), geturi.get_path());
      streamer->WaitHeader();
      
      size_t total_r=0;
      size_t total_s=streamer->Size();
      size_t r=0;

      if (streamer->Response()->status != 200) {
	if (streamer->Response()->status < 200) {
	  // this comes from the connectino layer
	  std::cerr << "error: unable to get file from '" << argument << "' " << streamer->Response()->status << " : [ " << httplib::to_string( (httplib::Error) streamer->Response()->status) << " ]" <<std::endl;
	} else {
	  // this is an HTTP response

	}
	exit(-1);
      }

      int outfd = ::open(outfile.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);

      // Check if the file is opened successfully
      if (fd == -1) {
	std::cerr << "error: unable to open output file '" << outfile << "'" << std::endl;
	exit(-1);
      }

      if (0) {
	std::cerr<< "header" << std::endl;
	for (auto i:streamer->Response()->headers) {
	  std::cerr << i.first << ":" << i.second << std::endl;
	}
      }
      if (0) {
	std::cerr << "total size " << streamer->Size() << std::endl;
      }

      size_t bs = 256*1024;
      char * buffer = (char*) malloc(bs);

      do {
	r = streamer->Read(buffer, bs);
	//	std::cerr << "[read] " << r << std::endl;
	auto w = ::write(outfd, buffer, r);
	if ( r != w ) {
	  std::cerr << "error: couldn't write all data to target '" << outfile << "'" << std::endl;
	  exit(-1);
	}
	progress.take();
	progress.print(total_r, total_s);
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
