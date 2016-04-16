#/bin/sh
#
# Add/change a password in an Nginx password file.
#

if [ $# -ne 2 ]; then
	echo 'usage: addpasswd.sh file user'
	exit 1
fi

__file=$1
__user=$2

stty -echo
pass1=X
while [ "$pass1" != "$pass2" ]; do
	printf 'Password: '
	read -r pass1
	echo
	printf 'Repeat password: '
	read -r pass2
	echo
done
stty echo

if [ -f "$__file" ]; then
	sed -e"/^$__user:/d" $__file >$$.tmp
	mv $$.tmp $__file
fi
printf "$__user:$(openssl passwd -1 $pass1)\n" >>$__file

