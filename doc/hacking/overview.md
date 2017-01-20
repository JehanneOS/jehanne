---
title: Hacking Jehanne
---

Jehanne is a work in progress that still needs a lot of effort to become
useful.

Here you find a quick tour of the project organization.

Further help can be obtained from the [mailing list]: you are welcome
to challenge my assumptions.

Coding Styles
=============
Jehanne is a small operating system, but it's composed of a kernel, a
few libraries and a minimal set of commands coded in C.

Most of libraries and commands come either from Bell Labs' Plan 9 or
from 9front, and thus they loosely follow the coding conventions
described in the [style(6)] manual page there.
It's wise to stick to that convention for such code, in the hope to
share improvements between the projects in a friendly manner.

Other libraries and commands may come from the Unix ecosystem:
we stick with the existing conventions there too.

Here I describe the **arbitrary** conventions that are followed for
new original C components **and for the kernel**.
This unfortunately means that, depending on the age of a kernel file,
you will find either the loosely followed Pike' style or my ugly
rules and conventions.

Fortunately, if you contribute a new interesting program, you are free
to choose your favourite coding style, declare it clearly in a README
file and stick with it. Note however that this won't apply to libraries.

Use good sense
--------------

> Le bon sens est la chose du monde la mieux partagée; car 
> chacun pense en être si bien pourvu, que ceux même qui sont les
> plus difficiles à contenter en toute autre chose n’ont point coutume
> d’en désirer plus qu’ils en ont.  
>
> -- [René Descartes], [Discours de la méthode]

In Jehanne good sense is both **strictly enforced** and **loosely defined**.  
This way nobody will try to sell you books, lectures or TED conferences
about it.

These are my rules of thumb:

**Keep it simple**  
: Never add features just because you can. Remove redundant features.
  Decouple unrelated features. Use obvious names for files and folders.

**Encapsulate**  
: Use properly scoped functions to access structures' members.

**Do not abstract**  
: Replace abstractions used less than 3 times. Remove unused code.

Aestetics
---------

I do not care too much about aestetics, but readability matters.  
Unfortunately, just like any other programmer, what I find readable
largely depends on the code that I had to debug in the past.

The conventions I try to honor are:

1.  Tabs are 8 characters.

2.  Lines should be no longer than readable. You can use macros to
    improve readability.

3.  Format blocks like these:

    ```c
        if(x == nil)
        	do_something();
        
        if(x == y){
        	...
        } else {
        	...
        }
        
        switch(v){
        case AnOption:	
        	...
        	break;
        case AnotherOption:	
        	...
        	break;
        default:
        	...
        	break;
        }
    ```

4.  Format functions like this:

    ```c
        /* will wlock/wunlock pool_lock */
        static void
        freelist_add(ImagePointer ptr, ElfImage *img)
        {
        	...
        }
    ```

5.  Use one space around `=`  `+`  `-`  `<`  `>`  `*`  `/`  `%`  
    `|`  `&`  `^`  `<=`  `>=`  `==`  `!=`  `?`  `:`, but no space between
    unary operators (`&`  `*`  `+`  `-`  `~`  `!`  `sizeof`  `typeof`
    `alignof`  `__attribute__`  `defined`  `++`  `--`) and their 
    operand, and obviously no space around the `.` and `->` structure
    member operators

6.  Use short names in local variables and module functions when the
    meaning is obvious in the context using them (`tmp`, `i`, `j`).  

7.  Use descriptive names for globally visible functions and variables
    (eg `proc_segment_detach`). In Jehanne's kernel a few frequently
    used global variables are allowed to violate this rule: 
    `up` (current user process), `m` (current processor) and `sys`.
    
8.  Use `typedefs` for struct and enums (CamelCase) but not for pointers.

9.  Functions should be short, do one thing, hold few local variables
    and `goto` a centralized cleanup section on error.
    Keep in mind errors when designing the return values of your functions.  
    Use Plan9's `error()` machinery only in functions directly called by
    other modules (like `Dev` methods and exported ones), not just
    to easily unroll the stack.

Hacking Tools
=============

To fasten development, a set of simple tools have been created.
As tools to a greater goal, they are **disposable** and can only evolve
as required by Jehanne's development.

Development Environment
-----------------------
Currently, Jehanne is coded and cross compiled from Linux (I work on a
stable Debian GNU/Linux).

To build it you need `bash`, `git`, `golang`, `build-essential` `flex`,
`bison` and `qemu-system`.

Inside the root of the repository you can enter the development
environment with `./hacking/devshell.sh` that will start a new Bash:

* your `$PS1` will be prepended with "JehanneDEV "
* the environment variable `$JEHANNE` will hold the path of the root
  of the respository
* the environment variable `$ARCH` will be "amd64" (aka x86_64, the
  only supported architecture so far)
