#!/bin/sh
#
# mail-cycle.sh
#
# Copyright 2004 by Anthony Howe.  All rights reserved.
#
# usage: mail-cycle.sh $email $smtpHost $popHost $popUser $popPass \
#	 $retrySeconds $warningSeconds $alertSeconds
#
# return
#
#	0	OK
#	1	Warning, eventually got a result, but slow.
#	2	Alert, no result 
#

EXIT_OK=0
EXIT_WARN=1
EXIT_ALERT=2

PATH=".:$PATH"

if test $# -ne 8 ; then
	echo 'usage: mail-cycle.sh $email $smtpHost $popHost $popUser $popPass \'
	echo '       $retrySeconds $warningSeconds $alertSeconds'
	exit $EXIT_ALERT
fi

email="$1"
smtpHost="$2"
popHost="$3"
popUser="$4"
popPass="$5"
retrySeconds="$6"
warnSeconds="$7"
alertSeconds="$8"

# Limit the number of retries.
retrys=$(( $alertSeconds / $retrySeconds ))

startTime=`date +'%s'`

# Delete all existing messages from the POP account.
popin -d -h $popHost $popUser $popPass
case $? in
0) ;;
2) echo "popin usage error"; exit $EXIT_ALERT ;;
*) echo "alert: popin $popHost failed"; exit $EXIT_ALERT ;;
esac

# Send a message via a specific SMTP server.
smtpout -h $smtpHost $email <<EOT 
Subject: SMTP POP cycle test.
From: "mail-cycle test"

.
EOT
case $? in
0) ;;
2) echo "smtpout usage error"; exit $EXIT_ALERT ;;
*) echo "alert: smtpout $smtpHost failed"; exit $EXIT_ALERT ;;
esac

# Attempt to get the number of messages in the POP account.
iteration=0
while test $iteration -lt $retrys; do
	count=`popin -h $popHost $popUser $popPass | sed -e 's/\([0-9]*\) .*/\1/'`
	if test $? -eq 0 -a $count -gt 0; then
		break
	fi
	sleep $retrySeconds
	iteration=$(( $iteration + 1 ))
done

stopTime=`date +'%s'`
runTime=$(( $stopTime - $startTime ))

# Did we exceed the number of retries?
if test $iteration -ge $retrys; then 
	echo "alert: no message after ${runTime}s $retrys retrys"
	exit $EXIT_ALERT
elif test $runTime -ge $alertSeconds; then
	echo "alert: mail looped in ${runTime}s $iteration/$retrys retrys"
	exit $EXIT_ALERT
elif test $runTime -ge $warnSeconds; then
	echo "warning: mail looped in ${runTime}s $iteration/$retrys retrys"
	exit $EXIT_WARN
fi

echo "ok: mail looped in ${runTime}s $iteration/$retrys retrys"
exit $EXIT_OK

#######################################################################
# END
#######################################################################
