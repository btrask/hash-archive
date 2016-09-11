#!/usr/bin/env node
// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

var net = require("net");

var sqlite = require("sqlite3");

var hximport = require("./hximport");

var socket = net.createConnection("./import.sock");
var db = new sqlite.Database(process.argv[2], sqlite.OPEN_READWRITE);

function next(cid, cb) {
	db.get(
		"SELECT req.request_id AS id, req.url, res.status,\n"+
			"res.content_type AS type, res.response_time AS time\n"+
		"FROM responses AS res\n"+
		"INNER JOIN requests AS req ON (res.request_id = req.request_id)\n"+
		"WHERE req.request_id > ?\n"+
		"ORDER BY res.response_time ASC, res.response_id ASC\n"+
		"LIMIT 1",
		cid,
	function(err, res) {
		if(err) return cb(err);
		if(!res) return cb(null);
		db.all(
			"SELECT h.algo, h.data\n"+
			"FROM response_hashes AS rh\n"+
			"INNER JOIN hashes AS h ON (rh.hash_id = h.hash_id)",
		function(err, hashes) {
			if(err) return cb(err);
			var digests = {};
			for(var i = 0; i < hashes.length; i++) {
				digests[hashes[i].algo] = hashes[i].data;
			}

			hximport.write_response(socket, {
				time: res.time/1000,
				url: res.url,
				status: res.status,
				type: res.type,
				length: null,
				digests: digests,
			});

			next(res.id, cb);
		});

	});
}
next(0, function(err) {
	if(err) throw err;
	socket.end();
});

