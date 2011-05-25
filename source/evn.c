
#include <errno.h> // errno
#include <unistd.h> // close
#include <stdlib.h> // free
#include <string.h> // memcpy

#include "evn.h"
#include "crossnet.h"

#define EVN_MAX_RECV 4096

static int evn_priv_unix_serverfd_create(struct sockaddr_un* socket_un, char* sock_path);
static int evn_priv_tcp_serverfd_create(struct sockaddr_in* socket_in, int port, char* sock_path);
static void evn_server_priv_on_connection(EV_P_ ev_io *w, int revents);

static void evn_stream_priv_on_connect(EV_P_ ev_io *w, int revents);
static inline void evn_stream_priv_on_activity(EV_P_ ev_io *w, int revents);
static void evn_stream_timer_priv_cb(EV_P, ev_timer* w, int revents);
static void evn_stream_priv_on_readable(EV_P_ ev_io *w, int revents);
static void evn_stream_priv_on_writable(EV_P_ ev_io *w, int revents);
static bool evn_stream_priv_send(EV_P, struct evn_stream* stream, void* data, int size);

// Simply adds O_NONBLOCK to the file descriptor of choice
int evn_set_nonblock(int fd) {
  int flags;

  flags = fcntl(fd, F_GETFL);
  flags |= O_NONBLOCK;
  return fcntl(fd, F_SETFL, flags);
}

struct evn_server* evn_server_create(EV_P_ evn_server_on_connection* on_connection) {
  struct evn_server* server = calloc(1, sizeof(struct evn_server));
  server->EV_A = EV_A;
  server->socket = NULL;
  server->on_connection = on_connection;
  return server;
}

int evn_server_listen(struct evn_server* server, int port, char* address) {
  int max_queue = 1024;
  int fd;
  struct sockaddr_un* sock_unix;
  struct sockaddr_in* sock_inet;
  struct evn_exception error;
  int reuse = 1;
  // TODO array_init(&server->streams, 128);

  if (0 == port)
  {
    server->socket = malloc(sizeof(struct sockaddr_un));
    sock_unix = (struct sockaddr_un*) server->socket;
    evn_debug("%s\n", address);
    fd = evn_priv_unix_serverfd_create(sock_unix, address);
    server->socket_len = sizeof(sock_unix->sun_family) + strlen(sock_unix->sun_path) + 1;
  }
  else
  {
    server->socket = malloc(sizeof(struct sockaddr_in));
    sock_inet = (struct sockaddr_in*) server->socket;
    fd = evn_priv_tcp_serverfd_create(sock_inet, port, address);
    server->socket_len = sizeof(struct sockaddr);
  }

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

  // put this up here so that if we need to destroy the server on error, the fd will be available
  // in io so we can close it.
  ev_io_init(&server->io, evn_server_priv_on_connection, fd, EV_READ);

  if (-1 == fd)
  {
    if (server->on_error)
    {
      error.error_number = errno;
      snprintf(error.message, sizeof error.message, "[EVN] failed to open server file descriptor: %s", strerror(errno));
      server->on_error(server->EV_A, server, &error);
    }
    evn_server_destroy(server->EV_A, server);
    return -1;
  }


  if (-1 == bind(fd, (struct sockaddr*) server->socket, server->socket_len))
  {
    if (server->on_error)
    {
      error.error_number = errno;
      snprintf(error.message, sizeof error.message, "[EVN] failed to bind server: %s", strerror(errno));
      server->on_error(server->EV_A, server, &error);
    }
    evn_server_destroy(server->EV_A, server);
    return -1;
  }

  // TODO max_queue
  if (-1 == listen(fd, max_queue)) {
    if (server->on_error)
    {
      error.error_number = errno;
      snprintf(error.message, sizeof error.message, "[EVN] listen failed: %s", strerror(errno));
      server->on_error(server->EV_A, server, &error);
    }
    evn_server_destroy(server->EV_A, server);
    return -1;
  }

  ev_io_start(server->EV_A_ &server->io);

  return 0;
}

static int evn_priv_tcp_serverfd_create(struct sockaddr_in* socket_in, int port, char* address) {
  int fd;

  // int optval = 1
  // setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))

  // Setup a tcp socket listener.
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == fd) {
    return -1;
  }

  // Set it non-blocking
  if (-1 == evn_set_nonblock(fd)) {
    close(fd);
    return -1;
  }

  // Set it as a tcp socket
  socket_in->sin_family = AF_INET;
  socket_in->sin_addr.s_addr = inet_addr(address);
  socket_in->sin_port = htons(port);

  return fd;
}

