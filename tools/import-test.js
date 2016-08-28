#!/usr/bin/env node
// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var net = require("net");

function write_uint16(sock, val) {
	var buf = new Buffer(2);
	buf.writeUInt16BE(val, 0);
	sock.write(buf);
}
function write_uint64(sock, val) {
	var buf = new Buffer(8);
	buf.writeUIntBE(val, 0, 8);
	sock.write(buf);
}
function write_string(sock, str) {
	var buf = new Buffer(str, "utf8");
	write_uint16(sock, buf.length);
	sock.write(buf);
}
function write_blob(sock, buf) {
	write_uint16(sock, buf.length);
	sock.write(buf);
}

var socket = net.createConnection("./import.sock");
write_uint64(socket, +new Date/1000);
write_string(socket, "http://localhost:8000/");
write_uint64(socket, 200+0xffff);
write_string(socket, "text/plain; charset=utf-8");
write_uint64(socket, 0);
write_uint16(socket, 7);
write_blob(socket, new Buffer(0));
write_blob(socket, new Buffer(0));
write_blob(socket, new Buffer([0xff, 0xff, 0xff, 0xff]));
write_blob(socket, new Buffer(0));
write_blob(socket, new Buffer(0));
write_blob(socket, new Buffer(0));
write_blob(socket, new Buffer(0));
socket.end();

