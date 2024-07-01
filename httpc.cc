

#include "httplib.h"
#include "httposix.hh"
#include "httprogress.hh"
#include "uri.hh"
#include <iostream>
#include <getopt.h>
#include <string>
#include <vector>
#include <algorithm>

// Function to display usage information
void display_usage(const std::string& programName) {
  std::cerr << "Usage: " << programName << " [-d] [-k] [-n] [--auth auth] [--cacert file] command [source] destination\n";
  std::cerr << "        - allowed commands: get, put, cp, head, mkdir, delete\n";
  std::cerr << "        - commands 'get', 'put' and 'cp' require a source argument.\n";
  std::cerr << "                    -k : don't verify server credentials.\n";
  std::cerr << "                    -n : disable progress bar.\n";
  std::cerr << "                    -d : enable debug.\n";
  std::cerr << "         --cacert file : crt file with full certifcate chain.\n";
  std::cerr << " -H \"header: contents\" : add an extra header to any request.\n";
}

int main(int argc, char* argv[]) {
  bool k_flag = false;
  bool n_flag = false;
  bool d_flag = false;
  std::string cacert_file;
  httplib::Headers extra_headers;
  std::string command, source, destination, pair;
  size_t pos = 0;
  const char *progname = argv[0];
  struct option long_options[] = {
    {"cacert", required_argument, nullptr, 'c'},
    {nullptr, 0, nullptr, 0}
  };
  int opt;
  int option_index = 0;
  while ((opt = getopt_long(argc, argv, "nkdc:H:", long_options, &option_index)) != -1) {
    switch (opt) {
    case 'k':
      k_flag = true;
      setenv("HTTCPP_NO_VERIFY","1", 1);
      break;
    case 'n':
      n_flag = true;
      break;
    case 'd':
      d_flag = true;
      break;
    case 'c':
      cacert_file = optarg;
      setenv("HTTCPP_CA_BUNDLE",cacert_file.c_str(), 1);
      break;
    case 'H':
      pair = optarg;
      pos = pair.find(':');
      if (pos != std::string::npos) {
	std::string key = pair.substr(0, pos);
	std::string value = pair.substr(pos + 1);
	// Trim any leading or trailing spaces (optional)
	key.erase(0, key.find_first_not_of(" \t\n\r\f\v"));
	key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
	value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
	value.erase(value.find_last_not_of(" \t\n\r\f\v") + 1);
	extra_headers.insert(std::make_pair(key,value));
      } else {
	std::cerr << "error: head definition not in the form key=value";
	display_usage(progname);
	return EXIT_FAILURE;
      }
      break;
    default:
      display_usage(progname);
      return EXIT_FAILURE;
    }
  }

  if (argc - optind < 2) {
    std::cerr << "Error: Missing required positional arguments.\n";
    display_usage(progname);
    return EXIT_FAILURE;
  }
  command = argv[optind];
  // List of allowed commands
  std::vector<std::string> allowed_commands = {"get", "put", "cp", "head", "mkdir", "delete"};

  // Check if the command is allowed
  if (std::find(allowed_commands.begin(), allowed_commands.end(), command) == allowed_commands.end()) {
    std::cerr << "Error: Invalid command '" << command << "'.\n";
    display_usage(progname);
    return EXIT_FAILURE;
  }
  // For commands 'get' and 'put', we need 'source' and 'destination'
  if (command == "get" || command == "put"  || command == "cp" ){
    if (argc - optind < 3) {
      std::cerr << "Error: Missing required positional arguments for command '" << command << "'.\n";
      display_usage(progname);
      return EXIT_FAILURE;
    }
    source = argv[optind + 1];
    destination = argv[optind + 2];
  } else { // For 'head', 'mkdir', and 'delete', we need only 'destination'
    destination = argv[optind + 1];
  }
  // Print parsed options and arguments
  if (d_flag) {
    std::cerr << "[debug] k_flag: " << (k_flag ? "true" : "false") << "\n";
    std::cerr << "[debug] n_flag: " << (k_flag ? "true" : "false") << "\n";
    std::cerr << "[debug] cacert_file: " << (cacert_file.empty() ? "not provided" : cacert_file) << "\n";
    std::cerr << "[debug] command: " << command << "\n";
    if (!source.empty()) {
      std::cerr << "[debug] source: " << source << "\n";
    }
    std::cerr << "[debug] destination: " << destination << "\n";
    for (auto i:extra_headers) {
      std::cerr << "[debug] header: " << std::setw(24) << std::left << i.first << ": " << i.second  << std::endl;
    }
  }
  // Extract the argument from command line
  if (command == "get") {
    httplib::Headers request_hd = extra_headers;
    const char* rangeheader = httplib::detail::get_header_value(request_hd, "Range");
    httplib::Ranges ranges;
    bool rangerequest = false;
    if (rangeheader && httplib::detail::parse_range_header(rangeheader, ranges)) {
      rangerequest = true;
    }
    try {
      uri geturi(source);
      if (rangerequest) {
	// return partial contents
	if (ranges.size() == 1) {
	  std::unique_ptr<HttPosixFile> file(new HttPosixFile);
	  int rc = file->Open(geturi.get_host(), geturi.get_port(), (geturi.get_scheme() == "https"), geturi.get_pathcgi(), request_hd);
	  if (rc) {
	    const httplib::Result& result = file->Result();
	    std::cerr << "error: unable to open file from '" << source << "' " << result.value().status << " : [ " << httplib::status_message(result.value().status) << " ]" <<std::endl;
	    return 1;
	  }
	  // single range request
	  auto buffer_size = ranges[0].second+1-ranges[0].first;
	  std::vector<char> buffer(buffer_size);
	  auto r = file->Read(&buffer[0], ranges[0].first, ranges[0].second-ranges[0].first);
	  if (r<0) {
	    const httplib::Result& result = file->Result();
	    std::cerr << "error: failed to read requested range from '" << source << "' " << result.value().status << " : [ " << httplib::status_message(result.value().status) << " ]" <<std::endl;
	    return 1;
	  }
	  file->Close();
	  return 0;
	} else {
	  // multi range request
	  std::unique_ptr<HttPosixFile> file(new HttPosixFile);
	  int rc = file->Open(geturi.get_host(), geturi.get_port(), (geturi.get_scheme() == "https"), geturi.get_pathcgi(), request_hd);
	  if (rc) {
	    const httplib::Result& result = file->Result();
	    std::cerr << "error: unable to open file from '" << source << "' " << result.value().status << " : [ " << httplib::status_message(result.value().status) << " ]" <<std::endl;
	    return 1;
	  }
	  auto buffer_size = 0;
	  for (auto i:ranges) {
	    buffer_size += i.second+1-i.first;
	  }
	  std::vector<char> buffer(buffer_size);
	  auto r = file->ReadV(ranges, &buffer[0]);
	  if (r<0) {
	    const httplib::Result& result = file->Result();
	    std::cerr << "error: failed to read requested ranges from '" << source << "' " << result.value().status << " : [ " << httplib::status_message(result.value().status) << " ]" <<std::endl;
	    return 1;
	  }
	  file->Close();
	  return 0;
	}
      } else {
	// stream a full file
	httprogress progress(geturi.get_host(), geturi.get_basename());
	if (!n_flag) {progress.start();}
	std::unique_ptr<HttPosixFileStreamer> streamer(new HttPosixFileStreamer);
	int fd = streamer->Open(geturi.get_host(), geturi.get_port(), (geturi.get_scheme() == "https"), geturi.get_pathcgi(), request_hd);
	streamer->WaitHeader();
	
	size_t total_r=0;
	size_t total_s=streamer->Size();
	size_t r=0;
	
	if (streamer->Response()->status != 200) {
	  if (streamer->Response()->status < 200) {
	    // this comes from the connectino layer
	    std::cerr << "error: unable to get file from '" << source << "' " << streamer->Response()->status << " : [ " << httplib::to_string( (httplib::Error) streamer->Response()->status) << " ]" <<std::endl;
	  } else {
	    // this is an HTTP response
	    std::cerr << "error: unable to get file from '" << source << "' " << streamer->Response()->status << " : [ " << streamer->Response()->reason << " ]" <<std::endl;
	  }
	  return 1;
	}
	
	int outfd = ::open(destination.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
	// Check if the file is opened successfully
	if (fd == -1) {
	  std::cerr << "error: unable to open output file '" << destination << "'" << std::endl;
	  return 1;
	}
	if (d_flag) {
	  std::cerr<< "[debug] header" << std::endl;
	  for (auto i:streamer->Response()->headers) {
	    std::cerr << "[debug]" << i.first << ":" << i.second << std::endl;
	  }
	}
	if (d_flag) {
	  std::cerr << "[debug]" << "total size " << streamer->Size() << std::endl;
	}
	size_t bs = 256*1024;
	std::vector<char> buffer(bs);
	
	do {
	  r = streamer->Read(&buffer[0], bs);
	  if (d_flag) {
	    std::cerr << "[debug] [read] " << r << std::endl;
	  }
	  auto w = ::write(outfd, &buffer[0], r);
	  if ( r != w ) {
	    std::cerr << "error: couldn't write all data to target '" << destination << "'" << std::endl;
	    exit(-1);
	  }
	  if (!n_flag) {
	    progress.take();
	    progress.print(total_r, total_s);
	  }
	  total_r += (r>0)?r:0;
	} while (r>0);
	if (!n_flag) {progress.stop();}
      }
    } catch (...) {
      std::cerr << "Invalid uri: " << source << std::endl;
      return 1; // Return error code 1
    }
  } else if ( command == "put" ) {
    std::cerr << "error: not implemented" << std::endl;
    return 1;
  } else if ( command == "head" ) {
    uri headuri(destination);
    httplib::Headers request_hd = extra_headers;
    httplib::Headers response_hd;
    struct stat buf;
    auto result = HttPosix::Stat(headuri.get_host(), headuri.get_port(), (headuri.get_scheme() == "https"), headuri.get_pathcgi(), buf, request_hd, response_hd);
    return 0;
  } else if ( command == "cp" ) {
    std::cerr << "error: not implemented" << std::endl;
    return 1;
  } else if ( command == "delete") {
    std::cerr << "error: not implemented" << std::endl;
    return 1;
  } else if ( command == "head") {
    std::cerr << "error: not implemented" << std::endl;
    return 1;
  } else if ( command == "mkdir") {
    std::cerr << "error: not implemented" << std::endl;
    return 1;
  } else {
    std::cerr << "error: not implemented" << std::endl;
    return 1;
  }
  return 0;
}