static int evn_priv_unix_serverfd_create(struct sockaddr_un* socket_un, char* sock_path) {
  int fd;

  unlink(sock_path);

  // Setup a unix socket listener.
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (-1 == fd) {
    return -1;
  }

  // Set it non-blocking
  if (-1 == evn_set_nonblock(fd)) {
    close(fd);
    return -1;
  }

  // Set it as unix socket
  socket_un->sun_family = AF_UNIX;
  strcpy(socket_un->sun_path, sock_path);
  evn_debug("sock_path: %s\n", sock_path);
  evn_debug("socket_un->sun_path: %s\n", socket_un->sun_path);

  return fd;
}

static void evn_server_priv_on_connection(EV_P_ ev_io *w, int revents) {
  evn_debugs("new connection - EV_READ - server->io.fd has become readable");

  int stream_fd;
  struct evn_stream* stream;
  struct evn_exception error;

  // since ev_io is the first member,
  // watcher `w` has the address of the
  // start of the evn_server struct
  struct evn_server* server = (struct evn_server*) w;

  while (1)
  {
    stream_fd = accept(server->io.fd, NULL, NULL);
    if (stream_fd == -1)
    {
      if( errno != EAGAIN && errno != EWOULDBLOCK )
      {
        if (server->on_error)
        {
          error.error_number = errno;
          snprintf(error.message, sizeof error.message, "[EVN] accept failed for abnormal reason: %s", strerror(errno));
          server->on_error(server->EV_A, server, &error);
        }
        evn_server_destroy(EV_A, server);
        return;
      }
      break;
    }
    stream = evn_stream_create(EV_A, stream_fd);
    stream->server = server;
    stream->ready_state = evn_OPEN;
    if (server->on_connection) { server->on_connection(EV_A_ server, stream); }
    if (true == stream->oneshot)
    {
      // each buffer chunk should be at least 4096 and we'll start with 128 chunks
      // the size will grow if the actual data received is larger
      stream->bufferlist = evn_bufferlist_create(4096, 128);
    }
    //stream->index = array_push(&server->streams, stream);
    ev_io_start(EV_A_ &stream->io);
  }
  evn_debugs(".");
}

int evn_server_close(EV_P_ struct evn_server* server) {
  close(server->io.fd);
  ev_io_stop(server->EV_A_ &server->io);

  if (server->on_close) { server->on_close(server->EV_A_ server); }

  if (NULL != server->socket)
  {
    free(server->socket);
    server->socket = NULL;
  }
  free(server);
  return 0;
}

int evn_server_destroy(EV_P_ struct evn_server* server) {
  return evn_server_close(EV_A_ server);
}

// this function creates a stream struct that uses the specified file descriptor
// used both server side and client side
inline struct evn_stream* evn_stream_create(EV_P, int fd) {
  evn_debug("evn_stream_create");
  struct evn_stream* stream;

  stream = calloc(1, sizeof(struct evn_stream));
  stream->EV_A = EV_A;
  stream->_priv_out_buffer = evn_inbuf_create(EVN_MAX_RECV);
  stream->oneshot = false;
  stream->bufferlist = NULL;
  stream->socket = NULL;
  stream->server = NULL;
  stream->timer.stream = stream;
  stream->timer.timeout = 0;
  evn_set_nonblock(fd);
  ev_io_init(&stream->io, evn_stream_priv_on_activity, fd, EV_READ | EV_WRITE);

  evn_debugs(".");
  return stream;
}

inline struct evn_stream* evn_create_connection(EV_P_ int port, char* address) {
  if (0 == port )
  {
    return evn_create_connection_unix_stream(EV_A_ address);
  }
  return evn_create_connection_tcp_stream(EV_A_ port, address);
}

struct evn_stream* evn_create_connection_tcp_stream(EV_P_ int port, char* address) {
  int stream_fd;
  struct evn_stream* stream;
  struct sockaddr_in* socket_in;

