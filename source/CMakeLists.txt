cmake_minimum_required(VERSION 2.6)
project(EVN C)
include_directories(${EVN_SOURCE_DIR}/include)
#include_directories("${CMAKE_SOURCE_DIR}")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
SET(CMAKE_C_FLAGS_DEBUG "-DDEBUG -g3 -ggdb3")

add_library(evn-buffer-list STATIC evn-buffer-list.c)
add_library(evn-inbuf STATIC evn-inbuf.c)

add_library(evn SHARED evn.c evn.h evn-buffer-list evn-inbuf)
target_link_libraries(evn ev)

add_executable(evn-bufferlist-test evn-buffer-list-test.c evn-buffer-list)
add_executable(evn-inbuf-test evn-inbuf-test.c evn-inbuf)

add_executable(echo-server echo-server.c)
target_link_libraries(echo-server evn)

add_executable(echo-client echo-client.c)
target_link_libraries(echo-client evn)

INSTALL(TARGETS evn
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
