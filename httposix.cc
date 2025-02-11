/* -------------------------------------------------------------------------- */
#include <iostream>
#include <unistd.h>
#include <future>
/* -------------------------------------------------------------------------- */
#include "httplib.h"
#include "httposix.hh"
#include "uri.hh"
/* -------------------------------------------------------------------------- */

#define BUFFER_SIZE 100200

int HttPosixFileStreamer::Open(const std::string host,
			       int port,
			       bool ssl,
			       const std::string path,
			       const httplib::Headers& header) {
  //  std::cerr << "http(get): " <<  host << ":" << port << " " << path << std::endl;
  request_header = header;
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
int HttPosixFileStreamer::httpGet(const std::string& host, int port, bool ssl, const std::string& path, int fd, HttPosixFileStreamer* streamer){
  
  std::unique_ptr<httplib::Client> cli = HttPosix::Client(host, port, ssl);
  std::string body;
  const httplib::Headers& hd = streamer->request_header;

  cli->set_follow_location(true);
  auto res = cli->Get(
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

/* -------------------------------------------------------------------------- */
httplib::Error
HttPosix::Stat(const std::string host,
	       int port,
	       bool ssl,
	       const std::string path,
	       struct stat& buf,
	       const httplib::Headers& request_hd,
	       httplib::Headers& response_hd)
{
  auto cli = HttPosix::Client(host, port, ssl);
  auto res = cli->Head(path, request_hd);
  if (res ) {
    std::cerr << std::setw(24) << std::left << "Status" << ": " << res->status << std::endl;
    for (auto i:res->headers) {
      std::cerr << std::setw(24) << std::left << i.first << ": " << i.second  << std::endl;
    }
    response_hd = res->headers;
    return httplib::Error::Success;
  } else {
    return res.error();
  }
}

/* -------------------------------------------------------------------------- */
int
HttPosix::Mkdir(const std::string host,
		int port,
		bool ssl,
		const std::string path)
{
  return 0;
}

/* -------------------------------------------------------------------------- */
int
HttPosix::Delete(const std::string host,
		 int port,
		 bool ssl,
		 const std::string path)
{
  return 0;
}

/* -------------------------------------------------------------------------- */
std::unique_ptr<httplib::Client>
HttPosix::Client(const std::string host, int port, bool ssl) {
  std::string uri = (ssl?std::string("https://"):std::string("http://")) + host + std::string(":") + std::to_string(port);
  auto cli =  std::make_unique<httplib::Client>(uri);

  // Use your CA bundle
  const char* b=0;
  if ((b = getenv("HTTPCPP_CA_BUNDLE"))) {
    cli->set_ca_cert_path(std::string(b));
  }

  // Disable cert verification
  if ((b = getenv("HTTPCPP_NO_VERIFY")) && ((std::string(b) == "off") || (std::string(b) == "false") || (std::string(b) == "1"))) {
    cli->enable_server_certificate_verification(false);
  }
  return cli;
}

/* -------------------------------------------------------------------------- */
int HttPosixFile::Open(const std::string host,
		       int port,
		       bool ssl,
		       const std::string path,
		       const httplib::Headers& request_header)
{
  std::lock_guard<std::mutex> mutx(mtx);
  if (isopen) {
    return EINVAL;
  }
  location.host = host;
  location.port = port;
  location.ssl  = ssl;
  location.path = path;
  return _Open(host, port, ssl, path, request_header);
}

/* -------------------------------------------------------------------------- */
int HttPosixFile::_Open(const std::string host,
		       int port,
		       bool ssl,
		       const std::string path,
		       const httplib::Headers& request_header)
{

  if (debug) {
    std::cerr << "[debug] Open [" << host << ":" << port << "{" << (ssl?std::string("https"):std::string("http")) << "} ]" << std::endl;
  }
  this->request_header = request_header;
  auto cli = HttPosix::Client(host, port, ssl);
  res = cli->Get(path, request_header);
  
  if (res ) {
    if (debug) {
      std::cerr << "[debug]" << std::setw(24) << std::left << "Status" << ": " << res->status << std::endl;
      for (auto i:res->headers) {
	std::cerr << "[debug]" << std::setw(24) << std::left << i.first << ": " << i.second  << std::endl;
      }
    }
    response_header = res->headers;
    isopen = true;
    if (res->has_header("Location")) {
      auto loc = res->get_header_value("Location");
      location_validity = time(NULL) + 30;
      try {
	uri geturi(loc);
	redirection.host = geturi.get_host();
	redirection.port = geturi.get_port();
	redirection.ssl  = (geturi.get_scheme() == "https");
	redirection.path = geturi.get_pathcgi();
	if (debug) {
	  std::cerr << "[debug] redirection.host     : " << redirection.host << std::endl;
	  std::cerr << "[debug] redirection.port     : " << redirection.port << std::endl;
	  std::cerr << "[debug] redirection.ssl      : " << redirection.ssl  << std::endl;
	  std::cerr << "[debug] redirection.path     : " << redirection.path << std::endl;
	  std::cerr << "[debug] redirection.validity : " << location_validity << std::endl;
	}
      } catch (...) {
	// invalid URI received
	std::cerr << "[open]: received invalid redirection URL : '" << loc << "'" << std::endl;
	return -1;
      }
      isopen = true;
      return 0;
    } else {
      location_validity = 0;
      redirection = location;
      isopen = true;
      if (debug) {
	std::cerr << "[debug] redirection.host     : " << redirection.host << std::endl;
	std::cerr << "[debug] redirection.port     : " << redirection.port << std::endl;
	std::cerr << "[debug] redirection.ssl      : " << redirection.ssl  << std::endl;
	std::cerr << "[debug] redirection.path     : " << redirection.path << std::endl;
	std::cerr << "[debug] redirection.validity : unlimited" << std::endl;
      }
    }
  }
  return -1;
}


/* -------------------------------------------------------------------------- */
int HttPosixFile::Close()
{
  if (debug) {
    std::cerr << "[debug] Close [" << location.host << ":" << location.port << "{" << (location.ssl?std::string("https"):std::string("http")) <<  "} ]"<< std::endl;
  }
  std::lock_guard<std::mutex> mutx(mtx);
  return 0;
}

// Function to split a string by a delimiter
std::vector<std::string> split(const std::string& str, const std::string& delimiter) {
  std::vector<std::string> parts;
  size_t start = 0;
  size_t end = str.find(delimiter);
  while (end != std::string::npos) {
    parts.push_back(str.substr(start, end - start));
    start = end + delimiter.length();
    end = str.find(delimiter, start);
  }
  parts.push_back(str.substr(start, end));
  return parts;
}

// Function to parse the multipart/byteranges body
std::string parseMultipartByteranges(const std::string& body, const std::string& boundary) {
  std::string filtered_body;
  std::vector<std::string> parts = split(body, "--" + boundary);
  
  for (const auto& part : parts) {
    // Ignore the empty parts and the final boundary part
    if (part.empty() || part == "--") continue;
    // Find the header/body separator
    size_t header_end = part.find("\n\n");
    if (header_end != std::string::npos) {
      // Extract the body part after the headers
      std::string content = part.substr(header_end + 2);
      content.pop_back();
      filtered_body += content;
    }
  }
  
  return filtered_body;
}

// Function to print a hex dump of a string
void hexDump(const std::string& data) {
    const size_t bytes_per_line = 16;
    size_t data_length = data.size();

    for (size_t i = 0; i < data_length; i += bytes_per_line) {
        // Print offset
        std::cout << std::setw(8) << std::setfill('0') << std::hex << i << ": ";

        // Print hex values
        for (size_t j = 0; j < bytes_per_line; ++j) {
            if (i + j < data_length) {
                std::cout << std::setw(2) << std::setfill('0') << std::hex
                          << (static_cast<unsigned int>(data[i + j]) & 0xFF) << " ";
            } else {
                std::cout << "   ";
            }
        }

        // Print ASCII characters
        std::cout << " ";
        for (size_t j = 0; j < bytes_per_line; ++j) {
            if (i + j < data_length) {
                char ch = data[i + j];
                if (std::isprint(ch)) {
                    std::cout << ch;
                } else {
                    std::cout << ".";
                }
            } else {
                std::cout << " ";
            }
        }

        std::cout << std::endl;
    }
}

/* -------------------------------------------------------------------------- */
int HttPosixFile::ReadV(const httplib::Ranges& ranges, void* buffer)
{
  if (debug) {
    std::cerr << "[debug] ReadV [" << location.host << ":" << location.port << "{" << (location.ssl?std::string("https"):std::string("http")) << " } ] ranges:[" << ranges.size() << "]" <<std::endl;
  }
  std::lock_guard<std::mutex> mutx(mtx);
  auto rc = ReOpen();
  if (rc) {
    return rc;
  }
  httplib::Headers hd = request_header;

  // rewrite the range header
  auto range = hd.equal_range("Range");
  hd.erase(range.first, range.second);
  hd.insert(httplib::make_range_header(ranges));

  if (debug) {
    for (auto i:hd) {
      std::cerr << "[debug] " << std::setw(24) << std::left << i.first << ": " << i.second  << std::endl;
    }
  }
  
  auto cli = HttPosix::Client(redirection.host, redirection.port, redirection.ssl);

  //  std::cerr << "redirection host: " << redirection.host << redirection.port << ":" << redirection.path << std::endl;
  off_t off=0;
  /*
  auto res = cli->Get(
		      redirection.path,
		      request_header,
		      [&](const httplib::Response &resp) {
			if (0) {
			  std::cerr << "Response: " << resp.status << std::endl;
			}
			this->res.value() = resp;
			if (resp.status != httplib::StatusCode::PartialContent_206) {
			  return false;
			}
			return true; // return 'false' if you want to cancel the request.
		      },
		      [&](const char *data, size_t data_length) {
			std::cerr << "[write] " << data_length << " offset: " << off <<std::endl;
			memcpy((char*)buffer+off, data, data_length);
			std::cerr << "recv: " << data_length << std::endl;
			std::string s(data,data_length);
			hexDump(s);
					       
			off += data_length;
			return true; // return 'false' if you want to cancel the request.
		      });
  */
  auto res = cli->Get(redirection.path, hd);

  if (res) {
    std::string filtered_body;
    if (res->body.size()) {
      std::string boundaryKey = "boundary=";
      std::string boundary;
      std::string header = res->get_header_value("Content-Type");
      if (header.empty()) {
	return -1;
      }
      // Find the position of "boundary="
      size_t pos = header.find(boundaryKey);
      if (pos != std::string::npos) {
        // Extract the boundary value
        boundary = header.substr(pos + boundaryKey.length());
      } else {
	return -1;
      }
      filtered_body = parseMultipartByteranges(res->body, boundary);
      memcpy(buffer, filtered_body.c_str(), filtered_body.size());
    }
    // for ( auto i: res->headers ) {
    //  std::cerr << i.first << ":" << i.second << std::endl;
    // }
    if (debug ) {
      std::cerr << "[debug] returned body size:" << filtered_body.size() << std::endl;
    }
    return filtered_body.size();
  } else {
    return -1;
  }
  
}

/* -------------------------------------------------------------------------- */
int HttPosixFile::Read(char* buffer, off_t offset, size_t len)
{
  if (debug) {
    std::cerr << "[debug] Read [" << location.host << ":" << location.port << "{" << (location.ssl?std::string("https"):std::string("http")) << "} ] " << "{" << offset << "," << len << "}" << std::endl;
  }  
  std::lock_guard<std::mutex> mutx(mtx);
  int rc = ReOpen();
  if (rc) {
    return rc;
  }
  httplib::Headers hd = request_header;

  // rewrite the range header
  auto range = hd.equal_range("Range");
  hd.erase(range.first, range.second);
  std::string request = std::string("bytes=") + std::to_string(offset) + std::string("-") + std::to_string(offset+len);

  hd.insert({"Range", request});

  if (debug) {
    for (auto i:hd) {
      std::cerr << "[debug]" << std::setw(24) << std::left << i.first << ": " << i.second  << std::endl;
    }
  }
  auto cli = HttPosix::Client(redirection.host, redirection.port, redirection.ssl);

  if (debug) {
    std::cerr << "[debug] redirection host: " << redirection.host << redirection.port << ":" << redirection.path << std::endl;
  }
  off_t off=0;
  res = cli->Get(
		 redirection.path,
		 request_header,
		 [&](const httplib::Response &resp) {
		   if (0) {
		     std::cerr << "Response: " << resp.status << std::endl;
		   }
		   this->res.value() = resp;
		   if (resp.status != httplib::StatusCode::PartialContent_206) {
		     return false;
		   }
		   return true; // return 'false' if you want to cancel the request.
		 },
		 [&](const char *data, size_t data_length) {
		   // std::cerr << "[write] " << data_length << " offset: " << off <<std::endl;
		   memcpy((char*)buffer+off, data, data_length);
		   // std::cerr << "recv: " << data_length << std::endl;
		   off += data_length;
			return true; // return 'false' if you want to cancel the request.
		 });

  if (res) {
    return off;
  } else {
    return -1;
  }
}