* `$PATH` will include `$JEHANNE/hacking/bin` and
  `$JEHANNE/hacking/cross/toolchain/bin`
* the environment variable `$TOOLPREFIX` will contain the prefix of
  the cross compiling toolchain

`devshell.sh` also gives you an hook to customize your development
environment without touching the repository: if the
`$JEHANNE_DEVELOPER_DIR` (default: `~/.jehanne/`) exists and contains 
a script named `devshell.sh`, such script will be sourced.
For example my own `devshell.sh` starts a couple of terminals.

To build the cross compiler you "only" need to run
`(cd $JEHANNE/hacking/cross/; ./init.sh; git clean -xdf src/)`.
It will automatically download and compile Binutils and GCC
(and their obsolete build dependencies).
The process requires around 30 minutes (depending on your hardware)
and 4 GB of free disk space, but fortunately it's seldom required
during development.

All other development tools can be built with the command
`./hacking/buildtools.sh`.
You can also invoke it with `--no-drawterm` or `--no-tools`: since
building drawterm is slower than all the other tools together, I
usually build it alone.

The build system
----------------
Jehanne's build system is an evolution of the Harvey's original one
based on Go and Json. It violates the [principle of least surprise],
so that [I was originally pretty skeptic about it], but it turns out
that [Aki was right]: a general purpose language provide both power
and painless evolution to a build system.

Thus, to build Jehanne you use the `build` commands.
Its source code is at `./hacking/src/jehanne/cmd/` and its documantation
can be obtained with `build --help`.

It consumes small JSON files (usually named `build.json`) describing the
build process. Some example to get started are [the commands one] and
[the libc one].

When I need to rebuild the entire system (for example after a
change to libc) I simply run `cd $JEHANNE; git clean -xdf . && build`.

However you can always build just a specific component
(or component set), for example `build sys/src/cmd/rc/`
or `build sys/src/cmds.json Cmds` or even
`cd sys/src/cmd/hmi/console/ && build screenconsole.json && cd -`.

Note that the Jehanne's build system does not track or enforce
dependencies or file changes: it always run the **entire build**
described in the provided JSON **and nothing more**. Thus it's simple,
fast enough and fully **predictable**.

Qemu (and friends)
------------------
Jehanne has been tested on Qemu, Bochs, VMVare and Hyper-V, but the day
to day testing is done with Qemu.

To run the system in Qemu you can run:

`./hacking/runOver9P.sh`
: that connects a 9P2000 server running on the linux host 
  to mount `$JEHANNE` as the root file system

`./hacking/runDisk.sh [path/to/disk/image]`
: that uses the disk image
  provided (or `$DISK`) to as the root file system

`./hacking/QA.sh` 
: used by `runqemu` to start the workhorse or to execute the QA checks
  (it should not be executed directly).

These scripts react to a few environment variables:

`$KERNEL`
: kernel to load (default: `jehanne.32bit`)

`$KERNDIR`
: directory containing $KERNEL (default: `$JEHANNE/arch/$ARCH/kern/`)

`$KAPPEND`
: additional parameters for the kernel

`$NCPU`
: number of simmetric processors to use

Qemu will multiplex the terminal I/O between Jehanne's serial console
and Qemu monitor. To switch between the two use `Ctrl-a x`. 
To stop Qemu use `Ctrl-a c`.

To create or update a bootable usb stick (or a disk image to be used
with Bochs or Qemu) you can use:

`./hacking/disk-create.sh` 
: creates a raw disk image at `$DISK`
  (default `./hacking/sample-disk.img`). It uses syslinux, its bios
  files (looked up at `$SYSLINUXBIOS`) and fdisk, but it can be run as
  a user **without** `sudo`. The image will contains two separate
  partitions, one for syslinux, the kernel and the initial ram disk
  (the `dos` partition) and one for the rest of the system
  (the `plan9` partition) in a [hjfs] file system.

`./hacking/disk-boot-update.sh`
: updates syslinux, the kernel and
  the initial ram disk in the appropriate partition of `$DISK`

`./hacking/disk-update.sh file1 file2 ...`
: copy the files provided as arguments **to** the [hjfs] partition of `$DISK`.

`./hacking/disk-get.sh file1 files2 ...`
: copy the files provided as
  arguments **from** the [hjfs] partition of `$DISK`
  to `$JEHANNE/usr/glenda/tmp`.

Note that **the whole process does NOT require root privileges**:
you don't need to trust Jehanne's developers but you have to `dd` the
usb stick yourself.

Debugging
---------
Once you get used to the codebase, debugging Jehanne is pretty simple.

First start the system in Qemu with either `./hacking/runOver9P.sh` or 
`./hacking/runDisk.sh`. If `$KAPPEND` contains the string "waitgdb",
Jehanne will stop at an early stage after the boot and will wait for a
gdb connection.

