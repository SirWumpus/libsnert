#!/bin/sh

#######################################################################
# Assert Safe Shell Environment
#######################################################################

export PATH='/bin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/pkg/bin'
export ENV=''
export CDPATH=''
export LANG=C

#######################################################################
#
#######################################################################

usage()
{
	echo 'usage: attach.sh [-m mail][-r rcpts][-s subject] file ...'
	exit 2
}

__mail="noreply@$(hostname)"
__rcpt="root@$(hostname)"
__subject="Mailed File Attachments"

while getopts 'm:r:s:' opt; do
	case "$opt" in
	(m)
		__mail="$OPTARG"
		;;
	(r)
		__rcpts=$(echo $OPTARG | tr ',' ' ')
		;;
	(s)
		__subject="$OPTARG"
		;;
	(*)
		usage
	esac
done
shift $(($OPTIND - 1))
if [ $# -lt 1 ]; then
	usage
fi

now=$(date +'%s')
boundary1="--=_${now}_1"
boundary2="--=_${now}_2"
msg="/tmp/$$.eml"

cat <<EOF >$msg
Subject: ${__subject}
From: <${__mail}>
To: $(echo "$__rcpts" | sed 's/^\([^ ]*\)/<\1>/; s/ \([^ ]*\)/, <\1>/g')
Date: $(date +'%d %b %Y %H:%M:%S %z')
Message-ID: <${now}@$(hostname)>
MIME-Version: 1.0
Content-Type: multipart/mixed; boundary=${boundary1}

--${boundary1}
Content-Type: multipart/alternative; boundary=${boundary2}

--${boundary2}
Content-Type: text/plain

This message contains $# file attachments.

--${boundary2}
Content-Type: text/html

<html><body>
<em>This message contains $# file attachments.</em>
</body></html>

--${boundary2}--

EOF

for f in "$@"; do
	cat <<-EOF >>$msg
		--${boundary1}
		Content-Type: application/octet; name="$(basename $f)"
		Content-Transfer-Encoding: base64

		$(uuencode -m $f $f | sed '1d')

	EOF
done

cat <<EOF >>$msg
--${boundary1}--
EOF

sendmail ${__rcpts} <$msg
excode=$?
rm $msg
exit $excode

#######################################################################
# -END-
#######################################################################

