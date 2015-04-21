#!/bin/ksh
. log.sh

__port=25
__host="127.0.0.1"


args=$(getopt 'h:p:v' $*)
if [ $? -ne 0 ]; then
        log_exit $EX_USAGE 'Usage: grey-test.sh [-v][-h host][-p port] helo mail rcpt'
fi
set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
	-h) __host=$2; shift ;;
	-p) __port=$2; shift ;;
        -v) __verbose=true ;;
        --) shift; break ;;
        esac
        shift
done

__helo=$1
__mail=$2
__rcpt=$3
__ip=$(dig +short a $__helo)

: ${NETCAT:=$(which ncat)}		# Nmap version
: ${NETCAT:=$(which netcat)}		# GNU Linux version
: ${NETCAT:=$(which nc)}		# Original 1.10
if [ -z "$NETCAT" ]; then
	log_exit $EX_ALERT "NETCAT path is undefined"
fi

: ${SMTP_SESSION:=$NETCAT -w 5 $__host $__port}
: ${ISO_DATE:=$(date +'%Y%m%d')}
: ${ISO_TIME:=$(date +'%H%M%S%z')}
: ${RFC_DATE:=$(date +'%e %b %Y %H:%M:%S %z')}


function smtp_session
{
	$SMTP_SESSION <<-EOF
		XCLIENT NAME=$__helo ADDR=$__ip
		HELO $__helo
		MAIL FROM:<$__mail>
		RCPT TO:<$__rcpt>
		DATA
		Date: $RFC_DATE
		Message-ID: <${ISO_DATE}T${ISO_TIME}@${__helo}>
		Subject: grey list
		From: <$__mail>
		To: <$__rcpt>

		Test grey listing
		$@
		.
		QUIT
	EOF
}

function smtp_settings
{
	typeset period=$1
	typeset ttl=$2

	$SMTP_SESSION <<-EOF
		OPTN grey-temp-fail-period="$period"
		OPTN grey-temp-fail-ttl="$ttl"
		QUIT
	EOF
}


# Short grey listing settings
log_info "# setting grey period=120, ttl=300"
smtp_settings 120 300

log_info "# initial connection"
smtp_session

log_info "# wait 20s"
sleep 20
log_info "# reconnect during grey period 1"
smtp_session

log_info "# wait 20s"
sleep 20
log_info "# reconnect during grey period 2"
smtp_session

log_info "# wait 100s"
sleep 100
log_info "# reconnect after grey period"
smtp_session

log_info "# restore grey defaults"
smtp_settings 600 90000

log_exit $EX_OK DONE