  stream_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == stream_fd) {
    #ifdef DEBUG
    perror("[EVN] TCP socket connection");
    #endif
    return NULL;
  }
  stream = evn_stream_create(EV_A, stream_fd);
  stream->socket = malloc(sizeof(struct sockaddr_in));
  socket_in = (struct sockaddr_in*) stream->socket;

  stream->ready_state = evn_OPENING;

  ev_io_stop(EV_A_ &stream->io);
  ev_io_init(&stream->io, evn_stream_priv_on_connect, stream_fd, EV_WRITE);

  socket_in->sin_family = AF_INET;
  socket_in->sin_addr.s_addr = inet_addr(address);
  socket_in->sin_port = htons(port);
  stream->socket_len = sizeof(struct sockaddr);

  if (-1 == connect(stream_fd, (struct sockaddr*) stream->socket, stream->socket_len)) {
    #ifdef DEBUG
    fprintf(stderr, "[EVN] connect to %s: %s\n", address, strerror(errno));
    #endif
    evn_stream_destroy(EV_A_ stream);
    stream = NULL;
  }

  if (NULL != stream)
  {
    ev_io_start(EV_A_ &stream->io);
  }
  return stream;
}

struct evn_stream* evn_create_connection_unix_stream(EV_P_ char* sock_path) {
  int stream_fd;
  struct evn_stream* stream;
  struct sockaddr_un* sock;

  stream_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (-1 == stream_fd) {
    #ifdef DEBUG
    perror("[EVN] Unix Stream socket connection");
    #endif
    return NULL;
  }
  stream = evn_stream_create(EV_A, stream_fd);
  stream->socket = malloc(sizeof(struct sockaddr_un));
  sock = (struct sockaddr_un*) stream->socket;

  stream->ready_state = evn_OPENING;

  ev_io_stop(EV_A_ &stream->io);
  ev_io_init(&stream->io, evn_stream_priv_on_connect, stream_fd, EV_WRITE);

  sock->sun_family = AF_UNIX;
  strcpy(sock->sun_path, sock_path);
  stream->socket_len = strlen(sock->sun_path) + 1 + sizeof(sock->sun_family);

  if (-1 == connect(stream_fd, (struct sockaddr *) sock, stream->socket_len)) {
    #ifdef DEBUG
    fprintf(stderr, "[EVN] connect to %s: %s\n", sock_path, strerror(errno));
    #endif
    evn_stream_destroy(EV_A_ stream);
    stream = NULL;
  }

  if (NULL != stream)
  {
    ev_io_start(EV_A_ &stream->io);
  }
  return stream;
}

static void evn_stream_priv_on_connect(EV_P_ ev_io *w, int revents) {
  struct evn_stream* stream = (struct evn_stream*) w;
  evn_debugs("Stream Connect");

  //ev_cb_set (ev_TYPE *watcher, callback)
  //ev_io_set (&w, STDIN_FILENO, EV_READ);

  stream->ready_state = evn_OPEN;

  int fd = stream->io.fd;
  ev_io_stop(EV_A_ &stream->io);
  ev_io_init(&stream->io, evn_stream_priv_on_activity, fd, EV_READ | EV_WRITE);
  ev_io_start(EV_A_ &stream->io);
  //ev_cb_set(&stream->io, evn_stream_priv_on_activity);

  if (stream->on_connect) { stream->on_connect(EV_A_ stream); }
  if (true == stream->oneshot)
  {
    // each buffer chunk should be at least 4096 and we'll start with 128 chunks
    // the size will grow if the actual data received is larger
    stream->bufferlist = evn_bufferlist_create(4096, 128);
  }

  stream->timer.last_activity = ev_now(EV_A);
  if ( (false == stream->timer.active) && (0 != stream->timer.timeout) )
  {
    evn_stream_timer_priv_cb(EV_A, &(stream->timer.timer), EV_TIMEOUT);
    evn_debugs("reactivated expired timer");
  }
}

int evn_stream_get_timeout(EV_P, struct evn_stream* stream) {
  return (int)(stream->timer.timeout * 1000);
}

void evn_stream_set_timeout(EV_P, struct evn_stream* stream, int timeout) {

  ev_timer_stop (EV_A, &(stream->timer.timer));
  stream->timer.active = false;

  if(0 == timeout)
  {
    return;
  }

  stream->timer.timeout = (ev_tstamp)timeout / 1000;
  stream->timer.last_activity = ev_now(loop);

  ev_init (&(stream->timer.timer), evn_stream_timer_priv_cb);
  evn_stream_timer_priv_cb(EV_A, &(stream->timer.timer), EV_TIMEOUT);
}

