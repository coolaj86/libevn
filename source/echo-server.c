#include <stdio.h>
#include <ev.h>

#include "evn.h"

//
// Event Callbacks
//

static void on_drain(EV_P, struct evn_stream* stream) {
  puts("[Server] On (Stream) Drain");
  evn_stream_end(EV_A, stream);
}

// Data
static void on_data(EV_P_ struct evn_stream* stream, void* data, int size)
{
  bool all_sent;
  FILE* file;

  file = fopen("./echo-server-receive.dat", "wb");
  if (NULL == file)
  {
    perror("failed to open file to write received data");
  }
  else
  {
    fwrite(data, size, 1, file);
    fclose(file);
  }

  puts("[Server] On (Stream) Data");
  printf("\t received %d bytes, now resending to the client\n", size);
  all_sent = evn_stream_write(EV_A_ stream, data, size);

  if (false == all_sent)
  {
    puts("\t all or part of the data was queued in user memory");
    puts("\t 'drain' will be emitted when the buffer is again free, and we will then end the socket");
    stream->on_drain = on_drain;
  }
  else
  {
    puts("\t wrote all data in one shot.");
    evn_stream_end(EV_A_ stream);
  }
}

// End
static void on_stream_end(EV_P_ struct evn_stream* stream)
{
  puts("[Server] On (Stream) End");
}

// Close
static void on_stream_close(EV_P_ struct evn_stream* stream, bool had_error)
{
  //struct evn_server* server = stream->server;
  puts("[Server] On (Stream) Close");
  if (true == had_error)
  {
    fprintf(stderr, "\t[Server] Stream Close had an error\n");
  }

  //evn_server_close(EV_A_ server);
}

static void on_stream_timeout(EV_P, struct evn_stream* stream)
{
  puts("connection timed out, destroying stream");
  evn_stream_destroy(EV_A, stream);
}

// Connection
static void on_connection(EV_P_ struct evn_server* server, struct evn_stream* stream)
{
  puts("[Server] On Connection");
  stream->on_data = on_data;
  stream->on_end = on_stream_end;
  stream->on_close = on_stream_close;
  stream->on_timeout = on_stream_timeout;
  stream->oneshot = true;

  evn_stream_set_timeout(EV_A, stream, 4000);
}

//// Listen
//static void on_listen(EV_P_ struct evn_server* server)
//{
  //puts("[Server] On Listen");
//}

// Close
static void on_server_close(EV_P, struct evn_server* server)
{
  puts("[Server] on server close");
}

//
// Run Application
//
int main (int argc, char* argv[])
{
  EV_P = ev_default_loop(0);
  struct evn_server* server;

  // Create unix socket in non-blocking fashion
  char socket_address[256] = {};
  snprintf(socket_address, 256, "%s%i%s", "/tmp/libevnet-echo.", (int) getuid(), ".sock");
  printf("socket_address: %s\n", socket_address);
  unlink(socket_address);

  server = evn_server_create(EV_A_ on_connection);
  //server->on_listen = on_listen;
  server->on_close = on_server_close;
  evn_server_listen(server, 0, socket_address);

  ev_loop(EV_A_ 0);
  puts("[Server] No events left");
  return 0;
}
