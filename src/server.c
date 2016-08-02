// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <stdlib.h>
#include <async/async.h>
#include <async/http/HTTPServer.h>
#include "strext.h"
#include "hash.h"
#include "url.h"
#include "Template.h"
#include "html.h"

#define SERVER_RAW_ADDR NULL
#define SERVER_RAW_PORT 8000

#define URI_MAX (1023+1)
#define USER_AGENT "Hash Archive (https://github.com/btrask/hash-archive)"

#define assertf(x, ...) assert(x) // TODO

typedef char str_t;
typedef char const *strarg_t;

static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;

// TODO: Remember that we should use the archive's URL representation in the DB.
// http://com,example,www/ or something like that


static int send_get(strarg_t const URL, HTTPConnectionRef *const out) {
	HTTPConnectionRef conn = NULL;
	url_t obj[1];
	int rc = 0;
	rc = url_parse(URL, obj);
	if(rc < 0) goto cleanup;
	rc = rc < 0 ? rc : HTTPConnectionCreateOutgoingSecure(obj->host, 0, NULL, &conn);
	rc = rc < 0 ? rc : HTTPConnectionWriteRequest(conn, HTTP_GET, obj->path, obj->host);
	rc = rc < 0 ? rc : HTTPConnectionWriteHeader(conn, "User-Agent", USER_AGENT);
	HTTPConnectionSetKeepAlive(conn, false); // No point.
	rc = rc < 0 ? rc : HTTPConnectionBeginBody(conn);
	rc = rc < 0 ? rc : HTTPConnectionEnd(conn);
	if(rc < 0) goto cleanup;
	*out = conn; conn = NULL;
cleanup:
	HTTPConnectionFree(&conn);
	return rc;
}
static int url_fetch(strarg_t const URL, int *const outstatus, HTTPHeadersRef *const outheaders, hasher_t **const outhasher) {
	HTTPConnectionRef conn = NULL;
	HTTPHeadersRef headers = NULL;
	hasher_t *hasher = NULL;
	int status = 0;
	int rc = 0;

	rc = rc < 0 ? rc : send_get(URL, &conn);
	rc = rc < 0 ? rc : HTTPConnectionReadResponseStatus(conn, &status);
	rc = rc < 0 ? rc : HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

	rc = hasher_create(HASHER_ALGOS_ALL, &hasher);
	if(rc < 0) goto cleanup;
	for(;;) {
		uv_buf_t buf[1];
		rc = HTTPConnectionReadBody(conn, buf);
		if(rc < 0) goto cleanup;
		if(0 == buf->len) break;
		async_pool_enter(NULL);
		rc = hasher_update(hasher, (unsigned char *)buf->base, buf->len);
		async_pool_leave(NULL);
		if(rc < 0) goto cleanup;
	}
	rc = hasher_finish(hasher);
	if(rc < 0) goto cleanup;

	*outstatus = status; status = 0;
	*outheaders = headers; headers = NULL;
	*outhasher = hasher; hasher = NULL;

cleanup:
	HTTPConnectionFree(&conn);
	HTTPHeadersFree(&headers);
	hasher_free(&hasher);
	return rc;
}

static char *link_html(hash_uri_type const t, strarg_t const URI_unsafe) {
	char *r = NULL;
	char *escaped = html_encode(URI_unsafe);
	if(!escaped) return NULL;
	switch(t) {
	case LINK_NONE: r = aasprintf(
		"<span>%s</span>", escaped); break;
	case LINK_RAW: r = aasprintf(
		"<a href=\"%s\">%s</a>", escaped, escaped); break;
	case LINK_WEB_URL: r = aasprintf(
		"<a href=\"/history/%s\">%s</a>"
		"<sup>[<a href=\"%s\" rel=\"nofollow\" "
			"target=\"_blank\">^</a>]</sup>",
		escaped, escaped, escaped); break;
	case LINK_HASH_URI:
	case LINK_NAMED_INFO:
	case LINK_MAGNET: r = aasprintf(
		"<a href=\"/sources/%s\">%s</a>"
		"<sup>[<a href=\"%s\" rel=\"nofollow\" "
			"target=\"_blank\">#</a>]</sup>",
		escaped, escaped, escaped); break;
	case LINK_MULTIHASH:
	case LINK_PREFIX:
	case LINK_SSB: r = aasprintf(
		"<a href=\"/sources/%s\">%s</a>", escaped, escaped); break;
	default: assertf(0, "Unknown link type %d", type);
	}
	free(escaped); escaped = NULL;
	return r;
}
static char *item_html(hash_uri_type const type, strarg_t const label_escaped, strarg_t const URI_unsafe, bool const deprecated) {
	strarg_t const class = deprecated ? "deprecated" : "";
	char *link = link_html(type, URI_unsafe);
	if(!link) return NULL;
	char *r = aasprintf("<li class=\"break %s\">%s%s</li>",
		class, label_escaped, link);
	free(link); link = NULL;
	return r;
}
static char *direct_link_html(hash_uri_type const type, strarg_t const URI_unsafe) {
	char *r = NULL;
	char *escaped = html_encode(URI_unsafe);
	if(!escaped) return NULL;
	switch(type) {
	case LINK_WEB_URL:
	case LINK_HASH_URI:
	case LINK_NAMED_INFO:
	case LINK_MAGNET: r = aasprintf(
		"<a href=\"%s\" rel=\"nofollow\">%s</a>",
		escaped, escaped); break;
	case LINK_MULTIHASH:
	case LINK_PREFIX:
	case LINK_SSB: r = aasprintf("<span>%s</span>", escaped); break;;
	default: assertf(0, "Unknown link type %d", type);
	}
	free(escaped); escaped = NULL;
	return r;
}

