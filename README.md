Daplie is Taking Back the Internet!
--------------

[![](https://daplie.github.com/igg/images/ad-developer-rpi-white-890x275.jpg?v2)](https://daplie.com/preorder/)

Stop serving the empire and join the rebel alliance!

* [Invest in Daplie on Wefunder](https://daplie.com/invest/)
* [Pre-order Cloud](https://daplie.com/preorder/), The World's First Home Server for Everyone

libevnet - Evented Networking in C
====

`libevnet` is essentially the `net` module of Node.JS (v0.2.6), implemented in `C`

Status: Release Candidate. Although there are a few missing features, `tigerbot` has implemented a simple webserver atop `libevn` and rumor is that it works well.

Build & Install
====

    make
    sudo make install

Examples
====

see `source/echo-client.c` and `source/echo-server.c`

API
====

net.Stream
----

  * `evn_stream* evn_create_connection(int port, char* address)` - create a Unix or TCP stream
    * If `port` is `0`, a **Unix Socket** is assumed. Otherwise a **TCP Socket** is assumed.
  * `struct evn_stream* evn_stream_create(int fd)`
  * `struct evn_stream* evn_create_connection(EV_P_ int port, char* address)`
  * `void evn_stream_priv_on_read(EV_P_ ev_io *w, int revents)`
  * `bool evn_stream_write(EV_P_ struct evn_stream* stream, void* data, int size)`
  * `bool evn_stream_end(EV_P_ struct evn_stream* stream)` - closes (and frees) the stream
  * `void evn_stream_pause(EV_P_ struct evn_stream* stream)` // TODO
  * `void evn_stream_resume(EV_P_ struct evn_stream* stream)` // TODO

**Event Callbacks**

  * `typedef void (evn_stream_on_connect)(EV_P_ struct evn_stream* stream)`
  * `typedef void (evn_stream_on_secure)(EV_P_ struct evn_stream* stream)` // TODO
  * `typedef void (evn_stream_on_data)(EV_P_ struct evn_stream* stream, void* blob, int size)`
  * `typedef void (evn_stream_on_end)(EV_P_ struct evn_stream* stream)`
  * `typedef void (evn_stream_on_timeout)(EV_P_ struct evn_stream* stream)`
  * `typedef void (evn_stream_on_drain)(EV_P_ struct evn_stream* stream)`
  * `typedef void (evn_stream_on_error)(EV_P_ struct evn_stream* stream, struct evn_exception* error)`
  * `typedef void (evn_stream_on_close)(EV_P_ struct evn_stream* stream, bool had_error)`


net.Server
----

  * `struct evn_server* evn_server_create(EV_P_ evn_server_on_connection* on_connection)`
  * `int evn_server_listen(struct evn_server* server, int port, char* address)` - create a Unix or TCP listener
    * If `port` is `0`, a **Unix Socket** is assumed. Otherwise a **TCP Socket** is assumed.
  * `int evn_server_close(EV_P_ struct evn_server* server)` -- closes (and frees) the server

**Event Callbacks**

  * `typedef void (evn_server_on_listen)(EV_P_ struct evn_server* server)`
  * `typedef void (evn_server_on_connection)(EV_P_ struct evn_server* server, struct evn_stream* stream)`
  * `typedef void (evn_server_on_close)(EV_P_ struct evn_server* server)`
