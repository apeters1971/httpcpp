#pragma once

#include <sys/time.h>
#include <stdarg.h>

class httprogress {
public:
  
  httprogress(const std::string& _prog, const std::string& _name) : prog(_prog), name(_name){}
  virtual ~httprogress() {}

  void
  cerr_print(const char* format, ...) {
    char cerr_buff[4096];
    va_list args;
    va_start(args, format);
    vsprintf(cerr_buff, format, args);
    va_end(args);
    std::cerr << cerr_buff;
  }

#define CERR(s) do {                            \
    cerr_print s;                               \
  } while (0)

  void start() {abs_last_time.tv_sec = abs_last_time.tv_usec = 0 ; gettimeofday(&abs_start_time, &tz);}
  void take()  {gettimeofday(&abs_stop_time, &tz);}
  void stop()  {CERR(("\n"));}
  void
  print(unsigned long long bytes, unsigned long long size)
  {
    if (!size) {
      bytes = size = 1; // fake 100% in that case      
    }

    
    float passed_time = ((float)((abs_stop_time.tv_sec - abs_last_time.tv_sec) * 1000
			      +
			      (abs_stop_time.tv_usec - abs_last_time.tv_usec) / 1000));
    if ( (bytes != size)  && (passed_time < 40)) {
      // we don't have to print to often
      return;
    }

    abs_last_time = abs_stop_time;
    
    CERR(("[%s] %-24s Total %.02f MB\t|", prog.c_str(), name.c_str(),
	  (float) size / 1024 / 1024));
    
    for (int l = 0; l < 20; l++) {
      if (l < ((int)(20.0 * bytes / size))) {
	CERR(("="));
      }
      
      if (l == ((int)(20.0 * bytes / size))) {
	CERR((">"));
      }
      
      if (l > ((int)(20.0 * bytes / size))) {
	CERR(("."));
      }
    }
    
    float abs_time = ((float)((abs_stop_time.tv_sec - abs_start_time.tv_sec) * 1000
			      +
			      (abs_stop_time.tv_usec - abs_start_time.tv_usec) / 1000));
    CERR(("| %.02f %% [%.01f MB/s] %.02f s\r", 100.0 * bytes / size,
	  bytes / abs_time / 1000.0, abs_time / 1000.0));

  }

private:
  std::string prog;
  std::string name;
  struct timeval abs_start_time;
  struct timeval abs_stop_time;
  struct timezone tz;
  struct timeval abs_last_time;
};
