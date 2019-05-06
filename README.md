# tnylpo
## What is this?
`tnylpo` allows the execution of programs written for CP/M-80
version 2.2 under Unixy operating systems. It has been tested
under Linux (Debian 8 i386, Debian 9 amd64, Ubuntu 18.04.1 LTS amd64,
CentOS 7),
FreeBSD (12.0 amd64), NetBSD (8.0 i386, sparc, vax), OpenBSD (6.4 i386),
Solaris (10 x86, 32 and 64 bit), and Mac OS X (10.9, 10.13),
but should need little to no modifications to run under any other
reasonable recent system. The companion program `tnylpo-convert`
converts text files to and from the CP/M format.
## More details, please!
Read the included man page, `tnylpo.1`; you can format and read
it with the command
```sh
nroff -man tnylpo.1 | less
```
## What is a man page? And what is CP/M-80?
Move on, there is nothing to see for you. Go play with your
smartphone, kiddy.
## What makes this program special?
I'm not the right person to tell, since I didn't do much
research into other CP/M emulators, so I cannot tell you what makes
`tnylpo` stand out from the crowd (and there is quite a crowd of
CP/M emulators, see e. g. the [emulators page on Thomas Scherrer's Z80
pages](http://www.z80.info/z80emu.htm)).

I wrote `tnylpo` for my own use, and I primarily wanted
to run CP/M compilers and assemblers on more recent machines. Since this is my
preferred working environment, I wanted my emulator to integrate CP/M
software as tightly as possible into the Unix command line.
Consequently, if you are in search of the authentic look-and-feel of
CP/M back in its heyday, `tnylpo` is probably not what you are
looking for. If on the other hand you want to play with old CP/M source
code and compilers without having access to a CP/M computer, if you do not
want to wait for the ages it takes the tired iron to create an executable,
or if you simply prefer to edit your CP/M sources with your favourite
Unix editor instead of CP/M's `ed`,
you might find `tnylpo` useful. Likewise, it may be the right tool if
you need to access e. g. old dBase II databases or WordStar text files,
especially if they contain data in some half-forgotten ASCII variant.

In short, `tnylpo`
* supports the full Z80 instruction set (including undocumented
instructions and features)
* provides a TPA size of 63.5KB
* maps CP/M file operations to operations on files in the Unix file
system
* allows you to map up to 16 CP/M drives to arbitrary Unix directories
* supports read-only drives
* maps the character set of the Unix locale (usually UTF-8) to
a user-configurable single byte CP/M character set for console (and
optionally, for printer, punch, and reader) I/O
* provides a built-in curses based emulation of the DEC VT52
terminal for full-screen applications (with extensions such as
eight bit characters, an alternate character set, insert/delete line
commands, character attributes, and VT100 box drawing characters)
* shields CP/M programs from terminal window resizing
* allows to combine CP/M programs with Unix shell redirections and pipelines.
## How do I build it?
Make sure you have a version of the `ncurses` library supporting
wide characters and its headers installed
(I used version 5.9). You'll need a C compiler
supporting the C99
standard. The `makefile` contains GNU `make` features, so you'll
need GNU `make` to use it (but then it is trivial and short enough that you
can easily modify it to suit your favourite `make` utility).

Building itself (at least on tested platforms) is as easy as entering
```sh
make
```
(resp. `gmake` on platforms with a non-GNU primary `make` utility).
## How do I install it?
Copy the resulting binaries `tnylpo` and `tnylpo-convert` to a
directory in your `PATH`
(e. g. `/usr/local/bin`) and the man pages `tnylpo.1` and
`tnylpo-convert.1` to
an appropriate directory in your `man` hierarchy (e. g.
`/usr/local/share/man/man1`).
```sh
man tnylpo
```
will then tell you how to get your CP/M-80 programs running.

The subdirectory `mine` contains a simple text-based CP/M game which
can be used to test/demonstrate `tnylpo`.
## What is the legal situation?
`tnylpo` is Open Source under a BSD-style license (see `LICENSE`);
apart from that, feel free to use it, modify it, or sell it for big
money to whoever is stupid enough to buy it. This program was written
by me from scratch and doesn't contain any third party code.
## Who wrote this crap?
Georg Brein. If you cannot resist the urge to to contact me
about `tnylpo`, send mail to `tnylpo@gmx.at`.
