#!/bin/ksh
. ./log.sh

__port=25
__host="127.0.0.1"
__log=/tmp/grey-test.log
__nctee="../tools/nctee"
__popin="../tools/popin"
__test_msg=false
__verbose=false
__is_smtpf=$(pgrep smtpf >/dev/null ; echo $?)

function usage
{
        log_exit $EX_USAGE 'Usage: grey-test.sh [-BTv][-h host][-p port] helo mail rcpt rcpt_pass'
}

args=$(getopt 'Tvh:p:' $*)
if [ $? -ne $EX_OK ]; then
	usage
fi
set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
        -B) __is_smtpf=$EX_FAIL ;;
	-h) __host=$2; shift ;;
	-p) __port=$2; shift ;;
	-T) __test_msg=true ;;
        -v) __verbose=true ; log_level=$LOG_DEBUG ;;
        --) shift; break ;;
        esac
        shift
done
if [ $# -ne 4 ]; then
	usage
fi

__helo=$1
__mail=$2
__rcpt=$3
__pass=$4
__ip=$(dig +short a $__helo)
__ptr=$(echo $__helo | cut -d. -f2- )
__user=$(echo $__rcpt | cut -d@ -f1 )

: ${NETCAT:=$(which ncat)}		# Nmap version
: ${NETCAT:=$(which netcat)}		# GNU Linux version
: ${NETCAT:=$(which nc)}		# Original 1.10
if [ -z "$NETCAT" ]; then
	log_exit $EX_ALERT "NETCAT path is undefined"
else
	log_info "NETCAT=$NETCAT"
	log_info "is_smtpf=$__is_smtpf ip=$__ip helo=$__helo ptr=$__ptr mail=$__mail rcpt=$__rcpt"
fi

: ${SMTP_SESSION:=$NETCAT -w 5 $__host $__port}
: ${ISO_DATE:=$(date +'%Y%m%d')}
: ${ISO_TIME:=$(date +'%H%M%S%z')}
: ${RFC_DATE:=$(date +'%e %b %Y %H:%M:%S %z')}

function smtp_session
{
	log_debug "message $__host $__port"
	$__nctee -a -c $__log <<-EOF | $SMTP_SESSION >>$__log
		XCLIENT NAME=$__helo ADDR=$__ip
		HELO $__helo
		MAIL FROM:<$__mail>
		RCPT TO:<$__rcpt>
		DATA
		Date: $RFC_DATE
		Message-ID: <${ISO_DATE}T${ISO_TIME}@${__helo}>
		Subject: grey list test
		From: <$__mail>
		To: <$__rcpt>

		Test grey listing
		.
		QUIT
	EOF
}

function smtp_settings
{
	typeset period=$1
	typeset ttl=$2

	log_debug "settings [$__host]:$__port grey period=$period, ttl=$ttl"

	if [ ! $__is_smtpf ]; then
		return
	fi

	$__nctee -a -c $__log <<-EOF | $SMTP_SESSION >>$__log
		OPTN grey-temp-fail-period="$period"
		OPTN grey-temp-fail-ttl="$ttl"
		OPTN grey-key=ptr,mail,rcpt
		VERB +grey
		CACHE DELETE grey:$__ptr,$__mail,$__rcpt
		CACHE DELETE grey:$__ptr
		QUIT
	EOF
}

function cache_query
{
	if [ ! $__is_smtpf ]; then
		return
	fi

	log_debug "cache query [$__host]:$__port"
	$__nctee -a -c $__log <<-EOF | $SMTP_SESSION >>$__log
		CACHE GET grey:$__ptr,$__mail,$__rcpt
		CACHE GET grey:$__ptr
		QUIT
	EOF
}

function cache_get
{
	typeset key=$1

	if [ ! $__is_smtpf ]; then
		return
	fi

	log_debug "cache get [$__host]:$__port $key"
	value=$($__nctee -a -c $__log <<-EOF | $SMTP_SESSION | tee -a $__log | sed -n -e '/k=.*d=/s/.*d="\([^"]*\).*/\1/p'
		CACHE GET $key
		QUIT
	EOF)
	log_info "cache get [$__host]:$__port $key='$value'"
	echo $value
}

function is_cache_key
{
	typeset key=$1
	typeset expect=$2

	if [ ! $__is_smtpf ]; then
		return
	fi

	typeset value=$(cache_get "$key")
	if [ "$value" != "$expect" ]; then
		log_exit $EX_FAIL "error grey key '$key' value='$value' expect='$expect'"
	fi
}

# Reset nc log
>$__log

# Short grey listing settings
smtp_settings 120 180

if $__test_msg ; then
	log_info "# single test message"
	smtp_session
	echo $(cache_get "grey:$__ptr,$__mail,$__rcpt")
	exit $EX_OK
fi

log_info "# verifying $__user mail box is empty"
msg_count=$($__popin "$__user" "$__pass" | tee -a $__log | cut -d' ' -f1)
if [ $msg_count -gt 0 ]; then
	log_exit $EX_ABORT "$__user mail box not empty"
fi

log_info "# initial connection starts grey temp.fail period"
smtp_session

# Check that the grey temp. fail key is created.
is_cache_key "grey:$__ptr,$__mail,$__rcpt" 4

# The grey pass key must not exist yet.
is_cache_key "grey:$__ptr" ''

log_info "# wait 20s"
sleep 20

log_info "# reconnect during grey period"
smtp_session
is_cache_key "grey:$__ptr,$__mail,$__rcpt" 4
is_cache_key "grey:$__ptr" ''

log_info "# wait 100s"
sleep 100

log_info "# reconnect after grey period"
smtp_session
# The grey pass key should now exist.
is_cache_key "grey:$__ptr" 0

log_info "# wait 20s"
sleep 20

log_info "# send 2nd messsage without being grey listed"
smtp_session
is_cache_key "grey:$__ptr" 0

# Should have two messages in mail box.
msg_count=$($__popin "$__user" "$__pass" | tee -a $__log | cut -d' ' -f1)
if [ $msg_count -ne 2 ]; then
	log_exit $EX_FAIL "$__user mail box miscount count=$msg_count expected=2"
fi

log_info "# Remove test messages from $__user mail box"
$__popin -d "$__user" "$__pass" 1 2

log_info "# wait 40s"
sleep 40

log_info "# The grey temp.fail record should have expired."
is_cache_key "grey:$__ptr,$__mail,$__rcpt" ''

# Restore BarricadeMX grey listing defaults.
smtp_settings 600 90000

log_exit $EX_OK DONE
