#!/bin/bash

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
. ./log.sh

log_level=$LOG_ERROR
log_app=$(basename $0 .sh)

#######################################################################
# Options & Arguments
#######################################################################

function usage
{
        echo 'usage: whois.sh [-d file] domain|list.txt'
        echo
        printf -- "-d file\t\tuse this whois data, skipping whois query\n"
        echo
        exit $EX_USAGE
}

args=$(getopt 'd:' $*)
if [ $? -ne $EX_OK ]; then
	usage
fi
set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
	(-d) __data=$2; shift ;;
        (--) shift; break ;;
        esac
        shift
done
if [ ${__data:-no} = 'no' -a $# -le 0 ]; then
	usage
fi

#######################################################################
# Functions
#######################################################################

trap "kill 0; exit $EX_ABORT" SIGINT SIGQUIT SIGTERM

function whois_grep
{
	typeset pattern="$1"
	typeset file="$2"
	grep -iE "$pattern" "$file" | sed -e's/^.*: *//' | tr -d '\r'
}

function whois_domain
{
	typeset domain="$1"
	typeset info="whois$$.tmp"

	if [ -z "$__data" ]; then
		whois $domain >$info
	else
		info="$__data"
		domain=$(whois_grep '^Domain Name:' $info)
	fi

	# ICANN 2013 Standard Whois Format
	# https://www.icann.org/resources/pages/approved-with-specs-2013-09-17-en#whois
	typeset org=$(whois_grep '^(Registrant Organization|Organization):' $info)
	typeset address=$(whois_grep '^Registrant (Street|City|State|Post|Country)' $info | tr '\n' ',' | sed -e's/,$//; s/,/, /g')
	typeset phone=$(whois_grep '^Registrant Phone' $info)
	typeset owner=$(whois_grep '^Registrant Email' $info)
	typeset admin=$(whois_grep '^Admin Email' $info)
	typeset tech=$(whois_grep '^Tech Email' $info)

	# Prior to 2013 registrars whois servers could return data
	# in a non-standard format.
	if [ -z "$phone" ]; then
		# Non-standard format.  Try find any/all phone numbers.
		phone=$(whois_grep '\+[0-9]+[-. 0-9]+' $info | tr '\n' ',' | sed -e's/,$//; s/,/, /g')
	fi
	if [ -z "$owner" ]; then
		# Non-standard format.  Try find any/all mail addresses.
		owner=$(whois_grep '@[-_a-z0-9]+(.[-_a-z0-9]+)+' $info | tr '\n' ',' | sed -e's/,$//; s/,/, /g')
	fi

	printf "\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\"\r\n" \
		$domain "$org" "$address" "$phone" "$owner" $admin $tech

	if [ -z "$__data" ]; then
		rm -f $info
	fi
}

function whois_file
{
	typeset domains="$1"
	for domain in $(cat "$domains"); do
		whois_domain $domain
	done
}

#######################################################################
# Main
#######################################################################

#printf "domain, owner_organization, owner_address, owner_phone, owner_mail, admin_mail, tech_mail\r\n"

for domain in "$@"; do
	if [ -f "$domain" ]; then
		whois_file "$domain"
	else
		whois_domain "$domain"
	fi
done
