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
        echo 'usage: smtp-profile.sh [-v][-H helo][-j dir][-t sec] domain|list.txt'
        exit $EX_USAGE
}

# If no HELO argument given, then use the host's name.  Note that smtp2
# defaults to the host's IP-domain-literal if no HELO argument given,
# which is valid, but not as pretty.
__helo=$(hostname)

# The default SMTP connection and command timeout is 300 seconds, which
# is too long to spend when we're simply testing SMTP connectivity.
__timeout=30

args=$(getopt 'vH:j:t:' $*)
if [ $? -ne $EX_OK ]; then
	usage
fi
set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
	-H) __helo=$2; shift ;;
	-j) JOBDIR=$2; shift ;;
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

: ${CHILDPOLL:=10}

# See config file used by this script and the .php script.
#JOBDIR="/tmp/smtp-profile"

## Should be part of environment variables.
#if [ -n "$PHP_AUTH_USER" ]; then
#	JOBDIR="$JOBDIR/$PHP_AUTH_USER";
#fi

NOW=$(date +'%Y%m%dT%H%M%S')
CSV="$JOBDIR/$NOW.csv"
LOG="$JOBDIR/$NOW.log"
JOB="$JOBDIR/$NOW.job"
SPAMHAUS="$JOBDIR/spamhaus.txt"
SPAMHAUSLOCK="$JOBDIR/spamhaus.lock"
FROM="postmaster@$__helo"

log_file_set "$LOG"

#######################################################################
# Functions
#######################################################################

trap "kill 0; exit $EX_ABORT" SIGINT SIGQUIT SIGTERM

function lf_nl
{
	typeset txt="$1"
	cat  $txt | tr -d '\r' >$txt.tmp
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
		flock -x $SPAMHAUSLOCK echo $domain >>$SPAMHAUS
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
	typeset SESSION="$JOBDIR/smtp$$.$domain"

	# Test connection.  Normally smtp2 logs to /var/log/maillog,
	# so the -vvv sets debugging and logging to stderr instead
	# which separates the logging from the system's maillog.
	smtp2 -e -f $FROM -H $__helo -t $__timeout -vvv info@$domain 2>&1 | tr -d '\r' >$SESSION

	log_write_file $SESSION

	analyse "$domain" "$SESSION"

	rm -f $SESSION
}

function test_job
{
	typeset job="$1"

	typeset domain
	typeset count=0
	typeset total=$(wc -l <$job)
	typeset COUNT="$job.count"

	for domain in $(cat $job); do
		echo "$count $total" >$COUNT

		# Keep track of what MXes have been tested to
		# avoid repeatedly testing domains hosted with
		# outlook.com, google.com, etc.
		typeset mx=$(dig +short mx "$domain" | cut -d' ' -f2 | tr '\n' '|' | sed -e's/|$//; /^$/d')
		if [ -n "$mx" ]; then
			if grep -i -q -E "$mx" $MX; then
				continue
			fi
			flock -x $MXLOCK echo "$mx" >>$MX
		fi

		test_domain "$domain"
		(( count++ ))
	done

	rm -f $COUNT
}

function test_file
{
	typeset domains="$1"

	typeset MX="$JOBDIR/$NOW.mx"
	typeset MXLOCK="$MX.lock"

	csv_headings

	# Create MX tracking file shared across sub-shells.
	>$MX

	# Do we want to split large jobs?
	typeset count=$(cat $domains | wc -l)
	if [ ${MAXCHILD:-0} -gt 1 -a $count -gt ${MAXPERCHILD:=100} ]; then
		typeset prefix="$JOBDIR/$NOW.job$$"
		split -a 3 -l $MAXPERCHILD $domains $prefix

		for subset in ${prefix}* ; do
			# Start background sub-shell.
			( test_job $subset >$subset.csv ) &

			# Limit how many sub-shells we run at a given time;
			# too many and we might "thrash" the CPU.
			while [ $(pgrep $0 | wc -l) -gt $MAXCHILD ]; do
				sleep $CHILDPOLL
			done
		done
		wait

		sort ${prefix}*.csv >$CSV
		rm -f ${prefix}*
	else
		test_job $domains >$CSV
	fi

	crlf_nl $CSV

	rm -f $MXLOCK $MX
}

#######################################################################
# Main
#######################################################################

# Assert report directory is present.  If held within /tmp, then
# /tmp is typically emptied on system reboot, so need to recreate
# it.
mkdir -p $JOBDIR >/dev/null 2>&1

# Allow it to be accessible by a web server.
chmod -R 0777 $JOBDIR >/dev/null 2>&1

if [ -f "$__list" ]; then
	# Ensure the list is one domain per line with Unix newlines.
	# Account for email addresses, list of emails, and odd suffixes.
	sed -e's/"//g; s/[ ,;] */\n/g; s/.stopped$//i; s/.gone$//i; s/.notrenew$//i; s/^.*@//' "$__list" \
		| tr -d '\r' | sort -u >$JOB.busy
	test_file $JOB.busy
	mv $JOB.busy $JOB
	crlf_nl	$JOB
else
	csv_headings
	test_domain "$__domain"
fi

# Convert the output files to CRLF newlines for Windows users.
log_info "$LOG DONE"
crlf_nl $LOG
(
	flock -x 99
	if [ -f $SPAMHAUS ]; then
		# Before sorting, ensure Unix newlines.
		cat $SPAMHAUS | tr -d '\r' | sort -u >$JOBDIR/$$.tmp
		mv $JOBDIR/$$.tmp $SPAMHAUS
		crlf_nl $SPAMHAUS
	fi
) 99>$SPAMHAUSLOCK
# Do not remove SPAMHAUSLOCK in case multiple instances of this
# script are running.

rm -f $log_lock
exit $EX_OK
