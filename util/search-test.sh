#!/bin/ksh

PROG="./search"

args=$(getopt 'h' $*)
if [ $? -ne 0 ]; then
	echo "usage: ${PROG}-test.sh [-h]"
	exit 2
fi
set -- $args
while [ $# -gt 0 ]; do
	case "$1" in
	(-h)
		__h='-h'
		;;
	(--)
		shift; break
		;;
	esac
	shift
done

function srch
{
	typeset max_err="$1"; shift
	typeset pattern="$1"; shift
	typeset file="$1"; shift
	typeset lines="$1"; shift

 	echo "<<< k=$max_err pat=$pattern $file"
	count=$($PROG $__h -b -k $max_err "$pattern" $file | tee search.tmp | wc -l)
	cat search.tmp

	printf 'got=%d expect=%d ' $count $lines
	if [ $count -ne $lines ]; then
		echo FAIL
		return 1
	fi
	echo -OK-

	printf 'check offsets '
	for offset in $( cut -d' ' -f2 search.tmp ) ; do
		typeset expect="$1"; shift
		if [ $offset -ne $expect ]; then
			echo FAIL
			return 1
		fi
	done

	echo -OK-
	return 0
}

srch 0 AGCT AGCT.txt 2 12 0
srch 1 AGCT AGCT.txt 2 12 0
srch 2 AGCT AGCT.txt 2 2 0
srch 3 AGCT AGCT.txt 2 2 0
#srch 4 AGCT AGCT.txt 2 0 0

srch 0 GCAGAGAG GCAGAGAG.txt 2 5 0
srch 1 GCAGAGAG GCAGAGAG.txt 3 5 10 0
srch 2 GCAGAGAG GCAGAGAG.txt 3 5 10 0
srch 3 GCAGAGAG GCAGAGAG.txt 3 3 10 0
srch 4 GCAGAGAG GCAGAGAG.txt 3 0 1 0

srch 0 return search.txt 4 12 0 8 13
srch 1 return search.txt 5 12 0 8 13 9
srch 2 return search.txt 6 12 0 8 13 9 9
srch 3 return search.txt 8 12 0 8 13 9 9 9 10

rm search.tmp

echo DONE
