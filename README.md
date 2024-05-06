httpcpp
===========

A lightweight HTTP client using httplib (https://github.com/yhirose/cpp-httplib)

Currently it provides a fast GET file streamer implementation using asynchronous programming exporting a filedescriptor interface.

Build it
--------

mkdir build
cd build
cmake ../
make


httpget
-------

./httpget https://

library
-------

Example how to use the file streamer:

```c++
try {
    uri geturi("https://myserver.mydomain//bigfile);

    std::unique_ptr<HttPosixFileStreamer> streamer(new HttPosixFileStreamer);
    int fd = streamer->Open(geturi.get_host(), geturi.get_port(), geturi.get_path());

    // wait for the initial response header
    streamer->WaitHeader();

    // print the status
    std::cout<< "Status: << streamer->Response().status << std::endl;

    // get the size 
    size_t total_s=streamer->Size();

    // read something sequentially from the body
    char buffer[PIPE_BUF];
    auto r = streamer->Read(buffer, sizeof(buffer));
} catch (...) {
  std::cerr << "Invalid URI" << std::endl;
}
```

