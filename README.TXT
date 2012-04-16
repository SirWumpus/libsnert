LibSnert
========

The LibSnert distribution lacks my usual documentation for the simple
fact that up until the time I wrote and distributed "A Mail Server",
this library had been an internal and personal thing. Eventually there
will be more complete documentation some day. Most of the documentation
that does exist is found in the C header files and the command line
usage of the tools and test programs.

This library is for use with SnertSoft software and not intend for use
in third party projects, since the APIs have been know to change from
time to time.


Configuration
-------------

For unix-like boxen, just type:

	./configure
	make
	
For Windows boxes you'll need the Cygwin environment with gcc and MingW.

To build for Cygwin with gcc: 

	./configure
	make build

To build native Windows apps. using Cygwin, Gcc, and Mingw: 

	./configure --enable-mingw
	make build

There should be no errors or warnings.  Please report any found.

There are a handful of command-line interface tools that can be installed,
such is ansi, cipher, mime, pdq, show, uri, etc. Please see TOOLS.TXT for 
a summary. This is optional.

	sudo make install


Building with LibSnert
----------------------

When building software using my library, add the equivalent -I, -L, and -l
options to your compile and link lines:

	-I$(path_to)/com/snert/include
	-L$(path_to)/com/snert/lib -lsnert


References
----------

Cygwin Unix environment for Windows.
	http://sources.redhat.com/cygwin/


Anthony Howe
16 April 2012


-END-
