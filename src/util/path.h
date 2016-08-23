// Copyright 2016 Ben Trask
// MIT licensed (see LICENSE for details)

#include <stdlib.h>

char const *path_extname(char const *const path);
char const *path_exttype(char const *const ext);

int path_dir_index(char *const path, char const *const index, size_t const max);
int path_subpath_secure(char const *const dir, char const *const subpath, char *const out, size_t const max);

