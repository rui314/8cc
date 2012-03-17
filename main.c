#include <stdio.h>
#include <string.h>
#include "8cc.h"

int main(int argc, char **argv) {
    int wantast = (argc > 1 && !strcmp(argv[1], "-a"));
    List *toplevels = read_toplevels();
    if (!wantast)
        emit_data_section();
    for (Iter *i = list_iter(toplevels); !iter_end(i);) {
        Ast *v = iter_next(i);
        if (wantast)
            printf("%s", ast_to_string(v));
        else
            emit_toplevel(v);
    }
    return 0;
}
