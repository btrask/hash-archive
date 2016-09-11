#!/usr/bin/env node
// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var fs = require("fs");
var net = require("net");
var pathm = require("path");
var zlib = require("zlib");

var csv = require("csv");

var mime = require("./mime");
var hximport = require("./hximport");

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
	var blocking = !hximport.write_response(sock, {
		time: time,
		url: url,
		status: 200,
		type: type,
		length: null,
		digests: digests,
	});
//	console.log(url);
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
var counter = 0, total = 0;

if(-1 === hximport.ALGOS.indexOf(algo)) {
	console.error("Invalid algorithm "+algo);
	process.exit(1);
}

file.pipe(gunzip).pipe(parser);
file.on("open", function(fd) {
	var stats = fs.fstatSync(fd);
	time = +stats.mtime/1000;
});

var stream = net.createConnection("./import.sock");
var interval = setInterval(function() {
	total += counter;
	console.log("\t"+counter+" per second, \t"+total+" total");
	counter = 0;
}, 1000*1);

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
		counter++;
		next();
	});
});
parser.on("end", function() {
	stream.end();
	clearInterval(interval);
});

})();

