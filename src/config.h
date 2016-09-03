// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

// TODO: Proper getenv() based configuration?

#define SERVER_RAW_ADDR NULL
#define SERVER_RAW_PORT 8000

#define QUEUE_WORKERS 16

#define HISTORY_MAX 30
#define SOURCES_MAX 30

#define CRAWL_DELAY_SECONDS (60*60*24)

static strarg_t const example_url = "https://torrents.linuxmint.com/torrents/linuxmint-18-cinnamon-64bit.iso.torrent";
static strarg_t const example_hash_uri = "hash://sha256/030d8c2d6b7163a482865716958ca03806dfde99a309c927e56aa9962afbb95d";

static char const *const critical[] = {
	"https://ftp.heanet.ie/mirrors/linuxmint.com/stable/18/linuxmint-18-cinnamon-64bit.iso",
	"https://code.jquery.com/jquery-2.2.3.min.js",
	"https://ajax.googleapis.com/ajax/libs/jquery/2.1.4/jquery.min.js",
	"https://ftp-master.debian.org/keys/archive-key-8.asc",
	"http://heanet.dl.sourceforge.net/project/keepass/KeePass%202.x/2.32/KeePass-2.32.zip",
	"http://openwall.com/signatures/openwall-signatures.asc",
	"http://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-23.noarch.rpm",
};


