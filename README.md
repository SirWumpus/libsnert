LibSnert
========

This is the support library for many of SnertSoft's software (BarricadeMX, milters) and not really intended for use in third party projects.  It is a collection of APIs and tools that have evolved over many years.  This library is little short on explicit documentation.  Most of the documentation that does exist is found in the C header files and the command line usage of the [tools and test programs](./TOOLS.md).

The library should build for any POSIX conforming system:

        BSD: FreeBSD, NetBSD, OpenBSD
        Linux: CentOS, Debian, RHEL, Ubuntu
        Others: AIX, Solaris

The primary development environment is currently `NetBSD` and the library is not regularly tested against other OSes.  Sometimes OSes or development introduce changes that break `./configure` and/or subsequent compile/link.  Usually this is a case of missing or relocated headers and/or libraries.  Please submit a bug report with the `./configure` output and the `config.log` when this occurs.


Configuration & Build
---------------------

        mkdir -p com/snert/src
        cd com/snert/src
        git clone https://github.com/SirWumpus/libsnert.git lib
        cd lib
        autoconf -f             # If ./configure is missing or out of date.
        ./configure --help
        ./configure [options]
        make links              # Need only happen once.
        make


NOTE: LibSnert did support Windows native builds using Cygwin / MingW, but that has not been maintained in a long time and would require some work to restore.  LibSnert has yet to be built against the Windows Linux subsystem.

The `./configure` script should detect the presence of optional headers and libraries.  The `./configure` script summary will show what was found.  Some developer packages (`-dev` or `-devel`) may need to be installed:

* Lua support is not required, as it was only needed for some incomplete experimental projects.

* OpenSSL support, used by BarricadeMX, install the `openssl-dev` package.

* SnertSoft milters, install `sendmail-dev` or `milter-dev` package.  Berkeley DB support is not required (though historical and/or SQLite3 can be used), unless you want to share your mail system `.db` files with the milters or BarricadeMX, in which case you will need to install the `dbX-X.Y-dev` package matching the `X.Y` version linked to the Sendmail or Postfix binaries.  Use `ldd` to determine which Berkeley DB version is linked.

There are also handful of command-line interface tools that can optionally be installed, such is `ansi`, `cipher`, `mime`, `pdq`, `show`, `spf`, `uri`, etc.  Please see the [tool summary](./TOOLS.md).  Rather than install them all, probably better to pick and choose and install by hand.

        $ sudo make install


Building with LibSnert
----------------------

When building software using my library, add the equivalent `-I`, `-L`, and `-l` options to your compile and link lines:

        -I$(path_to)/com/snert/include
        -L$(path_to)/com/snert/lib -lsnert


Comments
--------

* LibSnert 1.x tarball includes a copy of the SQLite3 tarball in order to ensure that the library is built with threading support required by BarricadeMX.  If your OS provides a SQLite3 pre-built package that enables thread support, then you can specify `./configure --without-sqlite3` to skip LibSnert's version.

* The `com/snert/*` structure is an artifact from building alongside Snert's TickCafe (a cyber cafe access control system for Windows 2000) written in Java with some Windows JNI support.

* Currently only a static library is built, no shared library yet.


References
----------

Cygwin Unix tool chain & environment for Windows.  
<http://cygwin.org/>

Lua  
<http://www.lua.org/>

SQLite3  
<https://www.sqlite.org/>
