// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "path.h"

char const *path_extname(char const *const path) {
	if(!path) return NULL;
	return strrchr(path, '.');
}
char const *path_exttype(char const *const ext) {
	if(!ext) return NULL;
	// TODO: Obviously this is quite a hack.
	// I was thinking about packing extensions into integers to use
	// them in a switch statement, but that's a little bit too crazy.
	// The optimal solution would probably be a trie.
	if(0 == strcasecmp(ext, ".html")) return "text/html; charset=utf-8";
	if(0 == strcasecmp(ext, ".css")) return "text/css; charset=utf-8";
	if(0 == strcasecmp(ext, ".js")) return "text/javascript; charset=utf-8";
	if(0 == strcasecmp(ext, ".png")) return "image/png";
	if(0 == strcasecmp(ext, ".jpg")) return "image/jpeg";
	if(0 == strcasecmp(ext, ".jpeg")) return "image/jpeg";
	if(0 == strcasecmp(ext, ".gif")) return "image/gif";
	if(0 == strcasecmp(ext, ".ico")) return "image/vnd.microsoft.icon";
	return NULL;
}

int path_dir_index(char *const path, char const *const index, size_t const max) {
	assert(max > 0);
	if(!path) return -EINVAL;
	size_t const len = strlen(path);
	if('/' != path[len-1]) return 0;
	int rc = strlcat(path, index, max);
	if(rc >= max) return -ENAMETOOLONG;
	if(rc < 0) return -errno ?: -1;
	return 0;
}
int path_subpath_secure(char const *const dir, char const *const subpath, char *const out, size_t const max) {
	assert(out);
	assert(max > 0);
	if(!dir) return -EINVAL;
	if(!subpath) return -EINVAL;

	// Critical security checks!
	if('/' != subpath[0]) return -EPERM;
	if(strstr(subpath, "..")) return -EPERM;
	if(strlen(dir) >= max) return -EPERM;

	int rc = snprintf(out, max, "%s%s", dir, subpath);
	if(rc >= max) return -ENAMETOOLONG;
	if(rc < 0) return -errno ?: -1;
	return 0;
}

