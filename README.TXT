LibSnert
========

The LibSnert distribution lacks my usual documentation for the simple
fact that up until the time I wrote and distributed "A Mail Server",
this library had been an internal and personal thing. Eventually there
will be more complete documentation some day. Until then:

	Use the Source Luke!


Configuration
-------------

For unix-like boxen, just type:

	./configure
	make
	
For Windows boxes you'll need the Cygwin environment with gcc. LibSnert
can also be built using Cygwin with Borland C++ 5.5 CLI tools free from
their site (after you get a free login). Setting up Cygwin or Borland
C++ is outside the scope of this document.

To build for Cygwin with gcc: 

	./configure
	make build

To build native Windows apps. using Cygwin, Bcc 5.5:

	./configure --enable-bcc32
	make build

To build native Windows apps. using Cygwin, Gcc, and Mingw: 

	./configure --enable-win32
	make build

There should be no errors or warnings.  Please report any found.

Note that there is no installation beyond the above commands. This is
because any applications I write to use LibSnert will sit within the
source tree hierarchy and know how to find and link with LibSnert. I'm
not yet at the stage were I want LibSnert to be install in /usr/local/lib,
since I'm not ready to support other people's use of my library.


Quick Test
----------

The Dns client test program takes an record type, domain name, and
and optional list of DNS server IP addresses to consult. The supported
record types are A, AAAA, CNAME, MX, NS, PTR, SOA, and TXT:

	io/Dns mx yahoo.com 192.168.1.1

There are some other test programs like:
	
	crc/Luhn
	mail/parsePath
	util/Text
	util/Memory	
	
The executable crc/crctable.exe is not a test program, but a generator
used to create *.tbl files according to some function. Try:

	crc/crctable -h

There are also some extra tools found in ../tools:

	ansi		an enhanced ANSI capable echo for scripting.
	cksum		POSIX 32-bit CRC and friends
	climits		figure out some things about the C compiler.
	cmp		compare files
	comm		compare two sorted files line by line
	echo		a classic echo program
	flip		flip newline style of text files (Mac, PC, Unix)
	kat		another cat like utility (kat -? for options)
	mailout		an SMTP client for scripting
	show		a combo head & tail like program with highlighting
	strings		a simple strings implementation
	uue		base64, binhex, uuencode encoder / decoder
	filed		inetd server
	zoned		inetd server
	popin		scriptable POP3 mail fetch tool
	smtpout		scriptable SMTP mail out tool
	mimepart	extract MIME parts
	range 		simple number list generator
	rawls		simple ls when directories have too many files
	rawrm		simple rm when directories have too many files

Just type the program without options or arguments to get the usage summary.


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


Free command line tools and debugger.
	http://www.borland.com/
	http://www.borland.com/products/downloads/download_cbuilder.html


Anthony Howe
27 June 2005


-END-
