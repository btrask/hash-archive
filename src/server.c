// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdlib.h>
#include <time.h>
#include <async/async.h>
#include <async/http/HTTPServer.h>
#include <async/http/QueryString.h>
#include "util/strext.h"
#include "util/hash.h"
#include "util/path.h"
#include "util/url.h"
#include "page.h"
#include "db.h"
#include "errors.h"
#include "config.h"
#include "queue.h"

static HTTPServerRef server_raw = NULL;
static HTTPServerRef server_tls = NULL;

// TODO: Remember that we should use the archive's URL representation in the DB.
// http://com,example,www/ or something like that


// TODO
int import_init(void);


static int parse_error(HTTPConnectionRef const conn, strarg_t const query) {
	static TemplateRef error = NULL;
	if(!error) {
		template_load("error.html", &error);
	}
	char *escaped = html_encode(query);
	if(!escaped) return UV_ENOMEM;
	TemplateStaticArg args[] = {
		{"query", escaped},
		{NULL, NULL},
	};
	HTTPConnectionWriteResponse(conn, 400, "Bad Request");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(error, TemplateStaticVar, &args, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);
	FREE(&escaped);
	return 0;
}


static int GET_index(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	if(0 != uripathcmp(URI, "/", NULL)) return -1;
	return hx_httperr(page_index(conn));
}
static int POST_lookup(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_POST != method) return -1;
	if(0 != uripathcmp(URI, "/lookup", NULL)) return -1;

	char body[URI_MAX * 2];
	ssize_t len = 0;
	static strarg_t const fields[] = { "str" };
	str_t *str = NULL;
	char parsed[URI_MAX];
	char loc[URI_MAX];
	int rc = 0;

	len = HTTPConnectionReadBodyStatic(conn, (unsigned char *)body, sizeof(body)-1);
	if(len < 0) rc = len;
	if(rc < 0) goto cleanup;
	body[len] = '\0';

	QSValuesParse(body, &str, fields, 1);
	if(!str) {
		HTTPConnectionSendStatus(conn, 400); // TODO
		rc = 0;
		goto cleanup;
	}

	rc = hash_uri_normalize(str, parsed, sizeof(parsed));
	if(rc >= 0) {
		snprintf(loc, sizeof(loc), "/sources/%s", str);
		HTTPConnectionSendRedirect(conn, 301, loc);
		goto cleanup;
	}
	if(HASH_EPARSE != rc) goto cleanup;

	rc = url_normalize(str, parsed, sizeof(parsed));
	if(rc >= 0) {
		snprintf(loc, sizeof(loc), "/history/%s", str);
		HTTPConnectionSendRedirect(conn, 301, loc);
		goto cleanup;
	}
	if(URL_EPARSE != rc) goto cleanup;

	rc = parse_error(conn, str);

cleanup:
	FREE(&str);
	if(rc < 0) return hx_httperr(rc);
	return 0;
}
static int GET_history(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	char url[1023+1]; url[0] = '\0';
	sscanf(URI, "/history/%1023s", url);
	if('\0' == url[0]) return -1;
	int rc = page_history(conn, url);
	if(URL_EPARSE == rc) return hx_httperr(parse_error(conn, url));
	return hx_httperr(rc);
}
static int GET_sources(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	char hash[1023+1]; hash[0] = '\0';
	sscanf(URI, "/sources/%1023s", hash);
	if('\0' == hash[0]) return -1;
	int rc = page_sources(conn, hash);
	if(HASH_EPARSE == rc) return hx_httperr(parse_error(conn, hash));
	return hx_httperr(rc);
}
static int GET_critical(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	if(0 != uripathcmp(URI, "/critical/", NULL)) return -1;
	return hx_httperr(page_critical(conn));
}

static int GET_api_enqueue(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	char url[1023+1]; url[0] = '\0';
	sscanf(URI, "/api/enqueue/%1023s", url);
	if('\0' == url[0]) return -1;
	return hx_httperr(api_enqueue(conn, url));
}
static int GET_api_history(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	char url[1023+1]; url[0] = '\0';
	sscanf(URI, "/api/history/%1023s", url);
	if('\0' == url[0]) return -1;
	return hx_httperr(api_history(conn, url));
}
static int GET_api_sources(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	char hash[1023+1]; hash[0] = '\0';
	sscanf(URI, "/api/sources/%1023s", hash);
	if('\0' == hash[0]) return -1;
	return hx_httperr(api_sources(conn, hash));
}
static int GET_api_dump(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;
	unsigned long long start = 0, duration = 0;
	sscanf(URI, "/api/dump/?start=%llu&duration=%llu", &start, &duration);
	if(!start || !duration) return -1;
	if(start > UINT64_MAX) start = UINT64_MAX;
	if(duration > UINT64_MAX) duration = UINT64_MAX;
	return hx_httperr(api_dump(conn, (uint64_t)start, (uint64_t)duration));
}

