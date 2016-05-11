#!/bin/sh

#######################################################################
# Assert Safe Shell Environment
#######################################################################

export PATH='/bin:/usr/bin:/usr/local/bin:/usr/pkg/bin'
export ENV=''
export CDPATH=''
export LANG=C

if [ $# -lt 1 ]; then
	echo 'usage: check_myip $myip_url [$myname]'
	exit 2
fi

__myip_url="$1"
__myname="$2"

#
# Given the host's FQDN, figure out the (sub)domain and primary NS to update.
#
myname=${__myname:-$(hostname | sed 's/\(.*\)\.$/\1/')}
mydomain=$(expr $myname : '[^.]*\.\(.*\)')
myns=$(dig +short soa $mydomain | cut -d' ' -f1)
myns=$(dig +short a $myns)

#
# Find absolute path of managed-keys-directory.
#
for d in $(sed -n '/[^-]directory /s/.* "\([^"]*\).*/\1/p; /managed-keys-directory/s/.* "\([^"]*\).*/\1/p' /etc/named.conf); do
	if expr $d : '/' >/dev/null; then
		# managed-keys-directory is an absolute path already.
		keydir="$d"
	else
		# managed-keys-directory is relative to named directory.
		keydir="$keydir/$d"
	fi
done

keyfile=$(ls -1 $keydir/K${mydomain}*.key 2>/dev/null)
if [ $? -ne 0 ]; then
	echo "$keydir/K${mydomain}*.key: file not found"
	exit 1
fi

#
# The URL, like http://mx.snert.org:8008/, must reply with a text/plain answer
# containing just the IP address of the request.
#
ipnow=$(curl "$__myip_url" 2>/dev/null | tr -d '\r')

ipwas=$(cat /tmp/myip.current 2>/dev/null)

if [ "$ipwas" != "$ipnow" ]; then
	nsupdate -k $keyfile <<-EOF
		server ${myns}
		zone ${mydomain}.
		update delete ${myname}.
		update add ${myname}. 60 IN A $ipnow
		send
	EOF
	if [ $? -ne 0 ]; then
		echo "nsupdate failed"
		exit 1
	fi

	# Save for future reference.  On reboot, it will be deleted.
	echo $ipnow >/tmp/myip.current
fi

exit 0
