// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include "util/hash.h"
#include "util/html.h"
#include "page.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static TemplateRef entry = NULL;
static TemplateRef notfound = NULL;
static TemplateRef short_hash = NULL;
static TemplateRef weak_hash = NULL;

int page_sources(HTTPConnectionRef const conn, strarg_t const URI) {
	if(!header) {
		template_load("sources-header.html", &header);
		template_load("sources-footer.html", &footer);
		template_load("sources-entry.html", &entry);
		template_load("sources-notfound.html", &notfound);
		template_load("sources-short.html", &short_hash);
		template_load("sources-weak.html", &weak_hash);
	}
	int rc = 0;

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, NULL, NULL, conn);


	TemplateWriteHTTPChunk(footer, NULL, NULL, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

