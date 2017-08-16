use Spawn;

config const spawnClient = true;
if spawnClient then
  begin {
    var client = spawn(["python", "client.py"]);
    client.communicate();
  }

use ZMQ;

var context: Context;
var socket = context.socket(ZMQ.REP);
socket.bind("tcp://*:5555");

for i in 0..#10 {
  var msg = socket.recv(string);
  writeln("[Chapel] Received message: ", msg);
  socket.send("Hello %i from Chapel".format(i));
}
