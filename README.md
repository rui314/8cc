8cc C Compiler
==============

8cc is a small C compiler. It supports a broad range of C99 features, such as
compound literals or designated initializers. Some GNU extensions, such as
computed gotos, are also supported. 8cc is self-hosting, which means 8cc can
compile itself.

8cc's source code is carefully written to be as concise and easy-to-read as
possible, so that the source code will eventually be a good study material to
learn various techniques used in compilers. You might find the lexer, the
preprocessor and the parser are already useful to learn how C source code is
processed at each stage.

It does not produce optimized assembly code; 8cc treats the CPU as a stack
machine. Local variables are always assigned on the stack. Operations in the
form of `A = B op C` are executed in the following way.

 1. Load B and C to registers
 2. Apply op to yield a result
 3. Write the results back to A's location

Producing optimization assembly is being planned.

Build
-----

Run make to build:

    make

8cc comes with unit tests. To run the tests, give "test" as an argument:

    make test

The following command compiles 8cc three times. The second generation
binary and the third are self-compiled ones, and it's tested that they
are identical. The unit tests are run for each generation of binaries.

    make fulltest

8cc supports x86-64 Linux only. I'm using Ubuntu 11 as a development platform.
It should work on other x86-64 Linux distributions.

Author
------

Rui Ueyama <rui314@gmail.com>


Links for C compiler development
--------------------------------

lcc: A Retargetable C Compiler: Design and Implementation
Addison-Wesley, 1995, ISBN 0805316701, ISBN-13 9780805316704
http://www.amazon.com/dp/0805316701
https://github.com/drh/lcc

TCC: Tiny C Compiler
http://bellard.org/tcc/
http://repo.or.cz/w/tinycc.git/tree

C99 standard final draft
http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf

C11 standard final draft
http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf

Dave Prosser's C Preprocessing Algorithm
http://www.spinellis.gr/blog/20060626/

x86-64 ABI
http://www.x86-64.org/documentation/abi.pdf