To start such connection you can use the script `./hacking/gdb.sh` that
will provide you a small but useful set of functions to ease your session:

`jhn-connect [host:port]`
: will connect to host:port (default localhost:1234); it's better than
  a simple `target remote :1234` because it integrates well with
  the `waitgdb` kernel argument and it is faster to type (jh TAB c TAB)

`jhn-log-syscalls`
: will log all syscalls.

`jhn-log-errors`
: will log errors

`jhn-break-cmd arch/amd64/path/to/cmd "cmd" [address]`
: will set a breakpoint at the provided address in the user space
  program named "cmd" (default address: `0x4000c0`, aka `_main`)

`jhn-break-pid arch/amd64/path/to/cmd pid [address]`
: will set a breakpoint at the provided address in the user space
  program running at pid (default address: `0x4000c0`)

Note how **in Jehanne you can debug any program or library running in
user space** with few simple gdb functions.

If `$JEHANNE_DEVELOPER_DIR/gdbinit` exists it is sourced, providing
another hook to ease your debug as you like.

If `$JEHANNE_GDB_LOGS` is defined the whole session will be logged there,
prepended with the current commit hash and a brief summary of the
repository status. 

The workhorse
-------------
A simplified kernel is built before the others: [the workhorse].

It's used during the compilation, whenever we need to run a Jehanne's
program that is impractical to port to Linux. For example it's run in a
qemu instance to create the initial ram disk for the other kernels.

Custom Go tools
---------------
Here is a brief summary of the other custom tools in
`./hacking/src/jehanne/cmd/`:

`runqemu` 
: runs Jehanne in a qemu instance and send commands to it.
  It is used both during compilation (to create the initial ram disk,
  for example) and to run [quality checks].

`ksyscalls` and `usyscalls`
: produce the boring code for system calls,
  in kernel and in libc respectively. It reads [sysconf.json].

`mksys`
: produces several headers reading from the [sysconf.json] too

`data2c` and `elf2c`
: embeed in programs in kernels (mainly in the workhorse).

`fetch`
: downloads external resources listed in [a fetch.json file]

`telnet`
: can connect a running instance of Jehanne.
  It was used before drawterm was available.

`preen`
: pretty print the JSON files used by `build`

Inside the development shell, these tools are available in `$PATH`.

Miscellaneous utilities
-----------------------
Among [devtools] you can also find several shell scripts and files
that are designed to be used only from the repository root.

In the default configuration (see [cfg/startup]), Jehanne is started as
a cpu server owned by glenda. You can connect it with the
`./hacking/drawterm.sh` script. The password is "demodemo".

Finally `./hacking/continuous-build.sh` and
`./hacking/coverity-scan.sh` are used during continuous build.

Third parties
-------------
In the hacking/third_party directory you can file the
[Go 9P2000 server] used during development and [drawterm], both
downloaded as git submodules and compiled by `./hacking/buildtools.sh`.

[mailing list]: https://groups.google.com/forum/#!forum/jehanneos
[principle of least surprise]: https://en.wikipedia.org/wiki/Principle_of_least_astonishment
[I was originally pretty skeptic about it]: https://groups.google.com/d/msg/harvey/IwK8-gebgyw/vxCPQVaGBAAJ
[Aki was right]: https://groups.google.com/d/msg/harvey/IwK8-gebgyw/vxCPQVaGBAAJ
[the commands one]: https://github.com/JehanneOS/jehanne/blob/master/sys/src/cmd/cmds.json
[the libc one]: https://github.com/JehanneOS/jehanne/blob/master/sys/src/lib/c/build.json
[sysconf.json]: https://github.com/JehanneOS/jehanne/blob/master/sys/src/sysconf.json
[a fetch.json file]: https://github.com/JehanneOS/devtools/blob/master/cross/src/fetch.json
[quality checks]: https://github.com/JehanneOS/jehanne/tree/master/qa
[the workhorse]: https://github.com/JehanneOS/jehanne/blob/master/sys/src/kern/amd64/workhorse.json
[devtools]: https://github.com/JehanneOS/devtools/
[hjfs]: http://man2.aiju.de/4/hjfs
[cfg/startup]: https://github.com/JehanneOS/jehanne/tree/master/cfg
[Go 9P2000 server]: https://github.com/lionkov/ninep
[drawterm]: https://github.com/0intro/drawterm
[style(6)]: http://man.cat-v.org/9front/6/style
[encapsulation]: http://www.tonymarston.co.uk/php-mysql/abstraction.txt
[René Descartes]: https://en.wikipedia.org/wiki/Ren%C3%A9_Descartes
[Discours de la méthode]: http://www.gutenberg.org/files/59/59-h/59-h.htm
