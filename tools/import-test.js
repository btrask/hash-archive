#!/usr/bin/env node
// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var net = require("net");

var hximport = require("./hximport");

var socket = net.createConnection("./import.sock");
hximport.write_response(socket, {
	time: +new Date/1000,
	url: "http://localhost:8000/",
	status: 200,
	type: "text/plain; charset=utf-8",
	length: 0,
	digests: {
		"sha256": new Buffer([0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff]),
	},
});
socket.end();

