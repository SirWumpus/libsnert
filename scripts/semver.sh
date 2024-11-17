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

__update=false
__file="VERSION.TXT"

usage()
{
	echo 'usage: semver.sh [-u][-f file] [major|minor|patch|$VERSION]'
	exit 2
}

while getopts 'f:u' opt; do
	case "$opt" in
	(f)
		__file="$OPTARG"
		;;
	(u)
		__update=true
		;;
	(*)
		usage
	esac
done
shift $(($OPTIND - 1))

is_autoconf_file=$( (grep -q AC_INIT "$__file" && echo true) || echo false)

if $is_autoconf_file; then
	version=$(sed -n -e'/AC_INIT/s/.*, *\([0-9]*\.[0-9]*\.[0-9]*\),.*/\1/p' "$__file")
else
	if [ ! -f "$__file" ]; then
		echo "0.0.0" >"$__file"
	fi
	version=$(cat "$__file")
fi

if [ $# -le 0 ]; then
	echo $version
	exit 0
fi

#echo $__file $version
major=$(expr "$version" : "\([0-9]*\)\.[0-9]*\.[0-9]*")
minor=$(expr "$version" : "[0-9]*\.\([0-9]*\)\.[0-9]*")
patch=$(expr "$version" : "[0-9]*\.[0-9]*\.\([0-9]*\)")

if ! $__update ; then
	case "$1" in
	(major)	echo $major ;;
	(minor)	echo $minor ;;
	(patch)	echo $patch ;;
	esac
	exit 0
fi

case "$1" in
(major)
	version="$(( major + 1 )).0.0"
	;;
(minor)
	version="$major.$(( minor + 1 )).0"
	;;
(patch)
	version="$major.$minor.$(( patch + 1 ))"
	;;
(*.*.*)
	version="$1"
	;;
(*)
	usage
esac

if $is_autoconf_file; then
	sed -e"/AC_INIT/s/[0-9]*\.[0-9]*\.[0-9]*,/$version,/" "$__file" >"$__file.new"
	mv "$__file.new" "$__file"
else
	echo "$version" | tee "$__file"
fi
