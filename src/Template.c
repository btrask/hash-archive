// Copyright 2014-2015 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include "Template.h"

// TODO: Get rid of this duplication...
#define MAX(a, b) ({ \
	__typeof__(a) const __a = (a); \
	__typeof__(b) const __b = (b); \
	__a > __b ? __a : __b; \
})
#define FREE(ptrptr) do { \
	__typeof__(ptrptr) const __x = (ptrptr); \
	free(*__x); *__x = NULL; \
} while(0)
#define assertf(x, ...) assert(x)

#define TEMPLATE_MAX (1024 * 512)

typedef struct {
	char *str;
	size_t len;
	char *var;
} TemplateStep;
struct Template {
	size_t count;
	TemplateStep *steps;
};

int TemplateCreate(char const *const str, TemplateRef *const out) {
	TemplateRef t = calloc(1, sizeof(struct Template));
	if(!t) return UV_ENOMEM;
	t->count = 0;
	t->steps = NULL;
	size_t size = 0;

	regex_t exp[1];
	regcomp(exp, "\\{\\{[a-zA-Z0-9]+\\}\\}", REG_EXTENDED);
	char const *pos = str;
	for(;;) {
		if(t->count >= size) {
			size = MAX(10, size * 2);
			TemplateStep *x = reallocarray(t->steps, size, sizeof(TemplateStep));
			if(!x) {
				regfree(exp);
				TemplateFree(&t);
				return UV_ENOMEM;
			}
			t->steps = x; x = NULL;
		}

		regmatch_t match[1];
		if(0 == regexec(exp, pos, 1, match, 0)) {
			regoff_t const loc = match->rm_so;
			regoff_t const len = match->rm_eo - loc;
			t->steps[t->count].str = strndup(pos, loc);
			t->steps[t->count].len = loc;
			t->steps[t->count].var = strndup(pos+loc+2, len-4);
			++t->count;
			pos += match->rm_eo;
		} else {
			t->steps[t->count].str = strdup(pos);
			t->steps[t->count].len = strlen(pos);
			t->steps[t->count].var = NULL;
			++t->count;
			break;
		}
	}
	regfree(exp);

	*out = t;
	return 0;
}
int TemplateCreateFromPath(char const *const path, TemplateRef *const out) {
	int rc = 0;
	char *str = NULL;
	uv_file file = async_fs_open(path, O_RDONLY, 0000);
	if(file < 0) rc = (int)file;
	if(rc < 0) goto cleanup;
	uv_fs_t req;
	rc = async_fs_fstat(file, &req);
	if(rc < 0) goto cleanup;
	int64_t const size = req.statbuf.st_size;
	if(size > TEMPLATE_MAX) rc = UV_EFBIG;
	if(rc < 0) goto cleanup;
	str = malloc((size_t)size+1);
	if(!str) rc = UV_ENOMEM;
	if(rc < 0) goto cleanup;
	uv_buf_t info = uv_buf_init(str, size);
	ssize_t len = async_fs_readall_simple(file, &info);
	if(len < 0) rc = (int)len;
	else if(size != len) rc = UV_EBUSY; // Detect race condition.
	if(rc < 0) goto cleanup;
	str[size] = '\0';
	rc = TemplateCreate(str, out);
cleanup:
	if(file >= 0) async_fs_close(file);
	file = -1;
	FREE(&str);
	return rc;
}
void TemplateFree(TemplateRef *const tptr) {
	TemplateRef t = *tptr;
	if(!t) return;
	for(size_t i = 0; i < t->count; ++i) {
		FREE(&t->steps[i].str);
		t->steps[i].len = 0;
		FREE(&t->steps[i].var);
	}
//	assert_zeroed(t->steps, t->count);
	FREE(&t->steps);
	t->count = 0;
//	assert_zeroed(t, 1);
	FREE(tptr); t = NULL;
}
int TemplateWrite(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, TemplateWritev const writev, void *wctx) {
	if(!t) return 0;

	uv_buf_t *output = calloc(t->count * 2, sizeof(uv_buf_t));
	char **vals = calloc(t->count, sizeof(char *));
	if(!output || !vals) {
		FREE(&output);
		FREE(&vals);
		return UV_ENOMEM;
	}

	for(size_t i = 0; i < t->count; i++) {
		TemplateStep const *const s = &t->steps[i];
		char *const val = s->var ? cbs->lookup(actx, s->var) : NULL;
		size_t const len = val ? strlen(val) : 0;
		output[i*2+0] = uv_buf_init((char *)s->str, s->len);
		output[i*2+1] = uv_buf_init((char *)val, len);
		vals[i] = val;
	}

	int rc = writev(wctx, output, t->count * 2);

	FREE(&output);

	for(size_t i = 0; i < t->count; i++) {
		TemplateStep const *const s = &t->steps[i];
		if(!s->var) continue;
		if(cbs->free) cbs->free(actx, s->var, &vals[i]);
		else vals[i] = NULL;
	}
//	assert_zeroed(vals, t->count);
	FREE(&vals);

	return rc;
}
int TemplateWriteHTTPChunk(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, HTTPConnectionRef const conn) {
	return TemplateWrite(t, cbs, actx, (TemplateWritev)HTTPConnectionWriteChunkv, conn);
}
static int async_fs_write_wrapper(uv_file const *const fdptr, uv_buf_t parts[], unsigned int const count) {
	return async_fs_writeall(*fdptr, parts, count, -1);
}
int TemplateWriteFile(TemplateRef const t, TemplateArgCBs const *const cbs, void const *const actx, uv_file const file) {
	assertf(sizeof(void *) >= sizeof(file), "Can't cast uv_file (%ld) to void * (%ld)", (long)sizeof(file), (long)sizeof(void *));
	return TemplateWrite(t, cbs, actx, (TemplateWritev)async_fs_write_wrapper, (uv_file *)&file);
}

static char *TemplateStaticLookup(void const *const ptr, char const *const var) {
	TemplateStaticArg const *args = ptr;
	assertf(args, "TemplateStaticLookup args required");
	while(args->var) {
		if(0 == strcmp(args->var, var)) return (char *)args->val;
		args++;
	}
	return NULL;
}
TemplateArgCBs const TemplateStaticCBs = {
	.lookup = TemplateStaticLookup,
	.free = NULL,
};


/*#include "../../deps/cmark/src/houdini.h"
#include "../../deps/cmark/src/buffer.h"

// HACK
extern cmark_mem DEFAULT_MEM_ALLOCATOR;

char *htmlenc(char const *const str) {
	if(!str) return NULL;
	cmark_strbuf out = CMARK_BUF_INIT(&DEFAULT_MEM_ALLOCATOR);
	houdini_escape_html(&out, (uint8_t const *)str, strlen(str));
	return (char *)cmark_strbuf_detach(&out);
}*/

