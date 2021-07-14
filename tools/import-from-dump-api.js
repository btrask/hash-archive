#!/usr/bin/env node
// Copyright 2021 Ben Trask
// MIT licensed

var net = require('net')
var http = require('http');
var JSONParse = require('jsonparse');
var hx = require('./hximport');


var local = net.createConnection("./import.sock");
var remote = new URL(process.argv[2]);
var start = parseInt(process.argv[3] || "1", 10);
var end = 1000000000000;
getNext();


function getNext() {

var parser = new JSONParse();

remote.pathname = '/api/dump/';
remote.search = '?start='+start+'&duration='+(end-start);
console.log('Connecting to '+remote.href);

var req = http.get(remote.href);
var res = null;
req.on('error', function(err) {
	getNext();
});
req.on('response', function(arg) {
	res = arg;
//	console.log(res.statusCode);
	if(200 != res.statusCode) throw new Error('Bad response status '+res.statusCode);
//	res.pipe(process.stdout);
	res.on('data', function(buf) {
//		process.stdout.write(buf);
		parser.write(buf);
	});
	res.on('end', function() {
		console.log('Dump ended?');
//		parser.end();
	});
});
req.end();

parser.onValue = function(val) {
	if(this.stack.length != 1) return;
	var obj = {
		time: val.timestamp,
		url: val.url,
		status: val.status,
		type: val.type,
		length: val.length,
		digests: {},
	};
	for(var i = 0; i < val.hashes.length; i++) {
		var hash = /^([^-]+)-(.*)$/.exec(val.hashes[i]);
		obj.digests[hash[1]] = Buffer.from(hash[2], 'base64');
	}
//	console.log(obj);
	var go = hx.write_response(local, obj);
	if(!go) {
		res.pause();
		local.once('drain', function() {
			res.resume();
		});
	}

	start = Math.max(start, val.timestamp);
};

}


