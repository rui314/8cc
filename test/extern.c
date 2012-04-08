#include "test/test.h"

extern int expect(int, int);
extern int externvar1;
int extern externvar2;

void testmain(void) {
    print("extern");
    expect(98, externvar1);
    expect(99, externvar2);
}
