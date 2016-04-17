#!/bin/sh
#
# smtp-profile.sh requires LibSnert 1.75.50+ for smtp2 CLI tool.
#

#######################################################################
# Assert Safe Shell Environment
#######################################################################

export PATH='/bin:/usr/bin:/usr/local/bin'
export ENV=''
export CDPATH=''
export LANG=C

#######################################################################
# Imports
#######################################################################

cd $(dirname -- $0)
. ./smtp-profile.cf
. ./log.sh

log_level=$LOG_ERROR
log_app=$(basename $0 .sh)

#######################################################################
# Options & Arguments
#######################################################################

function usage
{
        log_exit $EX_USAGE 'usage: smtp-profile.sh [-v][-H helo][-t sec] domain|list.txt'
}

# If no HELO argument given, then use the host's name.  Note that smtp2
# defaults to the host's IP-domain-literal if no HELO argument given,
# which is valid, but not as pretty.
__helo=$(hostname)

# The default SMTP connection and command timeout is 300 seconds, which
# is too long to spend when we're simply testing SMTP connectivity.
__timeout=30

args=$(getopt 'vH:t:' $*)
if [ $? -ne $EX_OK ]; then
	usage
fi
set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
	-H) __helo=$2; shift ;;
	-t)
		if [ "$2" -gt 0 ]; then
			__timeout=$2
		fi
		shift
		;;
        -v) log_level=$LOG_DUMP ;;
        --) shift; break ;;
        esac
        shift
