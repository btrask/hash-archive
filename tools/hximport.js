// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var hximport = exports;

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

function expect(obj, prop) {
	if(Object.prototype.hasOwnProperty.call(obj, prop)) throw new TypeError("Missing property "+prop);
	return obj[prop];
}

hximport.ALGOS = ALGOS;
hximport.write_response = function(sock, res) {
	var go = true;
	go = write_uint64(sock, expect(res, "time")) && go;
	go = write_string(sock, expect(res, "url")) && go;
	go = write_uint64(sock, expect(res, "status")+0xffff) && go;
	go = write_string(sock, expect(res, "type")) && go;
	go = write_uint64(sock, expect(res, "length")) && go;
	go = write_uint16(sock, ALGO_MAX) && go;
	for(var i = 0; i < ALGO_MAX; i++) {
		go = write_blob(sock, res.digests[ALGOS[i]]) && go;
	}
	return go;
}

