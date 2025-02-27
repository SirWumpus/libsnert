The Unmentioned LibSnert Tools
==============================

LibSnert has several command-line interface (CLI) tools that are used to test the APIs and support software, but are also useful for scripting solutions.  These tools have no documentation other than their usage descriptions and this brief overview.

Typically just type the tool name shows the options, but some that read from standard input will not display the usage unless an invalid option is given, like "-?".

So here is a quick run down of some of the more interesting tools in LibSnert that people can play with:

### ansi
A pumped up `echo(1)` that does only ANSI (vt100) terminal escape. For example:  

        $ ansi reverse say something clever normal bell lf

### bitdump
An `od(1)` like tool for displaying standard input or files as a stream of bits, instead of bytes.

### b64
Yet another Base64 encoder/decoder.  Carry over from AtariST / DOS era before Cygwin existed.

### cipher
An API and CLI for the paper & pencil cipher techniques used by VIC, SECOM, and PPN ciphers. See http://users.telenet.be/d.rijmenants/en/handciphers.htm

### clamstream
Yet another tool to stream a message to a `clamd` anti-virus server.

### convertDate
Converts textual date string into seconds; the reverse of `date(1)` and `strftime(3)`.  Supports RFC 2822, ctime, and ISO 8601 formats.

### flip
Yet another newline flipper for DOS, old Mac, and Unix.  Carry over from AtariST / DOS era before Cygwin existed.

### geturl
A simple HTTP retrieval program similar to `curl(1)` and `wget(1)`.

### inplace
A simple tool that takes a shell command line argument and one or more files to modify "inplace".  The most common example is when you want to use `sed(1)` to modify a file typically:

        sed 's/teh/the/' file >tmp
        mv tmp file

But now you can simplify the use of a temporary file with:

        inplace "sed 's/teh/the/'" file

(I know GNU sed has an option to do this, but BSD sed does not and many other command line tools can't do it.)


### ixhash
Generate iXhash checksums, typically used for anti-spam filtering and testing against iXhash blacklists.  http://ixhash.sourceforge.net/

### jspr
JSON String Path Recovery (jasper) used to query JSON elements within a `.json` file.  For real CLI power see [jq](https://stedolan.github.io/jq/).

### kvmap
Key-value map tool similar to `makemap(1)` or `postmap(1)`.  Supports a variety of formats.  See the `kvm` API, which is used by BarricadeMX and milters.

### kvmd
Key-value map extended-socketmap daemon.  Supports the same source formats as `kvmap`.

### kvmc
Key-value map extended-socketmap client. The extended socketmap protocol is outlined in [type/socketmap.txt](type/socketmap.txt).

### Luhn
Validates and generates Luhn checksums for numbers and/or text.  Think credit-card check digit.  This was the predecessor to a `Luhn.php` class I needed a few years ago for an online store.

### mailgroup
Originally intended for use with Sendmail `aliases` to provide the ability to deliver mail to the members of a system group id or name.  Could be used from the command line to broadcast mail to a group.

### mcc
Multicast/unicast cache server and control.  The API is used by BarricadeMX.

### mime
A MIME part extractor.

### myip
Simple `inetd` service reports IPv4 or IPv6 address and port number.  If the server port is 80 or any port between 8000..8999, then treat it as an HTTP request.

### natsort
Natural sort test tool sorts lines of text; https://github.com/sourcefrog/natsort.

### netcontainsip
Simple tool to test if a given IPv4 or IPv6 address is a member of the given IPv6 or IPv4 net/cidr.

### nctee
Interleave `netcat` client input with the server output.

### pad
Simple tool to add leading and trailing padding with byte and column width restrictions.

### pdq
Parallel Domain Query; does DNS lookups similar to `dig(1)`, but in a parallel manner, especially with DNS based black/white lists.  Used to test the PDQ API used by BarricadeMX and `milter-link`.

### playfair
An implementation of the paper & pencil Playfair cipher.  Can be used as a second stage substitution cipher for Victor.  See http://en.wikipedia.org/wiki/Playfair_cipher

### popin
POP3 interface mail retrieval tool.  I originally used this with `smtpout` (replaced by `smtp2`) to test a "mail send & receive circuit" with Nagios (see `mail-cycle.sh`).

### rot
Caesar cipher using English alphabet, printable ASCII, or user defined.

### secho, sechod
Secure `echo` test client and server.  Note that server can only handle a single connection at a time (no threads, no forking) since it was only intended for testing.  The client first connects in clear text and forwards standard input to the server, which in turn simply echos back the data.  The client can specify a line with ".starttls" to switch to TLS and continue to send and receive data over an encrypted channel.

### sqlargs
An `xargs(1)` like tool that queries a data source for arguments to be used in a command substitution.  Each row from the data source will invoke one instance of the command (default is to echo the row to standard output), with a limit as to how many instances of the command may be running at a time.  Currently works with `.csv` or `.sq3` files.

### show
Similar to `head(1)` and `tail(1)`.  Can highlight and/or beep when a constant pattern is seen.  Was intended for watching heavy volume logs and/or catch when a rare pattern might appear.

### sift
Log file monitoring tool.  See usage documentation and [tools/sift.cf](tools/sift.cf).

### siq
CLI version of `milter-siq`.

### smtp2
An SMTP mail engine.

### spf
SPF Classic, essentially CLI version of `milter-spiff`.  See also [mail/spf-test.sh](mail/spf-test.sh).

### TextFind
General pattern matching similar to `glob(3)`.  See the function description of `TextFind` for an overview of the supported meta-characters.

### uue
Yet another UUE encoder/decoder.  Carry over from AtariST / DOS era before Cygwin existed.

### uri
CLI version of `milter-link`.  Can also be used as a CGI, see [util/uri-cgi.txt](util/uri-cgi.txt).

### urid
Dedicated HTTP / URI server; see [util/uri-cgi.txt](util/uri-cgi.txt).

### uriFormat
CLI tool for RFC 6570 (level 3) URI template parsing and expansion.

### cmp, cksum, comm, echo, kat, tee, strings
Unix tool clones intended for use with Windows CLI when you don't have Cygwin or similar Unix tool set.

### ziplist, rarlist
ZIP & RAR file format test tools.

There are other tools lying about the LibSnert source tree, but those mentioned here are just some of the more interesting ones
that people might find useful or just curious about.  While some tools are clones of classic Unix tools, the majority can all be
built as Windows native binaries for use in Windows batch files and `cmd.exe`; ie. no `cygwin.dll` required.
