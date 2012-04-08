#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "8cc.h"

static void usage(void) {
    fprintf(stderr, "Usage: 8cc [ -a ] [ -h ] <file>\n");
    exit(1);
}

int main(int argc, char **argv) {
    bool wantast = false;
    int i;

    for (i = 1; argv[i]; i++) {
        if (!strcmp(argv[i], "-a"))
            wantast = true;
        else if (!strcmp(argv[i], "-h"))
            usage();
        else break;
    }
    if (i != argc - 1)
        usage();
    char *file = argv[i];

    setbuf(stdout, NULL);
    cpp_init();
    lex_init(file);

    if (wantast)
        suppress_warning = true;
    List *toplevels = read_toplevels();
    if (!wantast)
        emit_data_section();
    for (Iter *i = list_iter(toplevels); !iter_end(i);) {
        Node *v = iter_next(i);
        if (wantast)
            printf("%s", a2s(v));
        else
            emit_toplevel(v);
    }
    return 0;
}
