[![Build Status](https://api.travis-ci.org/JehanneOS/jehanne.svg?branch=master)](https://travis-ci.org/JehanneOS/jehanne)
[![Coverity Badge](https://scan.coverity.com/projects/7364/badge.svg)](https://scan.coverity.com/projects/jehanne)

# Jehanne

Jehanne is a [simple][simplicity] operating system. 

It is a fork of [Harvey][harvey] (which in turn is a fork of 
[Plan 9 from Bell Labs][plan9] merged with [Nix][nix] sources) but
diverges from the design and conventions of its ancestors whenever
they are at odds with its goals.

For this reason project is named after the famous French heretic [Joan of Arc][arc].  

## Overview

This is the main repository, used to build the system as a whole:

- [arch](./arch/) contains one folder for each
  supported architecture, with specific C headers and binaries
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
`/bin` and `/dev` that are bound during the boot as required.

## Build

To build Jehanne and play with it, you need to have git, golang, qemu, 
gcc, binutils and bison installed. 
For example on Debian GNU/Linux you should be able to get going with

	sudo aptitude install git golang build-essential bison qemu-system

After the repository clone, you can give a look with

	git submodule init                               # we have a lot of submodules
	git submodule update --init --recursive --remote
	./hacking/devshell.sh                            # start a shell with appropriate environment
	./hacking/continuous-build.sh                    # to build everything
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
[plan9]: https://github.com/brho/plan9 "UC Berkeley release of Plan 9 under the GPLv2"
[nix]: https://github.com/rminnich/nix-os
[arc]: https://en.wikipedia.org/wiki/Joan_of_Arc "Jeanne d'Arc"
[lic]: ./LICENSE.md "A summary of Jehanne licensing"
[mailinglist]: https://groups.google.com/forum/#!forum/jehanneos
