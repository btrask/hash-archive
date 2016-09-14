Hash Archive (v2)
=================

The Hash Archive is a website and database for mapping between hashes and URLs. See [the website](https://hash-archive.org) for more information.

This is the C rewrite of the [original Hash Archive](https://github.com/btrask/hash-archive-js), built on top of [libkvstore](https://github.com/btrask/libkvstore) for better scalability.

Building
--------

Debian/Ubuntu dependencies: `sudo apt-get install build-essentials automake autoconf libtool pkg-config cmake`

1. `git submodule init && git submodule update`
2. `./configure`
3. `DB=leveldb make`
4. `sudo make install` (also installs libressl root certs and runs setcap on binary)

