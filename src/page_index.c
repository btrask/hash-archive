// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include "util/Template.h"
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

int page_index(HTTPConnectionRef const conn) {
	int rc = 0;
	if(!index) {
		rc = TemplateCreateFromPath("/home/user/Code/hash-archive/templates/index.html", &index);
		assert(rc >= 0);

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

	TemplateStaticArg args[] = {
		{ "web-url-example", link_html(LINK_WEB_URL, example_url) }, // TODO LEAK
		{ "hash-uri-example", link_html(LINK_HASH_URI, example_hash_uri) },
		{ "named-info-example", link_html(LINK_NAMED_INFO, example_named_info) },
		{ "multihash-example", link_html(LINK_MULTIHASH, example_multihash) },
		{ "prefix-example", link_html(LINK_PREFIX, example_prefix) },
		{ "ssb-example", link_html(LINK_SSB, example_ssb) },
		{ "magnet-example", link_html(LINK_MAGNET, example_magnet) },
		{ "examples", "" },
		{ "recent-list", "" },
		{ "critical-list", "" },
		{ NULL, NULL },
	};

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(index, &TemplateStaticCBs, &args, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

