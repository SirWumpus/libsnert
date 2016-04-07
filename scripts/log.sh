#!/bin/ksh

#######################################################################
# Common Exit Codes
#######################################################################

# C stdlib.h
EX_OK=0			# success
EX_FAIL=1		# failure, test objectives failed

EX_ERROR=2		# general error, typically in preparation or operation
EX_ABORT=3		# "dude, you're screwed", cannot continue

# Unix sysexits.h
EX_USAGE=64		# command line usage error
EX_DATAERR=65		# data format error
EX_NOINPUT=66		# cannot open input
EX_NOUSER=67		# addressee unknown
EX_NOHOST=68		# host name unknown
EX_UNAVAILABLE=69	# service unavailable
EX_SOFTWARE=70		# internal software error
EX_OSERR=71		# system error (e.g., can't fork)
EX_OSFILE=72		# critical OS file missing
EX_CANTCREAT=73		# can't create (user) output file
EX_IOERR=74		# input/output error
EX_TEMPFAIL=75		# temp failure; user is invited to retry
EX_PROTOCOL=76		# remote error in protocol
EX_NOPERM=77		# permission denied
EX_CONFIG=78		# configuration error

# Bash Exit Codes (Reserved)
EX_NOT_EXECUTABLE=126	# Command found, but not executable.
EX_NOT_FOUND=127	# Command not found (in PATH).
EX__SIGNAL=128		# 128+N where N is the signal number

ex_name[$EX_OK]='-OK-'
ex_name[$EX_FAIL]='FAIL'
ex_name[$EX_KNOWN]='KNOWN'
ex_name[$EX_VERIFY]='VERIFY'
ex_name[$EX_ABORT]='ABORT'
ex_name[$EX_COREFOUND]='CORE'
ex_name[$EX_ERROR]='ERROR'
ex_name[$EX_INCOMPLETE]='INCOMPLETE'
ex_name[$EX_INCONCLUSIVE]='INCONCLUSIVE'
ex_name[$EX_BLOCKED]='BLOCKED'
ex_name[$EX_PARTIALLY_BLOCKED]='PARTIALLY_BLOCKED'
ex_name[$EX_PERMANENTLY_FAILED]='PERMANENTLY_FAILED'
ex_name[$EX_DEFERRED]='DEFERRED'
ex_name[$EX_USAGE]='USAGE'
ex_name[$EX_DATAERR]='DATAERR'
ex_name[$EX_NOINPUT]='NOINPUT'
ex_name[$EX_NOUSER]='NOUSER'
ex_name[$EX_NOHOST]='NOHOST'
ex_name[$EX_UNAVAILABLE]='UNAVAILABLE'
ex_name[$EX_SOFTWARE]='SOFTWARE'
ex_name[$EX_OSERR]='OSERR'
ex_name[$EX_OSFILE]='OSFILE'
ex_name[$EX_CANTCREAT]='CANTCREAT'
ex_name[$EX_IOERR]='IOERR'
ex_name[$EX_TEMPFAIL]='TEMPFAIL'
ex_name[$EX_PROTOCOL]='PROTOCOL'
ex_name[$EX_NOPERM]='NOPERM'
ex_name[$EX_CONFIG]='CONFIG'
ex_name[$EX_NOT_EXECUTABLE]='NOT_EXECUTABLE'
ex_name[$EX_NOT_FOUND]='NOT_FOUND'

#######################################################################
# Logging
#######################################################################

LOG_ALERT=0
LOG_ERROR=1
LOG_WARN=2
LOG_INFO=3
LOG_DEBUG=4

log_levels[$LOG_ALERT]="ALERT"
log_levels[$LOG_ERROR]="ERROR"
log_levels[$LOG_WARN]="WARN"
log_levels[$LOG_INFO]="INFO"
log_levels[$LOG_DEBUG]="DEBUG"

#
# Default log to standard error.
#
: ${log_file:=}

#
# Default log level
#
: ${log_level:=$LOG_INFO}

function log_print
{
	typeset level=$1; shift

	if [ $level -le $log_level ]; then
		typeset iso_8601=$(date +'%Y-%m-%dT%H:%M:%S')
		printf "%s %s %s\n" $iso_8601 ${log_levels[$level]} "$@" 1>&2
		if [ -n "$log_file" ]; then
			printf "%s %s %s\n" $iso_8601 ${log_levels[$level]} "$@" >>$log_file
		fi
	fi
}

function log_file
{
	typeset level=$1; shift
	typeset file=$1; shift

	if [ $level -le $log_level ]; then
		log_print $level "--start: $2"
		cat $file
		if [ -n "$log_file" ]; then
			cat $file >>$log_file
		fi
		log_print $level "--end: $2"
	fi
}

function log_debug
{
	log_print $LOG_DEBUG "$@"
}

function log_info
{
	log_print $LOG_INFO "$@"
}

function log_warn
{
	log_print $LOG_WARN "$@"
}

function log_error
{
	log_print $LOG_ERROR "$@"
}

function log_alert
{
	log_print $LOG_ALERT "$@"
}

function log_exit
{
	typeset ex=$1; shift
	if [ $ex -eq $EX_OK ]; then
		log_info "$@"
	elif [ $ex -eq $EX_ABORT ]; then
		log_alert "$@"
	else
		log_error "$@"
	fi
	exit $ex
}

function assert
{
	typeset lineno=$1 ; shift
	typeset condition="$1" ; shift
	if [ ! $condition ]; then
		log_exit $EX_ABORT "$0:$lineno asssert failed: $condition"
	fi
}

if [ $(basename $0) = "log.sh" ]; then
	log_info "Hello world!"
fi

#######################################################################
# END
#######################################################################
