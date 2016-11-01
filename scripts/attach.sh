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

args=$(getopt 'm:r:s:' "$@")
if [ $? -ne 0 ]; then
        usage
fi
eval set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
        (-m)
                __mail="$2"; shift 
                ;;
	(-r)
		__rcpts=$(echo $2 | tr ',' ' '); shift
		;;
	(-s)
		__subject="$2"; shift
		;;
        (--)
                shift; break
                ;;
        esac
        shift
done
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
		Content-Type: application/octet; name="$f"
		Content-Transfer-Encoding: base64

		$(uuencode -m $f $f | sed '1d')

	EOF
done

cat <<EOF >>$msg
--${boundary1}--
EOF

sendmail ${__rcpts} <$msg
echo "Done $msg"
