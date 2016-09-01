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
	regcomp(exp, "\\{\\{[a-zA-Z0-9-]+\\}\\}", REG_EXTENDED);
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

int TemplateWrite(TemplateRef const t, TemplateVarFn const var, void *const actx, TemplateWriteFn const wr, void *const wctx) {
	if(!t) return 0;
	int rc = 0;
	for(size_t i = 0; i < t->count; i++) {
		TemplateStep const *const s = &t->steps[i];
		rc = wr(wctx, uv_buf_init((char *)s->str, s->len));
		if(rc < 0) abort();
		rc = s->var && var ? var(actx, s->var, wr, wctx) : 0;
		if(rc < 0) abort();
		// Security critical!
		// If we stop early but the caller ignores our error,
		// a temporary error here could lead to invalid output!
		// Conversely, if we try to keep going, we could generate
		// invalid output ourselves.
		// Don't even use assert() because it might be compiled out.
		// Perhaps the best option here would be to close the conn.
		// However closing can (theoretically) fail silently...
		// And our HTTP connections are RAII anyway, so we can't
		// close them.
	}
	return rc;
}

static int HTTPConnectionWriteChunk_wrapper(void *ctx, uv_buf_t chunk) {
	HTTPConnectionRef const conn = ctx;
	return HTTPConnectionWriteChunkv(ctx, &chunk, 1);
}
static int async_fs_write_wrapper(void *ctx, uv_buf_t chunk) {
	uv_file const *const fdptr = ctx;
	return async_fs_writeall(*fdptr, &chunk, 1, -1);
}

int TemplateWriteHTTPChunk(TemplateRef const t, TemplateVarFn const var, void *const actx, HTTPConnectionRef const conn) {
	return TemplateWrite(t, var, actx, HTTPConnectionWriteChunk_wrapper, conn);
}
int TemplateWriteFile(TemplateRef const t, TemplateVarFn const var, void *const actx, uv_file const file) {
	uv_file fd = file;
	return TemplateWrite(t, var, actx, async_fs_write_wrapper, &fd);
}

int TemplateStaticVar(void *const actx, char const *const var, TemplateWriteFn const wr, void *const wctx) {
	TemplateStaticArg const *args = actx;
	assertf(args, "TemplateStaticLookup args required");
	for(; args->var; args++) {
		if(0 != strcmp(args->var, var)) continue;
		char const *const x = args->val;
		if(!x) return 0;
		return wr(wctx, uv_buf_init((char *)x, strlen(x)));
	}
	return UV_ENOENT;
}

