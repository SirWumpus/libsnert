#/bin/sh
#
# Add/change a password in an Nginx password file.
#

function usage
{
        echo 'usage: addpasswd.sh [-d][-p password] file user'
        exit 2
}

__change=true

args=$(getopt -- 'dp:' "$@")
if [ $? -ne 0 ]; then
	usage
fi
eval set -- $args
while [ $# -gt 0 ]; do
        case "$1" in
        (-d) __change=false ;;
	(-p) pass1=$2; shift ;;
        (--) shift; break ;;
        esac
        shift
done
if [ $# -ne 2 ]; then
	usage
fi

__file=$1
__user=$2

if $__change && [ -z "$pass1" ]; then
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
fi

if [ -f "$__file" ]; then
	# Delete an existing user.
	sed -e"/^$__user:/d" $__file >$$.tmp
	mv $$.tmp $__file
fi
if $__change; then
	printf "$__user:$(openssl passwd -1 $pass1)\n" >>$__file
	sort $__file >$$.tmp
	mv $$.tmp $__file
fi
