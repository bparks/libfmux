libfmux
=======

libfmux is a very simple protocol and library for multiplexing several communication
channels (and "conversations") over a single file handle. It is typically assumed
that this is a **bi-directional** link (e.g. a socket), but the library and
protocol would work equally well on top of a pipe or other file handle.

The library is built on top of the I/O functions in `unistd.h` and provided by
the Standard C Library to allow for the widest possible accessibility and adoption.

The protocol is inspired (in concept but not implementation) by the likes of
FastCGI and systemd's journal functionality.

The library IS thread-safe, using pthread mutexes on Linux and OS X and TODO on
Windows.

LICENSE
-------

This software is licensed under the GNU LGPL v3. Specifically, linking against
this library is permitted in ALL cases, even commercially.
