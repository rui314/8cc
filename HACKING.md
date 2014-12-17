I accept small patches, but because this is my hobby project to
learn about compilers, it's unlikely to accept large patches.
That's in practice not going to be an issue, because
if you are writing a large patch, that is your hobby project.
You want to hack in your forked repository rather than
taking time to send pull requests.

# Memory management

No memory management is a memory management scheme in 8cc.
Memory regions allocated using malloc are never freed
until the process terminates. That has greatly simplified
the code and the APIs because 1) you can write code as if
garbage collector were present, and 2) that design
decision has eliminated use-after-free bugs entirely.

Modern computers have gigs of memory. 8cc consumes
only about 100MB to compile 10K lines of C source file.
Compiler is not a long-running process.
It will never run out of memory unless you give an
unrealistically large source file.

If we really need to free memory, we could use Boehm garbage
collector. I don't see that need at this moment though.

# Backend

Backend is being rewritten. Once it's done, the current backend
code will be discarded. The new backend models after the LLVM IR
because the IR looks to be designed well. That's not going to be
the same, though.