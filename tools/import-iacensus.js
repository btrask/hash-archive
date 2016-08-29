#!/usr/bin/env node
// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var fs = require("fs");
var net = require("net");
var pathm = require("path");
var zlib = require("zlib");

var csv = require("csv");

var mime = require("./mime");

if(process.argv.length <= 3) {
	console.log("Usage: hash-archive-import-iacencus algo path");
	console.log("Warning: file's modification date is used for timestamp");
	console.log("For large imports, see tuning tips in source");
	process.exit(1);
}

log("Importer started");
process.on("exit", function() {
	log("Importer exiting");
});
process.on("uncaughtException", function(err) {
	log("Importer error");
	throw err;
});
function log(str) {
	console.log((new Date).toISOString()+": "+str);
}


var ALGOS = [];
ALGOS[0] = "md5";
ALGOS[1] = "sha1";
ALGOS[2] = "sha256";
ALGOS[3] = "sha384";
ALGOS[4] = "sha512";
ALGOS[5] = "blake2s";
ALGOS[6] = "blake2b";
var ALGO_MAX = 7;

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
	blocking = !write_uint16(sock, ALGO_MAX) || blocking;
	for(var i = 0; i < ALGO_MAX; i++) {
		blocking = !write_blob(sock, res.digests[ALGOS[i]]) || blocking;
	}
	return !blocking;
}

function write_response_row(sock, time, algo, row, cb) {
	var url = "https://archive.org/download/"+row[0]+"/"+row[1];
	var ext = pathm.extname(row[1]) || null;
//	if(ext && !has(mime, ext)) throw new Error("Unknown type of "+row[0]+"/"+row[1]);
	var type = mime[ext];
	var hash = new Buffer(row[2], "hex");
//	var sha1 = row[3] ? new Buffer(row[3], "hex") : null;
//	console.log(type, md5, sha1);

	var digests = {};
	digests[algo] = hash;
	var blocking = !write_response(sock, {
		time: time,
		url: url,
		status: 200,
		type: type,
		length: null,
		digests: digests,
	});
	console.log(url);
	if(!blocking) {
		process.nextTick(function() {
			cb(null);
		});
	} else {
		sock.once("drain", function() {
			cb(null);
		});
	}
}

(function() {

var algo = process.argv[2];
var path = pathm.resolve(process.argv[3]);
var file = fs.createReadStream(path);
var gunzip = new zlib.Gunzip();
var parser = csv.parse({ delimiter: "\t" });
var time = null;

if(-1 === ALGOS.indexOf(algo)) {
	console.error("Invalid algorithm "+algo);
	process.exit(1);
}

file.pipe(gunzip).pipe(parser);
file.on("open", function(fd) {
	var stats = fs.fstatSync(fd);
	time = +stats.mtime/1000;
});

var stream = net.createConnection("./import.sock");

// Fuck node-csv forever.
// parser.pause() is completely broken.
var wait = false;
parser.on("readable", function next() {
	if(wait) return;
	var row = parser.read();
	if(!row) return;
	wait = true;
	write_response_row(stream, time, algo, row, function(err) {
		if(err) throw err;
		wait = false;
		next();
	});
});
parser.on("end", function() {
	stream.end();
});

})();

