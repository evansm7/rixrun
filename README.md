# Rixrun

v0.1 19 Feb 2022


Run Acorn RISCiX binaries from the comfort of your own laptop!
This program is a simple userspace instruction/syscall emulator that
can run _unsqueezed_ RISCiX SPZMAGIC binaries.

It uses ARMulator to run arm26 code, and with some shockingly-simple
SWI syscall emulation it's able to run some basic programs.

My personal motivation for this is to run the RISCiX compilers on Linux,
so programs/drivers can be "cross-developed" without a cross-compiler.  To
that end, rixrun can now run cc and ld:

```
$ export RIX_ROOT=/path/to/riscix/
$ rixrun usr/lib/cc -help
Norcroft Unix ARM C 345/345/345/dbx [Oct 29 1990, 12:46:07]

Usage :       ./cc [-options] file1 file2 ... filen

Main options:

-ansi          Compile ANSI-style C source code
-pcc           Compile BSD UNIX PCC-style C source code
-c             Do not invoke the linker to link the files being compiled
-C             Prevent the preprocessor from removing comments (Use with -E)
-D<symbol>     Define preprocessor <symbol> on entry to the compiler
-E             Preprocess the C source code only, do not compile or link it
-F<options>    Enable a selection of compiler defined features
-g             Generate code that may be used with the debugger
-I<directory>  Include <directory> on the '#include' search path
-list          Generate a compilation listing
-M<options>    Generate a 'makefile' style list of dependencies
-o <file>      Instruct the linker to name the object code produced <file>
-O             Invoke the object code improver
-p<options>    Generate code to produce 'profile' information
-R             Place all compile time strings in a 'Read only' segment
-S             Generate ARM assembly code instead of object code
-U<symbol>     Undefine preprocessor <symbol> on entry to the compiler
-w<options>    Disable all or selected warning and error messages
```

This tool can compile and link a simple helloworld.c into a working binary.  Note due to the current vfork/execve limitation (below) you can't use the `cc foo.c -o some_binary` syntax, but instead need to invoke `ld` manually.  For example:

```
$ cd $RIX_ROOT
$ rixrun usr/lib/cc -Iusr/include -c hellow.c
$ rixrun usr/bin/ld hellow.o usr/lib/crt0.o -Lusr/lib -lc -o hellow
$ rixrun ./hellow
Hello world!
```

## Limitations

Many!  Only a handful of very simple syscalls are implemented so far:

   * IOCTL is all fake, no fancy terminal use will work
   * No networking
   * No form of fork/vfork/execve

`cc` will try to vfork/execve `ld`, which will dump an error message (with attempted args).
Build flows that use `cc -c` plus `ld` don't do this, and will work.


## Usage

### Environment variables

`RIX_ROOT` indicates the host path to a RISCiX installation, and is used to locate shared libraries.

All other paths (e.g. from command-line args) are used verbatim, without any 'chrooting' applied.

`RIX_VERBOSE` can be set to `1` or `2` for increasing debug output:  syscall
trace and instruction execution trace.


### Squeezedness

RISCiX binaries are by default squeezed.  (If you haven't already, go read about their novel
on-demand executable decompression, it's really cool!)  AFAIK the algorithm isn't documented anwhere, and I haven't reverse-engineered the algorithm for this,
so the current ZMAGIC loader can only load unsqueezed binaries.

As a workaround, this tool can execute an already-unsqueezed `unsqueeze` binary, in order to
prepare any given binary for execution.  Solve the chicken & egg by unsqueezing `unsqueeze` from RISCiX itself.

Note: `unsqueeze` writes to a temporary file and then attempts to execute `cp` to move the output
for some reason (which isn't supported).  The output needs to be manually copied to the right place.


# Licence

Copyright 2022 Matt Evans (and ARMulator authors, and small portions borrowed from QEMU's linux-user binary loaders).

This program is licenced under GPLv2.
