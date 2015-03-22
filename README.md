8cc C Compiler
==============

8cc is a compiler for the C programming language.
It's intended to support all C11 language features
while keeping the code as small and simple as possible.

The compiler is able to compile itself.
You can see its code both as an implementation of the C language
and as an example of what this compiler is able to compile.

8cc's source code is carefully written to be as concise and easy-to-read
as possible, so that the source code becomes good study material
to learn about various techniques used in compilers.
You may find the lexer, the preprocessor and the parser are
already useful to learn how C source code is processed at each stage.

It's not an optimizing compiler.
Generated code is usually 2x or more slower than GCC.
I plan to implement a reasonable level of optimization in the future.

8cc supports x86-64 Linux only. I have no plan to make it portable until
I fix all known miscompilations and implement an optimization pass.
As of 2015, I'm using Ubuntu 14 as my development platform.
It should work on other x86-64 Linux distributions though.

Note: Do not have high expectations on this compiler.
If you try to compile a program other than the compiler itself,
there's a good chance to see compile errors or miscompilations.
This is basically a one-man project, and I have spent only a few
months of my spare time so far.

Build
-----

Run make to build:

    make

8cc comes with unit tests. To run the tests, give "test" as an argument:

    make test

The following target builds 8cc three times to verify that
stage1 compiler can build stage2, and stage2 can build stage3.
It then compares stage2 and stage3 binaries byte-by-byte to verify
that we reach a fixed point.

    make fulltest

Author
------

Rui Ueyama <rui314@gmail.com>


Links for C compiler development
--------------------------------

Besides popular books about compiler, such as the Dragon Book,
I found the following books/documents are very useful
to develop a C compiler.
Note that the standard draft versions are very close to the ratified versions.
You can practically use them as the standard documents.

-   LCC: A Retargetable C Compiler: Design and Implementation
    http://www.amazon.com/dp/0805316701,
    https://github.com/drh/lcc

-   TCC: Tiny C Compiler
    http://bellard.org/tcc/,
    http://repo.or.cz/w/tinycc.git/tree

-   C99 standard final draft
    http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1124.pdf

-   C11 standard final draft
    http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf

-   Dave Prosser's C Preprocessing Algorithm
    http://www.spinellis.gr/blog/20060626/

-   The x86-64 ABI
    http://www.x86-64.org/documentation/abi.pdf
