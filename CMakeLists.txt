# CMakeLists.txt

cmake_minimum_required(VERSION 3.0)
project(httposix)

# Set source files
set(SOURCE_FILES httposix.cc httposix.hh httplib.h httplib.cc)

# Add shared library target
add_library(httposix SHARED ${SOURCE_FILES})

# Enable Openssl
add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT=1)

# Optionally, set any compiler flags or include directories
# target_compile_options(httposix PRIVATE ...)
# target_include_directories(httposix PRIVATE ...)

# Add executable target
add_executable(httpget httpget.cc)

# Link the executable to the library
target_link_libraries(httpget PRIVATE httposix ssl)

# Optionally, set any compiler flags or include directories
# target_compile_options(httposix PRIVATE ...)
# target_include_directories(httposix PRIVATE ...)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
