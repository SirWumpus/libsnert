#!/bin/sh

export MALLOC_OPTIONS="AFJR"

# ; For testing circular references in DNS code.
# circular0               IN CNAME        circular0
# 
# circular1               IN CNAME        circular2
# circular2               IN CNAME        circular3
# circular3               IN CNAME        circular1
# 
# chain1                  IN CNAME        chain2
# chain2                  IN CNAME        chain3
# chain3                  IN CNAME        pop

touch $$.txt
cat <<'EOT' >>$$.txt 
----
Test simple circular reference in CNAME:

circular0               IN CNAME        circular0
	
Expect:
DNS circular reference (3)

EOT
echo ./Dns a circular0.snert.net >>$$.txt 2>&1
./Dns a circular0.snert.net >>$$.txt 2>&1


cat <<'EOT' >>$$.txt 
----
Test circular reference in CNAME loop:

circular1               IN CNAME        circular2
circular2               IN CNAME        circular3
circular3               IN CNAME        circular1
	
Expect when upstream server is BIND 9:
DNS circular reference (3)

Expect when upstream server is BIND 4:
DNS server failure (2)

EOT
echo ./Dns a circular1.snert.net >>$$.txt 2>&1
./Dns a circular1.snert.net >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test a CNAME chain:

chain1                  IN CNAME        chain2
chain2                  IN CNAME        chain3
chain3                  IN CNAME        pop

EOT
echo ./Dns a chain1.snert.net >>$$.txt 2>&1
./Dns a chain1.snert.net >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test a PTR lookup of reverses IP address:

EOT
echo ./Dns ptr 34.10.97.82.in-addr.arpa >>$$.txt 2>&1
./Dns ptr 34.10.97.82.in-addr.arpa >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test a PTR lookup of normal IP address:

EOT
echo ./Dns ptr 193.41.72.72 >>$$.txt 2>&1
./Dns ptr 193.41.72.72 >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test lookup with root in domain name:

EOT
echo ./Dns a www.snert.com. >>$$.txt 2>&1
./Dns a www.snert.com. >>$$.txt 2>&1


cat <<'EOT' >>$$.txt 
----
Test lookup short TXT record:

EOT
echo ./Dns txt snert.net >>$$.txt 2>&1
./Dns txt snert.net >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test lookup long/multiple TXT records:

EOT
echo ./Dns txt aol.com >>$$.txt 2>&1
./Dns txt aol.com >>$$.txt 2>&1


cat <<'EOT' >>$$.txt 
----
Test lookup MX with bogus host name:

EOT
echo ./Dns mx zensearch.net >>$$.txt 2>&1
./Dns mx zensearch.net >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test lookup of localhost.

EOT
echo ./Dns a localhost >>$$.txt 2>&1
./Dns a localhost >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test lookup of 127.0.0.1.

EOT
echo ./Dns a 127.0.0.1 >>$$.txt 2>&1
./Dns a 127.0.0.1 >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test lookup of [127.0.0.1].

EOT
echo ./Dns a '[127.0.0.1]' >>$$.txt 2>&1
./Dns a '[127.0.0.1]' >>$$.txt 2>&1

cat <<'EOT' >>$$.txt 
----
Test lookup of thaiyahoo.com, which has a bogus result 
that specifies the root domain for its MX:

thaiyahoo.com.          IN MX 0 	. 

EOT
echo ./Dns mx thaiyahoo.com >>$$.txt 2>&1
./Dns mx thaiyahoo.com >>$$.txt 2>&1

# What about MX bsd.hu ?? Anything funny with the result from that?

diff -bu Dns-expect.txt $$.txt
rm -i $$.txt

