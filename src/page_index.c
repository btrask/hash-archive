// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdlib.h>
#include <string.h>
#include "util/hash.h"
#include "util/html.h"
#include "page.h"

static TemplateRef index = NULL;

static strarg_t const example_url = "https://torrents.linuxmint.com/torrents/linuxmint-18-cinnamon-64bit.iso.torrent";
static strarg_t const example_hash_uri = "hash://sha256/030d8c2d6b7163a482865716958ca03806dfde99a309c927e56aa9962afbb95d";

static char example_named_info[URI_MAX] = "";
static char example_prefix[URI_MAX] = "";
static char example_multihash[URI_MAX] = "";
static char example_ssb[URI_MAX] = "";
static char example_magnet[URI_MAX] = "";

static char const *const critical[] = {
	"https://ftp.heanet.ie/mirrors/linuxmint.com/stable/18/linuxmint-18-cinnamon-64bit.iso",
	"https://code.jquery.com/jquery-2.2.3.min.js",
	"https://ajax.googleapis.com/ajax/libs/jquery/2.1.4/jquery.min.js",
	"https://ftp-master.debian.org/keys/archive-key-8.asc",
	"http://heanet.dl.sourceforge.net/project/keepass/KeePass%202.x/2.32/KeePass-2.32.zip",
	"http://openwall.com/signatures/openwall-signatures.asc",
	"http://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-23.noarch.rpm",
};

static int write_link(TemplateWriteFn const wr, void *const wctx, hash_uri_type const type, strarg_t const URI_unsafe) {
	char *x = link_html(type, URI_unsafe);
	if(!x) return UV_ENOMEM;
	int rc = wr(wctx, uv_buf_init(x, strlen(x)));
	free(x); x = NULL;
	return rc;
}
static int template_var(void *const actx, char const *const var, TemplateWriteFn const wr, void *const wctx) {
	if(0 == strcmp(var, "web-url-example")) {
		return write_link(wr, wctx, LINK_WEB_URL, example_url);
	}
	if(0 == strcmp(var, "hash-uri-example")) {
		return write_link(wr, wctx, LINK_HASH_URI, example_hash_uri);
	}
	if(0 == strcmp(var, "named-info-example")) {
		return write_link(wr, wctx, LINK_NAMED_INFO, example_named_info);
	}
	if(0 == strcmp(var, "multihash-example")) {
		return write_link(wr, wctx, LINK_MULTIHASH, example_multihash);
	}
	if(0 == strcmp(var, "prefix-example")) {
		return write_link(wr, wctx, LINK_PREFIX, example_prefix);
	}
	if(0 == strcmp(var, "ssb-example")) {
		return write_link(wr, wctx, LINK_SSB, example_ssb);
	}
	if(0 == strcmp(var, "magnet-example")) {
		return write_link(wr, wctx, LINK_MAGNET, example_magnet);
	}

	if(0 == strcmp(var, "recent-list")) return 0;

	if(0 == strcmp(var, "critical-list")) {
		for(size_t i = 0; i < numberof(critical); i++) {
			char *x = item_html(LINK_WEB_URL, "", critical[i], false);
			if(!x) return UV_ENOMEM;
			int rc = wr(wctx, uv_buf_init(x, strlen(x)));
			free(x); x = NULL;
			if(rc < 0) return rc;
		}
	}

	return 0;
}

int page_index(HTTPConnectionRef const conn) {
	int rc = 0;
	if(!index) {
		template_load("index.html", &index);

		hash_uri_t obj[1] = {};
		rc = hash_uri_parse(example_hash_uri, obj);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_NAMED_INFO, example_named_info, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_PREFIX, example_prefix, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_MULTIHASH, example_multihash, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_SSB, example_ssb, URI_MAX);
		assert(rc >= 0);
		rc = hash_uri_variant(obj, LINK_MAGNET, example_magnet, URI_MAX);
		assert(rc >= 0);
		hash_uri_destroy(obj);
	}

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(index, template_var, NULL, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

