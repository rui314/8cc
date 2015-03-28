// Copyright 2014 Rui Ueyama. Released under the MIT license.

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include "8cc.h"

// Returns the shortest path for the given full path to a file.
static char *clean(char *p) {
    assert(*p == '/');
    char buf[PATH_MAX];
    char *q = buf;
    *q++ = '/';
    for (;;) {
        if (*p == '/') {
            p++;
            continue;
        }
        if (!memcmp("./", p, 2)) {
            p += 2;
            continue;
        }
        if (!memcmp("../", p, 3)) {
            p += 3;
            if (q == buf + 1)
                continue;
            for (q--; q[-1] != '/'; q--);
            continue;
        }
        while (*p != '/' && *p != '\0')
            *q++ = *p++;
        if (*p == '/') {
            *q++ = *p++;
            continue;
        }
        *q = '\0';
        return strdup(buf);
    }
}

// Returns the shortest absolute path for the given path.
char *fullpath(char *path) {
    static char cwd[PATH_MAX];
    if (path[0] == '/')
        return clean(path);
    if (*cwd == '\0' && !getcwd(cwd, PATH_MAX))
        error("getcwd failed: %s", strerror(errno));
    return clean(format("%s/%s", cwd, path));
}