static int GET_static(HTTPConnectionRef const conn, HTTPMethod const method, strarg_t const URI, HTTPHeadersRef const headers) {
	if(HTTP_GET != method && HTTP_HEAD != method) return -1;

	url_t obj[1];
	int rc = url_parse(URI, obj);
	if(rc < 0) return hx_httperr(rc);

	// TODO: Decode obj->path.

	char path[4095+1];
	rc = path_subpath_secure(CONFIG_STATIC_DIR, obj->path, path, sizeof(path)); // TODO
	if(rc < 0) return hx_httperr(rc);

	strarg_t const type = path_exttype(path_extname(path));
	rc = HTTPConnectionSendFile(conn, path, type, -1);
	if(UV_EPIPE == rc) rc =  0;
	if(UV_EISDIR == rc) {
		str_t location[URI_MAX]; location[0] = '\0';
		rc = snprintf(location, sizeof(location), "%s/", URI);
		if(rc >= sizeof(location)) return 414; // Request-URI Too Large
		if(rc < 0) return 500;
		// TODO: HTTPConnection needs to validate the headers it writes.
		// Must be basic ASCII, no line breaks or -- (HTTP comment)?
		HTTPConnectionSendRedirect(conn, 301, location);
		return 0;
	}
	if(rc < 0) return hx_httperr(rc);
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

	// DNS rebinding protection.
	// TODO: Accept IP addresses? Lots of complexity...
	strarg_t const host = HTTPHeadersGet(headers, "host");
	host_t obj[1];
	rc = host_parse(host, obj);
	if(	CONFIG_HOSTNAME_EXPECTED &&
		0 != strcasecmp(obj->domain, CONFIG_HOSTNAME_EXPECTED) &&
		0 != strcasecmp(obj->domain, "localhost")) rc = UV_EACCES;
	if(rc < 0) goto cleanup;

	if(CONFIG_SERVER_TLS_PORT && server != server_tls) {
		rc = HTTPConnectionSendSecureRedirect(conn, obj->domain, CONFIG_SERVER_TLS_PORT, URI);
		goto cleanup;
	}

	rc = -1;
	rc = rc >= 0 ? rc : GET_index(conn, method, URI, headers);
	rc = rc >= 0 ? rc : POST_lookup(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_history(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_sources(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_critical(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_api_enqueue(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_api_history(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_api_sources(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_api_dump(conn, method, URI, headers);
	rc = rc >= 0 ? rc : GET_static(conn, method, URI, headers);
	if(rc < 0) rc = 404;
	if(rc > 0) HTTPConnectionSendStatus(conn, rc);

cleanup:
	if(rc < 0) HTTPConnectionSendStatus(conn, hx_httperr(rc));
//	char const *const username = NULL;
//	HTTPConnectionLog(conn, URI, username, headers, SERVER_LOG_FILE);
	HTTPHeadersFree(&headers);
}

static void init(void *ignore) {
	HTTPServerRef raw = NULL;
	HTTPServerRef tls = NULL;
	int rc;

	rc = hx_db_load();
	if(rc < 0) {
		alogf("Database load error: %s\n", hx_strerror(rc));
		goto cleanup;
	}
	queue_log(10);


	queue_init();
	for(size_t i = 0; i < CONFIG_QUEUE_WORKERS; i++) {
		async_spawn(STACK_DEFAULT, queue_work_loop, NULL);
	}

	if(CONFIG_SERVER_TLS_PORT) {
		int const port = CONFIG_SERVER_TLS_PORT;
		rc = HTTPServerCreate(listener, NULL, &tls);
		if(rc < 0) {
			alogf("TLS server error: %s\n", hx_strerror(rc));
			goto cleanup;
		}
		rc = HTTPServerListenSecurePaths(tls, CONFIG_SERVER_TLS_ADDR, port,
			CONFIG_SERVER_TLS_KEY_PATH,
			CONFIG_SERVER_TLS_CRT_PATH);
		if(rc < 0) {
			alogf("TLS server error: %s\n", hx_strerror(rc));
			goto cleanup;
		}
		alogf("Hash Archive running at https://localhost:%d/\n", port);
		server_tls = tls; tls = NULL;
	}
	if(CONFIG_SERVER_RAW_PORT) {
		int const port = CONFIG_SERVER_RAW_PORT;
		rc = HTTPServerCreate(listener, NULL, &raw);
		if(rc < 0) {
			alogf("Raw server error: %s\n", hx_strerror(rc));
			goto cleanup;
		}
		rc = HTTPServerListen(raw, CONFIG_SERVER_RAW_ADDR, port);
		if(rc < 0) {
			alogf("Raw server error: %s\n", hx_strerror(rc));
			goto cleanup;
		}
		alogf("Hash Archive running at http://localhost:%d/\n", port);
		server_raw = raw; raw = NULL;
	}

	rc = import_init();
	if(rc < 0) {
		alogf("Import socket error: %s\n", hx_strerror(rc));
		goto cleanup;
	}

cleanup:
	HTTPServerFree(&raw);
	HTTPServerFree(&tls);
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

