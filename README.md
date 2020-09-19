[![Build Status](https://api.travis-ci.org/JehanneOS/jehanne.svg?branch=master)](https://travis-ci.org/JehanneOS/jehanne)
[![Coverity Badge](https://scan.coverity.com/projects/7364/badge.svg)](https://scan.coverity.com/projects/jehanne)

# Jehanne

Jehanne is a [simple][simplicity] operating system.

Jehanne has noble ancestors:

- most of userland tools, a lot of wisdom and some kernel modules,
  come from [9front][9front]
- the kernel is a fork of Charles Forsyth's [Plan9-9k][plan9-9k]
- most of the build system and some valuable piece of code come from [Harvey OS][harvey]

Still the project is named after a humble peasant,
the famous French heretic [Joan of Arc][arc], because it diverges deeply
from the design and conventions of its predecessors.

## Overview

This is the main repository, used to build the system as a whole:

- [arch](./arch/) contains one folder for each
  supported architecture, with specific C headers, libraries and executables
  (note that by architecture we intend any kind of physical or virtual
  machine that is able to run code, thus rc is actually an architecture)
- [sys](./sys) is the system folder
    * [include](./sys/include) contains portable C headers
    * [lib](./sys/lib) contains data and scripts used by the
      running system
    * [man](./sys/man) contains manual pages
    * [src](./sys/src) contains the sources of the system
- [doc](./doc/) contains useful documentation for Jehanne
  development
    * [license](./doc/license/) contains detailed info
      about Jehanne [licenses][lic]
    * [hacking](./doc/hacking/) contains details about how
      to build and modify Jehanne
- [hacking](./hacking) contains the utilities used to
  develop Jehanne
- [qa](./qa) contains the regression tests
- [mnt](./mnt) contains default mount targets
- [usr](./usr) contains the users' folders
- [pkgs](./pkgs) will contains the installed packages

The running system also includes supplemental folders like `/lib`,
`/cmd` and `/dev` that are bound during the boot as required.

## Build

To build Jehanne and play with it, you need to have git, golang, qemu,
gcc, binutils and bison installed.
For example on Debian GNU/Linux 10 you should be able to get going with

	sudo apt-get install git golang build-essential flex bison qemu-system autoconf autoconf-archive curl automake-1.15

After the repository clone, you can give a look with

	git submodule init                               # we have a lot of submodules
	git submodule update --init --recursive --remote --depth 1
	./hacking/devshell.sh                            # start a shell with appropriate environment
	./hacking/continuous-build.sh                    # to build everything (will take a while)
	./hacking/runOver9P.sh                           # to start the system in QEMU
	./hacking/drawterm.sh                            # to connect Jehanne with drawterm

## Hacking

Jehanne is a work in progress.
Forks and pull requests are welcome.

In [doc/hacking](./doc/hacking/) you will find all you
need to know about its principles, design and weirdness.

There's a lot of work to do, in every area of the system.

To coordinate our efforts, we use the github issues.
To discuss (and even debate) about the design and development of Jehanne
we use the [JehanneOS mailing list][mailinglist]: please join and present
yourself and your attitudes.

[simplicity]: http://plato.stanford.edu/entries/simplicity/ "What is simplicity?"
[harvey]: http://harvey-os.org "Harvey OS"
[9front]: http://9front.org/ "THE PLAN FELL OFF"
[plan9-9k]: https://bitbucket.org/forsyth/plan9-9k "Experimental 64-bit Plan 9 kernel"
[nix]: https://github.com/rminnich/nix-os
[arc]: https://en.wikipedia.org/wiki/Joan_of_Arc "Jeanne d'Arc"
[lic]: ./LICENSE.md "A summary of Jehanne licensing"
[mailinglist]: https://groups.google.com/forum/#!forum/jehanneos

