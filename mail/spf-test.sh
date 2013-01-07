#!/bin/ksh
#
# SPF Test Driver
#

SPF="./spf"
SPF_V="-vv"

args=`getopt av $*`
if [ $? -ne 0 ]; then
        echo 'Usage: spf-test.sh [-av]'
        exit 2
fi
set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
        -a) show_all='yes' ;;
        -v) verbose="$SPF_V" ;;
        --) shift; break ;;
        esac
        shift
done

# spf exit codes
set -A spf_exit Pass Fail None Neutral SoftFail TempError PermError

function spf_test
{
	while read expect ip helo domain txt ; do
		if expr $expect : '#' >/dev/null; then
			continue
		fi
		result=`$SPF $verbose -h $helo -t "$txt" $ip $domain | cut -f 2 -d' '`
		status=$?
		if test $expect != $result ; then
			echo "FAIL expected $expect, got $result: $SPF $verbose -h $helo -t '$txt' $ip $domain"
		elif test ${show_all:-no} = 'yes' ; then
			echo "-OK- $result : $SPF $verbose -h $helo -t '$txt' $ip $domain"
		fi
	done
}

spf_test <<EOF
Neutral 192.0.2.1 localhost spf.test.snert.net
Neutral 192.0.2.1 localhost spf.test.snert.net v=woot
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 a -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +a -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -a +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?a -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~a -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 a -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 +a -all
Fail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 -a +all
Neutral 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ?a -all
SoftFail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ~a -all
PermError 192.0.2.1 localhost spf.test.snert.net v=spf1 a/99 ?all
PermError 2001:0db8::1001 localhost spf.test.snert.net v=spf1 a//999 -all
PermError 192.0.2.1 localhost spf.test.snert.net v=spf1 a/99//999 ?all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 a/29 ?all
Fail 198.51.100.1 localhost spf.test.snert.net v=spf1 a/29 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 a//112 -all
Fail 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 a//112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 a/29//112 ?all
Fail 198.51.100.1 localhost spf.test.snert.net v=spf1 a/29//112 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 a/29//112 -all
Fail 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 a/29//112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 a:host1.spf.test.snert.net -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +a:host1.spf.test.snert.net -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -a:host1.spf.test.snert.net +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?a:host1.spf.test.snert.net -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~a:host1.spf.test.snert.net -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 a:host1.spf.test.snert.net/29 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 a:host1.spf.test.snert.net//112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 a:host1.spf.test.snert.net/29//112 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 a:host1.spf.test.snert.net/29//112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 mx -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +mx -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -mx +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?mx -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~mx -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 mx -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 +mx -all
Fail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 -mx +all
Neutral 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ?mx -all
SoftFail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ~mx -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 mx/29 -all
Fail 198.51.100.1 localhost spf.test.snert.net v=spf1 mx/29 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 mx:spf.test.snert.net -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +mx:spf.test.snert.net -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -mx:spf.test.snert.net +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?mx:spf.test.snert.net -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~mx:spf.test.snert.net -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 mx:spf.test.snert.net/29 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 mx:spf.test.snert.net//112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 mx:spf.test.snert.net/29//112 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 mx:spf.test.snert.net/29//112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 ip4:192.0.2.0/24 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +ip4:192.0.2.0/24 -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -ip4:192.0.2.0/24 +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?ip4:192.0.2.0/24 -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~ip4:192.0.2.0/24 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ip6:2001:0db8::/112 -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 +ip6:2001:0db8::/112 -all
Fail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 -ip6:2001:0db8::/112 +all
Neutral 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ?ip6:2001:0db8::/112 -all
SoftFail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ~ip6:2001:0db8::/112 -all
Pass 198.51.100.1 localhost spf.test.snert.net v=spf1 -ip4:192.0.2.0/24 ip4:198.51.100.0/24 -all
Pass 198.51.100.1 localhost spf.test.snert.net v=spf1 -ip4:192.0.2.0/24 +ip4:198.51.100.0/24 -all
Fail 198.51.100.1 localhost spf.test.snert.net v=spf1 +ip4:192.0.2.0/24 -ip4:198.51.100.0/24 +all
Neutral 198.51.100.1 localhost spf.test.snert.net v=spf1 ip4:192.0.2.0/24 ?ip4:198.51.100.0/24 -all
SoftFail 198.51.100.1 localhost spf.test.snert.net v=spf1 ip4:192.0.2.0/24 ~ip4:198.51.100.0/24 -all
Pass 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 -ip6:2001:0db8::/112 ip6:2001:0db8::ffff:0/112 -all
Pass 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 -ip6:2001:0db8::/112 +ip6:2001:0db8::ffff:0/112 -all
Fail 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 +ip6:2001:0db8::/112 -ip6:2001:0db8::ffff:0/112 +all
Neutral 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 ip6:2001:0db8::/112 ?ip6:2001:0db8::ffff:0/112 -all
SoftFail 2001:0db8::ffff:1001 localhost spf.test.snert.net v=spf1 ip6:2001:0db8::/112 ~ip6:2001:0db8::ffff:0/112 -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 ptr -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +ptr -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -ptr +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?ptr -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~ptr -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ptr -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 +ptr -all
Fail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 -ptr +all
Neutral 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ?ptr -all
SoftFail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ~ptr -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 ptr:snert.test -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +ptr:snert.test -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -ptr:snert.test +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?ptr:snert.test -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~ptr:snert.test -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ptr:snert.test -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 +ptr:snert.test -all
Fail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 -ptr:snert.test +all
Neutral 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ?ptr:snert.test -all
SoftFail 2001:0db8::1001 localhost spf.test.snert.net v=spf1 ~ptr:snert.test -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +exists:%{d} -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -exists:%{d} +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?exists:%{d} +all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~exists:%{d} +all
PermError 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%(d) -all
PermError 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{z} -all
#Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{t}.%{d} -all
#Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{r}.%{d} -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%_.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%-.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%%.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{v}.%{d} -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 exists:%{v}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{i}.%{d} -all
#Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{c}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{ir}.%{d} -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 exists:%{i}.%{d} -all
#Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 exists:%{c}.%{d} -all
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 exists:%{ir}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d128}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d4}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d3}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d2}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d1}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{dr}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d128r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d4r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d3r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d2r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{d1r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o128}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o4}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o3}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o2}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o1}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{or}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o128r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o4r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o3r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o2r}.%{d} -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 exists:%{o1r}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{s}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l128}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l4}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l3}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l2}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l1}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{lr}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l128r}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l4r}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l3r}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l2r}.%{d} -all
Pass 192.0.2.1 localhost abc.123.def@spf.test.snert.net v=spf1 exists:%{l1r}.%{d} -all
Pass 192.0.2.1 localhost abc-123-def@spf.test.snert.net v=spf1 exists:%{l-}.%{d} -all
Pass 192.0.2.1 localhost abc+123+def@spf.test.snert.net v=spf1 exists:%{l+}.%{d} -all
#Pass 192.0.2.1 localhost abc,123,def@spf.test.snert.net v=spf1 exists:%{l,}.%{d} -all
Pass 192.0.2.1 localhost abc/123/def@spf.test.snert.net v=spf1 exists:%{l/}.%{d} -all
Pass 192.0.2.1 localhost abc_123_def@spf.test.snert.net v=spf1 exists:%{l_}.%{d} -all
Pass 192.0.2.1 localhost abc=123=def@spf.test.snert.net v=spf1 exists:%{l=}.%{d} -all
Pass 192.0.2.1 localhost abc-123+def@spf.test.snert.net v=spf1 exists:%{l-+}.%{d} -all
#Pass 192.0.2.1 localhost abc+123,def@spf.test.snert.net v=spf1 exists:%{l+,}.%{d} -all
#Pass 192.0.2.1 localhost abc,123/def@spf.test.snert.net v=spf1 exists:%{l,/}.%{d} -all
Pass 192.0.2.1 localhost abc/123_def@spf.test.snert.net v=spf1 exists:%{l/_}.%{d} -all
Pass 192.0.2.1 localhost abc_123=def@spf.test.snert.net v=spf1 exists:%{l_=}.%{d} -all
Pass 192.0.2.1 localhost abc=123-def@spf.test.snert.net v=spf1 exists:%{l=-}.%{d} -all
Pass 127.0.0.2 localhost spf.test.snert.net v=spf1 exists:%{ir}.zen.spamhaus.org -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h128}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h4}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h3}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h2}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h1}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{hr}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h128r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h4r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h3r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h2r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{h1r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p128}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p4}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p3}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p2}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p1}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{pr}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p128r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p4r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p3r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p2r}.%{d} -all
Pass 192.0.2.1 putz.snert.test spf.test.snert.net v=spf1 exists:%{p1r}.%{d} -all
PermError 192.0.2.1 localhost spf.test.snert.net v=spf1 exp=A.%{d} exp=B.%{d} -all
PermError 192.0.2.1 localhost spf.test.snert.net v=spf1 redirect=A.%{d} redirect=B.%{d} -all
Pass 192.0.2.2 localhost spf.test.snert.net v=spf1 redirect=_spf.%{d}
Pass 2001:0db8::1001 localhost spf.test.snert.net v=spf1 redirect=_spf.%{d}
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 include:_spf.snert.test -all
Pass 192.0.2.1 localhost spf.test.snert.net v=spf1 +include:_spf.snert.test -all
Fail 192.0.2.1 localhost spf.test.snert.net v=spf1 -include:_spf.snert.test +all
Neutral 192.0.2.1 localhost spf.test.snert.net v=spf1 ?include:_spf.snert.test -all
SoftFail 192.0.2.1 localhost spf.test.snert.net v=spf1 ~include:_spf.snert.test -all
Fail 192.0.2.1 putz.snert.com spf.test.snert.net v=spf1 include:snert.invalid -all
Pass 192.0.2.99 putz.snert.com spf.test.snert.net v=spf1 include:_include0.%{d} -all
Pass 192.0.2.99 putz.snert.com spf.test.snert.net v=spf1 include:_include1.%{d} -all
Pass 192.0.2.99 putz.snert.com spf.test.snert.net v=spf1 include:_include2.%{d} -all
EOF