static void evn_stream_timer_priv_cb(EV_P, ev_timer* w, int revents) {
  ev_tstamp timeout;
  ev_tstamp now = ev_now(EV_A);
  struct evn_stream_timer* stream_timer = (struct evn_stream_timer*) w;
  struct evn_stream* stream = stream_timer->stream;

  timeout = stream_timer->last_activity + stream_timer->timeout;

  if (timeout < now)
  {
    evn_debug(" stream timed out. Current time = %f, Last avtivity = %f, Timeout = %f\n", now, stream_timer->last_activity, stream_timer->timeout);
    if (stream->on_timeout) { stream->on_timeout(EV_A, stream); }
    ev_timer_stop (EV_A, &(stream->timer.timer));
    stream_timer->active = false;
  }
  else
  {
    w->repeat = timeout-now;
    ev_timer_again(EV_A, w);
    stream_timer->active = true;
    evn_debug("timer callback made at %f, but stream not timed out. callback with occur again in %f secs\n", now, w->repeat);
  }
}

bool evn_stream_write(EV_P, struct evn_stream* stream, void* data, int size) {

  if (evn_READ_ONLY == stream->ready_state)
  {
    if (stream->on_error)
    {
      struct evn_exception error;
      error.error_number = -1;
      snprintf(error.message, sizeof error.message, "[EVN] trying to write to a read only stream");
      stream->on_error(EV_A, stream, &error);
    }
    return false;
  }

  stream->timer.last_activity = ev_now(EV_A);
  evn_debug("user write reset timeout at time %f\n", stream->timer.last_activity);
  if ( (false == stream->timer.active) && (0 != stream->timer.timeout) )
  {
    evn_stream_timer_priv_cb(EV_A, &(stream->timer.timer), EV_TIMEOUT);
    evn_debugs("reactivated expired timer");
  }

  if (0 == stream->_priv_out_buffer->size)
  {
    // priv_send will send the data over the socket, and add the leftover data to priv_out_buffer if it doesn't send everything
    if (true == evn_stream_priv_send(EV_A, stream, data, size))
    {
      evn_debugs("All data was sent without queueing");
      return true;
    }
    evn_debugs("Some data was queued");
  }
  else
  {
    evn_debugs("ABQ data");
    // we aren't ready to send yet, so add the new data to the buffer to be sent when the socket is writable
    evn_inbuf_add(stream->_priv_out_buffer, data, size);
  }

  // Ensure that we listen for EV_WRITE
  if (!(stream->io.events & EV_WRITE))
  {
    // store the file descriptor to make sure we don't lose it when we stop the event.
    int fd = stream->io.fd;
    // bitwise or the events we are looking for with EV_WRITE to make sure EV_WRITE is inlcuded.
    int new_events = stream->io.events | EV_WRITE;
    ev_io_stop(EV_A_ &stream->io);
    ev_io_set(&stream->io, fd, new_events);
  }
  ev_io_start(EV_A_ &stream->io);

  return false;
}

static inline void evn_stream_priv_on_activity(EV_P_ ev_io *w, int revents) {
  struct evn_stream* stream = (struct evn_stream*) w;
  evn_debugs("Stream Activity");

  stream->timer.last_activity = ev_now(EV_A);
  evn_debug("socket file activity reset timeout at time %f\n", stream->timer.last_activity);
  if ( (false == stream->timer.active) && (0 != stream->timer.timeout) )
  {
    evn_stream_timer_priv_cb(EV_A, &(stream->timer.timer), EV_TIMEOUT);
    evn_debugs("reactivated expired timer");
  }

  if (revents & EV_READ)
  {
    evn_stream_priv_on_readable(EV_A, w, revents);
  }
  else if (revents & EV_WRITE)
  {
    evn_stream_priv_on_writable(EV_A, w, revents);
  }
  else
  {
    // Never Happens
    fprintf(stderr, "[evn] [ERR] ev_io received something other than EV_READ or EV_WRITE");
  }
}

