#!/usr/bin/env node
(function () {
  "use strict"

  var net = require("net"),
    fs = require('fs'),
    server,
    interval,
    socket_addr,
    timed_out = false;

  function on_connection(stream) {
    var out_file = -1;

    console.log("[Server] On Connection. [" + server.connections + "]");

    stream.pause();
    // Write stream to file once it is ready
    fs.open('./js-server-received.dat', 'w', function(err, fd) {
      if (err) {
        throw err;
      }
      out_file = fd;
      stream.resume();
    });

    stream.setTimeout(5000);
    // stream.setNoDelay();
    // stream.setKeepAlive();

    console.log("\t[Stream] readyState: " + stream.readyState);

    stream.on('connect', function () {
      timed_out = false;
      console.log("\t[Stream] On Connect");
    });

    stream.on('secure', function () {
      console.log("\t[Stream] On Secure");
    });

    stream.on('data', function (data) {
      timed_out = false;
      console.log("\t[Stream] On Data");
      stream.write(data);

      stream.pause();
      fs.write(out_file, data, 0, data.length, null, function (err, written) {
        if (err) {
          throw err;
        }
        stream.resume();
      });
    });

    stream.on('end', function () {
      fs.close(out_file);
      console.log("\t[Stream] On End (received FIN).\n\t\treadyState: " + stream.readyState);
    });

    stream.on('timeout', function () {
      console.log("\t[Stream] On Timeout");
      stream.end();
      console.log("\t[Stream] Closing (sent FIN).\n\t\treadyState: " + stream.readyState);
    });

    stream.on('drain', function () {
      console.log("\t[Stream] On Drain");
    });

    stream.on('error', function (err) {
      console.log("\t[Stream] On Error: " + err.message);
    });

    stream.on('close', function (had_error) {
      console.log("\t[Stream] On Close (file descriptor closed).\n\t\treadyState: " + stream.readyState);
      // 'closed', 'open', 'opening', 'readOnly', or 'writeOnly'
      if ('open' === stream.readyState) {
        stream.write("cause error");
      }
    });
  }

  function on_close() {
    console.log("[Server] closing; waiting for all streams to close");
    clearInterval(interval);
  }

  function check_for_timeout() {
    if (true === timed_out) {
      server.close();
    }
    timed_out = true;
  }

  server = net.createServer(on_connection);
  server.on('close', on_close);
  server.maxConnections = 100;

  socket_addr = '/tmp/libevnet-echo.' + process.getuid() + '.sock';
  if (isNaN(socket_addr)) {
    server.listen(socket_addr);
    console.log("[Server] listening on " + socket_addr);
  }
  else if (socket_addr < 65536) {
    server.listen(socket_addr, 'localhost');
    console.log("[Server] listening on port" + socket_addr);
  }

  //interval = setInterval(check_for_timeout, 5000);
}());