static int GET_index(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	if(0 != uripathcmp(URI, "/", NULL)) return -1;

	static TemplateRef index = NULL;
	if(!index) {
		int rc = TemplateCreateFromPath("/home/user/Code/hash-archive/templates/index.html", &index);
		if(rc < 0) return HTTPError(rc);
	}

	static strarg_t const example_url = "https://torrents.linuxmint.com/torrents/linuxmint-18-cinnamon-64bit.iso.torrent";
	static strarg_t const example_hash = "hash://sha256/030d8c2d6b7163a482865716958ca03806dfde99a309c927e56aa9962afbb95d";

	hash_uri_t example_obj[1] = {};
	int rc = hash_uri_parse(example_hash, example_obj);
	alogf("parse error: %s\n", hash_strerror(rc));
	assert(rc >= 0);
	char tmp[500] = "";
	hash_uri_format(example_obj, tmp, sizeof(tmp));
	alogf("test %s\n", tmp);
	hash_uri_destroy(example_obj);

	TemplateStaticArg args[] = {
		{ "web-url-example", link_html(LINK_WEB_URL, example_url) }, // TODO LEAK
		{ "hash-uri-example", "" },
		{ "named-info-example", "" },
		{ "multihash-example", "" },
		{ "prefix-example", "" },
		{ "ssb-example", "" },
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

static void listener(void *ctx, HTTPServerRef const server, HTTPConnectionRef const conn) {
	assert(server);
	assert(conn);
	HTTPMethod method = 99; // 0 is HTTP_DELETE...
	str_t URI[URI_MAX]; URI[0] = '\0';
	HTTPHeadersRef headers = NULL;
	ssize_t len = 0;
	int rc = 0;

	len = HTTPConnectionReadRequest(conn, &method, URI, sizeof(URI));
	if(UV_EOF == len) goto cleanup;
	if(UV_ECONNRESET == len) goto cleanup;
	if(len < 0) {
		rc = len;
		alogf("Request error: %s\n", uv_strerror(rc));
		goto cleanup;
	}

	rc = HTTPHeadersCreateFromConnection(conn, &headers);
	if(rc < 0) goto cleanup;

	strarg_t const host = HTTPHeadersGet(headers, "host");
	host_t obj[1];
	rc = host_parse(host, obj);
	// TODO: Verify Host header to prevent DNS rebinding.

/*	if(SERVER_PORT_TLS && server != server_tls) {
		rc = HTTPConnectionSendSecureRedirect(conn, obj->domain, SERVER_PORT_TLS, URI);
		goto cleanup;
	}*/

	rc = -1;
	rc = rc >= 0 ? rc : GET_index(conn, method, URI, headers);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);

cleanup:
	if(rc < 0) HTTPConnectionSendStatus(conn, HTTPError(rc));
//	char const *const username = NULL;
//	HTTPConnectionLog(conn, URI, username, headers, SERVER_LOG_FILE);
	HTTPHeadersFree(&headers);
}

static void init(void *ignore) {
	HTTPServerRef server = NULL;
	int rc;
	rc = HTTPServerCreate(listener, NULL, &server);
	if(rc < 0) goto cleanup;
	rc = HTTPServerListen(server, SERVER_RAW_ADDR, SERVER_RAW_PORT);
	if(rc < 0) goto cleanup;
	int const port = SERVER_RAW_PORT;
	alogf("Hash Archive running at http://localhost:%d/\n", port);
	server_raw = server; server = NULL;



	{ // TODO: debug
	int status = 0;
	HTTPHeadersRef headers = NULL;
	hasher_t *hasher = NULL;
	rc = url_fetch("http://localhost:8000/", &status, &headers, &hasher);
	alogf("got result %s (%d): %d, %s\n", uv_strerror(rc), rc, status, HTTPHeadersGet(headers, "content-type"));
	}


cleanup:
	HTTPServerFree(&server);
	return;
}
static void term(void *ignore) {
	HTTPServerClose(server_raw);
	HTTPServerClose(server_tls);
}
static void cleanup(void *ignore) {
	HTTPServerFree(&server_raw);
	HTTPServerFree(&server_tls);
	async_pool_destroy_shared();
}

int main(void) {
	int rc = async_process_init();
	if(rc < 0) {
		fprintf(stderr, "Initialization error: %s\n", uv_strerror(rc));
		return 1;
	}
/*	rc = tls_init();
	if(rc < 0) {
		fprintf(stderr, "TLS initialization error: %s\n", strerror(errno));
		return 1;
	}*/

	// Even our init code wants to use async I/O.
	async_spawn(STACK_DEFAULT, init, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_spawn(STACK_DEFAULT, term, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

#if 0
	// cleanup is separate from term because connections might
	// still be active.
	async_spawn(STACK_DEFAULT, cleanup, NULL);
	uv_run(async_loop, UV_RUN_DEFAULT);

	async_process_destroy();
#endif

	return  0;
}