// This callback is called when data is readable on the unix socket.
static void evn_stream_priv_on_readable(EV_P_ ev_io *w, int revents) {
  void* data;
  struct evn_exception error;
  int length;
  struct evn_stream* stream = (struct evn_stream*) w;
  char recv_data[EVN_MAX_RECV];

  evn_debugs("EV_READ - stream->io.fd");
  length = recv(stream->io.fd, &recv_data, EVN_MAX_RECV, 0);

  if (length < 0)
  {
    if (stream->on_error) {
      error.error_number = errno;
      snprintf(error.message, sizeof error.message, "[EVN] read failed, now destroying stream: %s", strerror(errno));
      stream->on_error(EV_A, stream, &error);
    }
    evn_stream_destroy(EV_A, stream);
    return;
  }
  else if (0 == length)
  {
    evn_debugs("received FIN");

    if (stream->oneshot)
    {
      evn_debugs("oneshot shot");
      // TODO put on stack char data[stream->bufferlist->used];
      if (stream->bufferlist->used)
      {
        if (stream->on_data)
        {
          evn_buffer* buffer = evn_bufferlist_concat(stream->bufferlist);
          stream->on_data(EV_A_ stream, buffer->data, buffer->used);
          free(buffer); // does not free buffer->data, that's up to the user
        }
      }
    }
    if (stream->on_end) { stream->on_end(EV_A_ stream); }

    // if we've already sent to FIN packet, nothing left but to close the stream entirely
    if (evn_READ_ONLY == stream->ready_state)
    {
      evn_debugs("destroying socket now that both ends are closed");
      stream->ready_state = evn_CLOSED;
      evn_stream_destroy(EV_A_ stream);
    }
    // otherwise leave the socket open so we can write to it, but stop listening for READ events.
    else
    {
      evn_debugs("settings the socket to write only mode");
      stream->ready_state = evn_WRITE_ONLY;

      // store the file descriptor to make sure we don't lose it when we stop the event.
      int fd = stream->io.fd;
      // bitwise and the events we are looking for with the bitwise not of EV_READ to remove it.
      int new_events = stream->io.events & ~EV_READ;

      // stop the event on the loop, change the settings, and restart it
      ev_io_stop(EV_A_ &stream->io);
      ev_io_set(&stream->io, fd, new_events);
      ev_io_start(EV_A_ &stream->io);
    }
  }
  else if (length > 0)
  {
    if (stream->oneshot)
    {
      evn_debug("adding to %d bytes to the buffer (current size = %d)\n", length, stream->bufferlist->size);
      // if (stream->on_progress) { stream->on_progress(EV_A_ stream, data, length); }
      // if time - stream->started_at > stream->max_wait; stream->timeout();
      // if buffer->used > stream->max_size; stream->timeout();
      evn_bufferlist_add(stream->bufferlist, &recv_data, length);
      return;
    }
    if (stream->on_data)
    {
      evn_debug("read %d from the socket\n", length);
      data = malloc(length);
      memcpy(data, &recv_data, length);
      stream->on_data(EV_A_ stream, data, length);
    }
  }
}

static void evn_stream_priv_on_writable(EV_P_ ev_io *w, int revents) {
  struct evn_stream* stream = (struct evn_stream*) w;

  evn_debugs("EV_WRITE");

  evn_stream_priv_send(EV_A, stream, NULL, 0);
  if (evn_CLOSED == stream->ready_state)
  {
    // we experienced a problem while writing, don't continue
    return;
  }

  // If the buffer is finally empty, send the `drain` event
  if (0 == stream->_priv_out_buffer->size)
  {
    // store the file descriptor to make sure we don't lose it when we stop the event.
    int fd = stream->io.fd;
    // bitwise and the events we are looking for with the bitwise not of EV_WRITE to remove it.
    int new_events = stream->io.events & ~EV_WRITE;

    // stop the event on the loop, change the settings, and restart it
    ev_io_stop(EV_A_ &stream->io);
    ev_io_set(&stream->io, fd, new_events);
    ev_io_start(EV_A_ &stream->io);

    evn_debugs("pre-drain");
    if (stream->on_drain) { stream->on_drain(EV_A_ stream); }

    return;
  }
  evn_debugs("post-null");
}