exit 0

# Issue 1.
#
# What is the expansion of %{i} for an IPv6 address?
#
# RFC 4408 section 8.1:
#
#    For IPv6 addresses, the "i" macro expands to a dot-format address; it
#    is intended for use in %{ir}.
#
# There is no example of %{i} for an IPv6. There is an example for %{ir}:
#
#    The IPv6 SMTP client IP is 2001:DB8::CB01.
#
#    IPv6:
#    %{ir}.%{v}._spf.%{d2}                               1.0.B.C.0.0.0.0.
#    0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.B.D.0.1.0.0.2.ip6._spf.example.com
#
# Does this mean %{i} is the forward version of %{ir} or the full IPv6 address in colon format? With or without leading zero?
#
# 1.  2.0.0.1.0.D.B.8.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.C.B.0.1 (forward of %{ir})
#
# 2.  2001.DB8.0.0.0.0.0.CB01 (full, using dot in place of colon)
#
# 3.  2001:DB8:0:0:0:0:0:CB01 (full, without leading zeros)
#
# 4.  2001:0DB8:0000:0000:0000:0000:0000:CB01 (full, with leading zeros)
#

# Issue 2.
#
# RFC 4408 section 8.1 permits comma (,) the list of macro replacement delimiters:
#
#    delimiter        = "." / "-" / "+" / "," / "/" / "_" / "="
#
# Yet RFC 5322 section 3.2.3 indicates that comma (,) is a special that could not be used in either a domain or address local-part.
#
# In what context was comma delimiter intended? Is it an oversight that it was original included in the set of replacement delimiters?
