// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <string.h>
#include <cmark.h>
#include "util/markdown.h"
#include "page.h"

static TemplateRef header = NULL;
static TemplateRef footer = NULL;
static char *content = NULL;
static size_t length = 0;

int page_critical(HTTPConnectionRef const conn) {
	if(!header) {
		template_load("critical-header.html", &header);
		template_load("critical-footer.html", &footer);

		int const options =
			CMARK_OPT_DEFAULT |
			CMARK_OPT_HARDBREAKS |
			CMARK_OPT_NORMALIZE |
			CMARK_OPT_SMART |
			0;

		char buffer[1024*8];
		size_t bytes = 0;
		cmark_parser *parser = cmark_parser_new(options);
		FILE *fp = fopen("./CRITICAL_URLS.md", "rb");
		while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
			cmark_parser_feed(parser, buffer, bytes);
			if (bytes < sizeof(buffer)) {
				break;
			}
		}
		cmark_node *document = cmark_parser_finish(parser);
		cmark_parser_free(parser);
		assert(document); // TODO

		md_process(document);
		content = cmark_render_html(document, options);
		assert(content);
		length = strlen(content);

	}
	int rc = 0;

	HTTPConnectionWriteResponse(conn, 200, "OK");
	HTTPConnectionWriteHeader(conn, "Transfer-Encoding", "chunked");
	HTTPConnectionWriteHeader(conn, "Content-Type", "text/html; charset=utf-8");
	HTTPConnectionBeginBody(conn);
	TemplateWriteHTTPChunk(header, NULL, NULL, conn);

	uv_buf_t part = uv_buf_init(content, length);
	HTTPConnectionWriteChunkv(conn, &part, 1);

	TemplateWriteHTTPChunk(footer, NULL, NULL, conn);
	HTTPConnectionWriteChunkEnd(conn);
	HTTPConnectionEnd(conn);

	return 0;
}