static bool evn_stream_priv_send(EV_P, struct evn_stream* stream, void* data, int size) {
  //const int MAX_SEND = 4096;
  struct evn_exception error;
  int sent;
  evn_inbuf* buf = stream->_priv_out_buffer;
  int buf_size = buf->size;

  evn_debugs("priv_send");
  if (0 != buf_size)
  {
    evn_debug("has buffer with %d bytes of data\n", buf_size);
    sent = send(stream->io.fd, buf->bottom, buf->size, MSG_DONTWAIT | EVN_NOSIGNAL);
    if (sent < 0)
    {
      if (stream->on_error) {
        error.error_number = errno;
        snprintf(error.message, sizeof error.message, "[EVN] send failed, now destroying stream: %s", strerror(errno));
        stream->on_error(EV_A, stream, &error);
      }
      evn_stream_destroy(EV_A, stream);
      return false;
    }
    evn_inbuf_toss(buf, sent);

    if (sent != buf_size)
    {
      evn_debug("buffer has %d bytes remaining after sending %d bytes\n", buf->size, sent);
      if (NULL != data && 0 != size) { evn_inbuf_add(buf, data, size); }
      return false;
    }

    if (NULL != data && 0 != size)
    {
      evn_debugs("and has more data");
      sent = send(stream->io.fd, data, size, MSG_DONTWAIT | EVN_NOSIGNAL);
      if (sent < 0)
      {
        if (stream->on_error) {
          error.error_number = errno;
          snprintf(error.message, sizeof error.message, "[EVN] send failed, now destroying stream: %s", strerror(errno));
          stream->on_error(EV_A, stream, &error);
        }
        evn_stream_destroy(EV_A, stream);
        return false;
      }
      if (sent != size) {
        evn_debugs("enqueued remaining data");
        evn_inbuf_add(buf, data + sent, size - sent);
        return false;
      }
    }
    return true;
  }

  if (NULL != data && 0 != size)
  {
    evn_debugs("doesn't have data in buffer, but does have data");
    sent = send(stream->io.fd, data, size, MSG_DONTWAIT | EVN_NOSIGNAL);
    if (sent < 0)
    {
      if (stream->on_error) {
        error.error_number = errno;
        snprintf(error.message, sizeof error.message, "[EVN] send failed, now destroying stream: %s", strerror(errno));
        stream->on_error(EV_A, stream, &error);
      }
      evn_stream_destroy(EV_A, stream);
      return false;
    }
    if (sent != size) {
      evn_debugs("could not send all of the data");
      evn_inbuf_add(buf, data + sent, size - sent);
      return false;
    }
    evn_debugs("sent all of the data");
  }

  return true;
}

bool evn_stream_end(EV_P_ struct evn_stream* stream) {

  if (evn_WRITE_ONLY == stream->ready_state)
  {
    stream->ready_state = evn_CLOSED;
    evn_stream_destroy(EV_A_ stream);
    return true;
  }

  stream->ready_state = evn_READ_ONLY;
  // this command closes the socket for writing and sends the FIN packet
  shutdown(stream->io.fd, SHUT_WR);
  return false;
}

bool evn_stream_destroy(EV_P_ struct evn_stream* stream) {
  bool result;

  // TODO delay freeing of server until streams have closed
  // or link loop directly to stream?
  result = close(stream->io.fd) ? true : false;
  result = (evn_CLOSED != stream->ready_state) ? true : result;
  ev_io_stop(EV_A_ &stream->io);
  stream->ready_state = evn_CLOSED;

  ev_timer_stop (EV_A, &(stream->timer.timer));

  evn_debugs("starting on_close callback");
  if (stream->on_close) { stream->on_close(EV_A_ stream, result); }
  evn_debugs("finished on_close callback");

  if (NULL != stream->_priv_out_buffer)
  {
    evn_inbuf_destroy(stream->_priv_out_buffer);
    stream->_priv_out_buffer = NULL;
  }
  else
  {
    fprintf(stderr, "[EVN] out buffer is null in stream about to be destroyed\n\tPossible double destroy in progress\n");
  }
  if (NULL != stream->bufferlist)
  {
    evn_bufferlist_destroy(stream->bufferlist);
    stream->bufferlist = NULL;
  }
  else if (true == stream->oneshot)
  {
    fprintf(stderr, "[EVN] bufferlist is null in stream about to be destroyed (with one shot = true)\n\tPossible double destroy in progress\n");
  }
  if (NULL != stream->socket)
  {
    free(stream->socket);
    stream->socket = NULL;
  }
  else if (NULL == stream->server)
  {
    fprintf(stderr, "[EVN] socket is null in stream about to be destroyed (client side)\n\tPossible double destroy in progress\n");
  }
  free(stream);

  return result;
}