done
if [ $# -ne 1 ]; then
	usage
fi

__list="$1"
__domain="$1"

#######################################################################
# Constants
#######################################################################

# See config file used by this script and the .php script.
#ROOTDIR="/tmp/smtp-profile"

NOW=$(date +'%Y%m%dT%H%M%S')
DOMAINS="$ROOTDIR/domains$$.tmp"
SESSION="$ROOTDIR/smtp$$.log"
CSV="$ROOTDIR/$NOW.csv"
LOG="$ROOTDIR/$NOW.log"
JOB="$ROOTDIR/$NOW.job"
MX="$ROOTDIR/$NOW.mx"
COUNT="$ROOTDIR/$NOW.count"
SPAMHAUS="$ROOTDIR/spamhaus.txt"
LOCK="$ROOTDIR/spamhaus.lock"
FROM="postmaster@$__helo"

# Enable log file in addition to stderr.
log_file="$LOG"

#######################################################################
# Functions
#######################################################################

function lf_nl
{
	typeset txt="$1"
	tr -d '\r' $txt >$txt.tmp
	mv $txt.tmp $txt
}

function crlf_nl
{
	typeset txt="$1"
	CR=$(printf '\r')
	sed "s/\([^$CR]\)\$/\1$CR/" $txt >$txt.tmp
	mv $txt.tmp $txt
}

function analyse
{
	typeset domain="$1"
	typeset session="$2"

	typeset mx=$(sed -n -e's/.*connecting host=\(.*\)$/\1/p;' $session)

	log_debug "domain=$domain mx=$mx"

	# Did we connect and get a welcome banner?  If not, we were
	# probably quit_ok because of our IP being blacklisted.  Note
	# server connection could return 421 Server Busy or 554 No Service.
	typeset connect=false
	typeset no_service=false
	typeset server_busy=false
	typeset connect_reply=$(sed -e'1,/connected host=/d' $session | head -n1)
	typeset connect_rcode=$(echo "$connect_reply" | sed -n -e's/.*<< \([245][0-9][0-9]\)[- ].*/\1/p')
	case "$connect_rcode" in
	# Successful connection.
	(220) connect=true;;
	# Successful connection, but busy.
	(421) connect=true; server_busy=true;;
	# Successful connection, but no service.
	(554) connect=true; no_service=true;;
	# Successful connection, but unexpected non-compliant SMTP code.
	([0-9]*) connect=true;;
	(*)
		if grep -q "domain=$domain does not exist" $session; then
			connect_rcode='NXDOMAIN'
			connect=':'
			mx='n/a'
		elif grep -q "connection timeout MX domain=$domain" $session; then
			connect_rcode='TIMEOUT'
			connect=':'
		elif grep -iq "Connection timed out" $session; then
			connect_rcode='TIMEOUT'
			connect=':'
		fi
		;;
	esac

	# Was it multiline banner?  These can screw with some poorly
	# written spam engines and possible sign of anti-spam efforts.
	typeset multiline_banner=false
	if grep -q '<< 220-' $session ; then
		multiline_banner=true
	fi

	# Did we have to downgrade to HELO from EHLO?
	typeset helo_used=false
	if grep -q '>> [0-9]*:HELO' $session ; then
		helo_used=true
	fi

	log_debug "conn=$connect rcode=$connect_rcode multiline=$multiline_banner helo=$helo_used"

	# Was the sender blocked or accepted?
	typeset mail_from=false
	typeset mail_reply=$(sed -e'1,/MAIL FROM/d' $session | head -n1)
	typeset mail_rcode=$(echo "$mail_reply" | sed -n -e's/.*<< \([245][0-9][0-9]\)[- ].*/\1/p')
	case "$mail_rcode" in
	(250) mail_from=true;;
	esac

	log_debug "mail=$mail_from rcode=$mail_rcode"

	# Was the recipient blocked or accepted?
	typeset rcpt_to=false
	typeset grey_list=false
	typeset rcpt_reply=$(sed -e'1,/RCPT TO/d' $session | head -n1)
	typeset rcpt_rcode=$(echo "$rcpt_reply" | sed -n -e's/.*<< \([245][0-9][0-9]\)[- ].*/\1/p')
	case "$rcpt_rcode" in
	(250) rcpt_to=true;;
	esac

	# Were we temporarily blocked by grey listing?  Traditional
	# grey-listing uses a tuple of IP, MAIL FROM, and RCPT TO.
	# Code 451 is the recommended code, but some old versions
	# use 450.
	case "$rcpt_rcode" in
	(45[01]) grey_list=true;;
	esac

	# Delay-checks?  Some MTA can be configured to delay rejections
	# until the RCPT is provided in order to allow for RCPT white
	# listing.  Officially only the SMTP reply code and extended codes
	# are considered.  The human readable text can be empty, nonsense,
	# or possible useful feedback as to the reason.
	typeset delay_checks=false
	echo "$rcpt_reply" | grep -iqE 'spamhaus|(client|IP|host|connection) .*(blocked|rejected|refused|blacklisted)'
	if [ $? -eq 0 ];then
		delay_checks=true
	fi

	log_debug "rcpt=$rcpt_to rcode=$rcpt_rcode grey_list=$grey_list delayed=$delay_checks"

	# Were we dropped before QUIT reply?  Server well behaved?
	typeset quit_ok=false
	if grep -q '<< 221' $session ; then
		quit_ok=true;
	fi

	# Is spamhaus mentioned any where in the session?
	typeset spamhaus=false
	if grep -iq spamhaus $session; then
		log_debug "spamhaus referenced in log"
		spamhaus=true;
	elif ! $connect; then
		log_debug "connection dropped, assume spamhaus BL"
		spamhaus=true;
	elif $delay_checks ; then
		log_debug "delayed reporting of blocked client, assume spamhaus BL"
		spamhaus=true;
	fi

	# Collect SpamHaus hits in a separate file.
	if $spamhaus ; then
		# Make sure only one instance can update at a time.
		(
			flock -x 99
			echo $domain >>$SPAMHAUS
			sort $SPAMHAUS | uniq -u >$ROOTDIR/$$.tmp
			mv $ROOTDIR/$$.tmp $SPAMHAUS
		) 99>$LOCK
	fi

	log_debug "quit=$quit_ok spamhaus=$spamhaus"

	# Write CSV row.
	echo "$domain, $mx, $spamhaus, $connect_rcode, $multiline_banner, $helo_used, $mail_rcode, $rcpt_rcode, $grey_list, $delay_checks, $quit_ok"
}

#
# Write CSV column headings for humans.
#
function csv_headings
{
	echo "domain, mx, spamhaus, connect_rcode, multiline_banner, helo_used, mail_rcode, rcpt_rcode, grey_list, delay_checks, quit_ok"
}

function test_domain
{
	typeset domain="$1"

	# Test connection.  Normally smtp2 logs to /var/log/maillog,
	# so the -vvv sets debugging and logging to stderr instead
	# which separates the logging from the system's maillog.
	smtp2 -e -f $FROM -H $__helo -t $__timeout -vvv info@$domain 2>&1 | tr -d '\r' >$SESSION

	log_write_file $SESSION

	# Analyse SESSION session writing out CSV row.
	analyse "$domain" "$SESSION"

	rm -f $SESSION
}

function test_file
{
	typeset list="$1"

	csv_headings

	# Sort and remove duplicate domains.
	sed -e's/"//g; y/,;/\n\n/; s/.stopped$//i; s/.gone$//i; s/.notrenew$//i; ;s/^.*@//' "$list" | sort | uniq -u >$DOMAINS

	typeset count=0
	typeset total=$(wc -l <$DOMAINS)

	>$MX
	for domain in $(cat $DOMAINS); do
		echo "$count $total" >$COUNT

		# Keep track of what MXes have been tested to
		# avoid repeatedly testing domains hosted with
		# outlook.com, google.com, etc.
		mx=$(dig +short mx "$domain" | cut -d' ' -f2 | tr '\n' '|' | sed -e's/|$//; /^$/d')
		if [ -n "$mx" ]; then
			if grep -i -q -E "$mx" $MX; then
				continue
			fi
			echo "$mx" >>$MX
		fi

		test_domain "$domain"
		(( count++ ))
	done

	rm -f $DOMAINS $COUNT
}

#######################################################################
# Main
#######################################################################

# Assert report directory is present.  If held within /tmp, then
# /tmp is typically emptied on system reboot, so need to recreate
# it.
mkdir $ROOTDIR >/dev/null 2>&1

if [ -f "$__list" ]; then
	# Convert from CRLF to LF.
	tr -d '\r' <"$__list" >$JOB.busy
	test_file $JOB.busy >$CSV
	mv $JOB.busy $JOB
else
	csv_headings
	test_domain "$__domain"
fi

# Convert the output files to CRLF newlines for Windows users.
crlf_nl $CSV
crlf_nl $LOG
crlf_nl	$JOB
(
	flock -x 99
	if [ -f $SPAMHAUS ]; then
		crlf_nl $SPAMHAUS
	fi
) 99>$LOCK

log_exit $EX_OK "$LOG DONE"
