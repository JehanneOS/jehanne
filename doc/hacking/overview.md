Hacking Jehanne
===============

Jehanne is a work in progress that still needs a lot of effort to become
useful.

Here you find a quick tour of the project organization.

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
original C components **and for the kernel**.
This unfortunately means that, depending on the age of a kernel file,
you will find either the loosely followed Pike' style or my ugly
conventions.




Hacking Tools
=============

To fasten development, a set of simple tools have been created.
As tools to a greater goal, they are **disposable** and can only evolve
as required by Jehanne's development.

This document is a brief overview of such tools and it will follow
their fate.
Further help can be obtained from the [mailing list].

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

* `runqemu` runs Jehanne in a qemu instance and send commands to it.
  It is used both during compilation (to create the initial ram disk,
  for example) and to run [quality checks].
* `ksyscalls` and `usyscalls` produce the boring code for system calls,
  in kernel and in libc respectively. It reads [sysconf.json].
* `mksys` produces several headers reading from the [sysconf.json] too
* `data2c` and `elf2c` embeed in programs in kernels (mainly in the
  workhorse).
* `fetch` downloads external resources listed in [a fetch.json file]
* `telnet` can connect a running instance of Jehanne.
  It was used before drawterm was available.
* `preen` pretty print the JSON files used by `build`

Inside the development shell, these tools are available in `$PATH`.

Miscellaneous utilities
-----------------------
Among [devtools] you can also find several shell scripts and files
that are designed to be used only from the repository root.

To get a bootable usb stick (or a disk image to be used with Bochs
or Qemu) you can use:

* `./hacking/disk-create.sh` creates a raw disk image at `$DISK`
  (default `./hacking/sample-disk.img`). It uses syslinux, its bios
  files (looked up at `$SYSLINUXBIOS`) and fdisk, but it can be run as
  a user **without** `sudo`. The image will contains two separate
  partitions, one for syslinux, the kernel and the initial ram disk
  (the `dos` partition) and one for the rest of the system
  (the `plan9` partition) in a [hjfs] file system.
* `./hacking/disk-boot-update.sh` updates syslinux, the kernel and
  the initial ram disk in the appropriate partition of `$DISK`
* `./hacking/disk-update.sh file1 file2 ...`  copy the files provided
  as arguments **to** the [hjfs] partition of `$DISK`.
* `./hacking/disk-get.sh file1 files2 ...` copy the files provided as
  arguments **from** the [hjfs] partition of `$DISK`
  to `$JEHANNE/usr/glenda/tmp`.

To run the system in Qemu you can use:

* `./hacking/runOver9P.sh` that uses a 9P2000 file server running on
  the host as the root file system
* `./hacking/runDisk.sh [path/to/disk/image]` that uses the disk image
  provided (or `$DISK`) to as the root file system.

Moreover `./hacking/QA.sh` is used by `runqemu` to start the workhorse
and execute the QA checks.

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
