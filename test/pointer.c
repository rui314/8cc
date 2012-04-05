#include "test/test.h"

void t1() {
    int a = 61;
    int *b = &a;
    expect(61, *b);
}

void t2() {
    char *c = "ab";
    expect(97, *c);
}

void t3() {
    char *c = "ab" + 1;
    expect(98, *c);
}

void t4() {
    char s[] = "xyz";
    char *c = s + 2;
    expect(122, *c);
}

void t5() {
    char s[] = "xyz";
    *s = 65;
    expect(65, *s);
}

void t6() {
    struct tag {
        int val;
        struct tag *next;
    };
    struct tag node1 = { 1, NULL };
    struct tag node2 = { 2, &node1 };
    struct tag node3 = { 3, &node2 };
    struct tag *p = &node3;
    expect(3, p->val);
    expect(2, p->next->val);
    expect(1, p->next->next->val);
    p->next = p->next->next;
    expect(1, p->next->val);
}

void testmain() {
    print("pointer");
    t1();
    t2();
    t3();
    t4();
    t5();
    t6();
}
