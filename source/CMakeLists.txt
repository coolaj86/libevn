cmake_minimum_required(VERSION 2.6)
project(EVN)
include_directories(${EVN_SOURCE_DIR}/include)
#include_directories("${CMAKE_SOURCE_DIR}")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

add_library(buffer-list STATIC buffer-list.c)
add_library(inbuf STATIC inbuf.c)

add_library(evn SHARED evn.c evn.h buffer-list inbuf)
target_link_libraries(evn ev)

add_executable(bufferlist-test buffer-list-test.c buffer-list)
add_executable(inbuf-test inbuf-test.c inbuf)

add_executable(echo-server echo-server.c)
target_link_libraries(echo-server evn)

add_executable(echo-client echo-client.c)
target_link_libraries(echo-client evn)

INSTALL(TARGETS evn
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
