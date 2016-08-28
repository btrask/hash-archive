#!/usr/bin/env node
// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var net = require("net");

function write_uint16(sock, val) {
	var buf = new Buffer(2);
	buf.writeUInt16BE(val, 0);
	return sock.write(buf);
}
function write_uint64(sock, val) {
	var buf;
	if(null === val) {
		buf = new Buffer([0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]);
	} else {
		buf = new Buffer(8);
		buf.writeUIntBE(val, 0, 8);
	}
	return sock.write(buf);
}
function write_string(sock, str) {
	var buf = new Buffer(str || "", "utf8");
	write_uint16(sock, buf.length);
	return sock.write(buf);
}
function write_blob(sock, buf) {
	if(!buf) {
		return write_uint16(sock, 0);
	}
	write_uint16(sock, buf.length);
	return sock.write(buf);
}
function write_response(sock, res) {
	var blocking = false;
	blocking = !write_uint64(sock, res.time) || blocking;
	blocking = !write_string(sock, res.url) || blocking;
	blocking = !write_uint64(sock, res.status+0xffff) || blocking;
	blocking = !write_string(sock, res.type) || blocking;
	blocking = !write_uint64(sock, res.length) || blocking;
	blocking = !write_uint16(sock, res.digests.length) || blocking;
	for(var i = 0; i < res.digests.length; i++) {
		blocking = !write_blob(sock, res.digests[i]) || blocking;
	}
	return !blocking;
}

var socket = net.createConnection("./import.sock");
write_response(socket, {
	time: +new Date/1000,
	url: "http://localhost:8000/",
	status: 200,
	type: "text/plain; charset=utf-8",
	length: 0,
	digests: [
		null,
		null,
		new Buffer([0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]),
		null,
		null,
		null,
		null,
	],
});
socket.end();

