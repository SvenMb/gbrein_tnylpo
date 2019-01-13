# tnylpo
## What is this?
`tnylpo` allows the execution of programs written for CP/M-80
version 2.2 under Unixy operating systems. It has been tested
under Linux (i386 and amd64 Debian 8 & 9) and Mac OS X (10.13),
but should need little to no modifications to run under any other
reasonable recent system. The companion program `tnylpo-convert`
converts text files to and from the CP/M format.
## More details, please!
Read the included man page, `tnylpo.1`; you can format and read
it with the command
```
nroff -man tnylpo.1 | less
```
## What is a man page? And what is CP/M-80?
Move on, there is nothing to see for you. Go play with your
smartphone, kiddy.
## No, seriously, who needs this?
I don't know, but it was fun to write; I guess there are quite
a lot of CP/M emulators out there, some of them likely
more mature and richer in features, but I don't care.
## How do I build it?
Make sure to have the `ncurses` library and its headers installed
(I used version 5.9). You'll need a C compiler supporting the C99
standard. The `makefile` contains GNU `make` features, so you'll
need GNU `make` to use it (but then it is trivial and short enough that you
can easily modify it to suit your favourite `make` utility).

Building itself is as easy as entering
```
make
```
## How do I install it?
Copy the resulting binaries `tnylpo` and `tnylpo-convert` to a
directory in your `PATH`
(e. g. `/usr/local/bin`) and the man pages `tnylpo.1` and
`tnylpo-convert.1` to
an appropriate directory in your `man` hierarchy (e. g.
`/usr/local/share/man/man1`).
```
man tnylpo
```
will then tell you how to get your CP/M-80 programs running.
## What is the legal situation?
`tnylpo` is Open Source under a BSD-style license (see `LICENSE`);
apart from that, feel free to use it, modify it, or sell it for big
money to whoever is stupid enough to buy it. This program was written
by me from scratch and doesn't contain any third party code.
## Who wrote this crap?
Georg Brein. If you cannot resist the urge to to contact me
about `tnylpo`, send mail to `tnylpo@gmx.at`.
