"use strict";
const {listenerConfig, createControlServer} = require("../online_server/control_tls");
const config = listenerConfig({HOST: "127.0.0.1", TLS_CERT_FILE: process.argv[2], TLS_KEY_FILE: process.argv[3]});
const server = createControlServer(config, socket => {
  const chunks = [];
  let received = 0;
  socket.setNoDelay(true);
  socket.pause();
  setTimeout(() => socket.resume(), 300);
  socket.on("error", () => {});
  socket.on("data", data => {
    for (let i = 0; i < data.length; i++) {
      if (data[i] !== (((received + i) * 37 + 11) & 255)) throw new Error("corrupted application bytes");
    }
    chunks.push(data);
    received += data.length;
    if (received > 2 * 1024 * 1024) throw new Error("unexpected application bytes");
    if (received === 2 * 1024 * 1024) {
      socket.end(Buffer.concat(chunks));
      console.log("PAYLOAD PASS");
    }
  });
});
server.listen(0, "127.0.0.1", () => console.log(server.address().port));
