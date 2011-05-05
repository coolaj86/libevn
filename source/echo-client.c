#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "evn.h"

//
// Client Callbacks
//

static void* sent_data = NULL;
static int sent_data_len = 0;

// Secure
static void on_secure(EV_P_ struct evn_stream* stream) {
  puts("[Client] Stream On Secure");
}

// Data
static void on_data(EV_P_ struct evn_stream* stream, void* data, int size) {
  int i;
  char* msg = (char*) data;
  char* sent = (char*) sent_data;
  puts("[Client] Stream On Data");

  if (sent_data_len != size)
  {
    fprintf(stderr, "\t ERR - did not receive(%d) same data size as was sent(%d)\n", size, sent_data_len);
    return;
  }

  for (i = 0; i < size; i += 1)
  {
    if (*msg != *sent)
    {
      fprintf(stderr, "\t ERR - sent data and received data differ at byte %d\n", i);
      return;
    }
    msg += 1;
    sent += 1;
  }

  printf("\t received data matches what was sent\n");
}

// End
static void on_end(EV_P_ struct evn_stream* stream) {
  puts("[Client] Stream On End");

  //evn_stream_end(EV_A_ stream);
}

// Timeout
static void on_timeout(EV_P_ struct evn_stream* stream) {
  puts("[Client] Stream On Timeout");
}

// Drain
static void on_drain(EV_P_ struct evn_stream* stream) {
  puts("[Client] Stream On Drain");
  puts("\t now ending the stream");
  evn_stream_end(EV_A, stream);
}

// Error
static void on_error(EV_P_ struct evn_stream* stream, struct evn_exception* error) {
  puts("[Client] Stream On Error");
  fprintf(stderr, "\t %s\n", error->message);
}

// Close
static void on_close(EV_P_ struct evn_stream* stream, bool had_error) {
  puts("[Client] Stream On Close");
}

// Connect
static void on_connect(EV_P_ struct evn_stream* stream) {
  bool all_sent;

  puts("[Client] Stream On Connect");
  printf("\t sending %d bytes to the server\n", sent_data_len);
  all_sent = evn_stream_write(EV_A_ stream, sent_data, sent_data_len);

  if (false == all_sent)
  {
    stream->on_drain = on_drain;
    puts("\t all or part of the data was queued in user memory");
    puts("\t 'drain' will be emitted when the buffer is again free");
  }
  else
  {
    puts("\t wrote all data in one shot.");
    evn_stream_end(EV_A_ stream);
  }
}

//
// Run Application
//

int main (int argc, char* argv[]) {
  EV_P = EV_DEFAULT;
  char socket_address[256] = {};

  if (argc > 2) {
    fprintf(stderr, "too many arguments\n");
    return 1;
  }
  else if (1 == argc) {
    char* msg = "Hello World\n";
    sent_data_len = strlen(msg) + 1;
    sent_data = malloc(sent_data_len);
    strncpy(sent_data, msg, sent_data_len);
  }
  else if (2 == argc) {
    int fd;
    fd = open(argv[1], O_RDONLY);
    if (fd > 0) {
      struct stat sb;
      int err_check;

      err_check = fstat(fd, &sb);
      if(-1 == err_check)
      {
        fprintf(stderr, "failed to stat %s: %s", argv[1], strerror(errno));
        return 1;
      }
      sent_data_len = (int)sb.st_size;

      sent_data = (char*)calloc(1, sent_data_len + 100);
      err_check = (int)read(fd, sent_data, sent_data_len + 100); // try to read more just to see if it's there
      if (err_check != sent_data_len)
      {
        fprintf(stderr, "read %d bytes instead of the expected %d for %s: %s\n", err_check, sent_data_len, argv[1], strerror(errno));
        free(sent_data);
        close(fd);
        return 1;
      }

      close(fd);
    }
    else {
      sent_data_len = strlen(argv[1]) + 1;
      sent_data = malloc(sent_data_len);
      strncpy(sent_data, argv[1], sent_data_len);
    }
  }
  else {
    fprintf(stderr, "I don't understand how you got an argc of %d\n", argc);
    return 1;
  }

  snprintf(socket_address, 256, "%s%i%s", "/tmp/libevnet-echo.", (int) getuid(), ".sock");
  printf("socket_address: %s\n", socket_address);

  struct evn_stream* stream = evn_create_connection(EV_A_ 0, socket_address);

  if (stream) {
    stream->on_connect = on_connect;
    stream->on_secure = on_secure;
    stream->on_data = on_data;
    stream->oneshot = true;
    stream->on_end = on_end;
    stream->on_timeout = on_timeout;
    //stream->on_drain = on_drain;
    stream->on_error = on_error;
    stream->on_close = on_close;
  }

  ev_loop(EV_A_ 0);
  free(sent_data);
  puts("[Client] No events left");
  return 0;
}

