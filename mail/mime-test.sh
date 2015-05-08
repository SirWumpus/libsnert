#!/bin/ksh
echo $KSH_VERSION

make mime >/dev/null

for i in md5 md5sum ; do
	if which $i >/dev/null 2>&1 ; then
		MD5=`which $i`
		break;
	fi
done
if test ${MD5:-no} = 'no' ; then
#	echo "No md5 file checksum tool found."
#	exit 1

	cd ../util
	make md5
	cd -
	MD5='../util/md5'
fi
echo "MD5=${MD5}"

function check_mime
{
	typeset file=$1; shift
	typeset mime_opts=$@

	typeset mime_cmd="./mime${mime_opts:+ $mime_opts}"

	echo "$ $mime_cmd -l ${file} " >>${file}.out
	typeset num_parts=`$mime_cmd -l ${file} | tee -a ${file}.out | sed -n -e '$s/://p' | tr -d '\015'`
	if test ${num_parts} -eq 0 ; then
		num_parts=1
	fi

	typeset -i i=0
	while test $i -le ${num_parts} ; do
		typeset encoded=`$mime_cmd -l ${file} | sed -n -e "/^${i}: X-MD5-Encoded:/s/^${i}: X-MD5-Encoded: //p" | tr -d '[:space:]'`
		typeset decoded=`$mime_cmd -l ${file} | sed -n -e "/^${i}: X-MD5-Decoded:/s/^${i}: X-MD5-Decoded: //p" | tr -d '[:space:]'`

		echo "$ $mime_cmd -p${i} ${file} " >>${file}.out
		typeset computed=`$mime_cmd -p${i} ${file} | tee -a ${file}.out | tee $$.tmp | $MD5 | sed -e 's/ \*-//'`

		if test -n "${encoded}" -a ${computed} != ${encoded:-undef} ; then
			echo "${file}: part $i encoded $encoded computed $computed" | tee -a ${file}.out
			echo ---- | tee -a ${file}.out
			cat $$.tmp | od -c | tee -a ${file}.out
			echo ---- | tee -a ${file}.out
		fi

		if test -n "${decoded}" ; then
			echo "$ $mime_cmd -d -p${i} ${file} " >>${file}.out
			computed=`$mime_cmd -d -p${i} ${file} | tee -a ${file}.out | tee $$.tmp  | $MD5 | sed -e 's/ \*-//'`
			if test ${computed} != ${decoded:-undef} ; then
				echo "${file}: part $i decoded $decoded computed $computed"| tee -a ${file}.out
				echo ---- | tee -a ${file}.out
				cat $$.tmp | od -c | tee -a ${file}.out
				echo ---- | tee -a ${file}.out
			fi
		fi

		i=`expr $i + 1`
	done
	echo '$' >>${file}.out
	rm -f $$.tmp

	return 0
}

file="mime-stdin"
echo "Checking stdin ..." | tee ${file}.out
echo "Hello world!" "Hello world!"| ./mime -l >>${file}.out
computed=`echo "Hello world!" | ./mime -p0 | tee -a ${file}.out | tee $$.tmp | $MD5 | sed -e 's/ \*-//'`
expected=`echo "Hello world!" | $MD5 | sed -e 's/ \*-//'`
if test ${computed} !=  ${expected}; then
	echo "stdin: part 0 expected $expected computed $computed" | tee -a ${file}.out
	echo ---- | tee -a ${file}.out
	cat $$.tmp | od -c | tee -a ${file}.out
	echo ---- | tee -a ${file}.out
fi
rm -f $$.tmp

echo "Checking mime-7bit-crlf.eml ..." | tee "mime-7bit-crlf.eml.out"
check_mime "mime-7bit-crlf.eml"

echo "Checking mime-qp-crlf.eml ..." | tee  "mime-qp-crlf.eml.out"
check_mime "mime-qp-crlf.eml"

echo "Checking mime-b64-crlf.eml ..." | tee  "mime-b64-crlf.eml.out"
check_mime "mime-b64-crlf.eml"

echo "Checking mime-gtube-crlf.eml ..." | tee  "mime-gtube-crlf.eml.out"
check_mime "mime-gtube-crlf.eml"

echo "Checking mime-gtube-lf.eml ..." | tee  "mime-gtube-lf.eml.out"
check_mime "mime-gtube-lf.eml"

echo "Checking mime-multi-crlf.eml with NO preamble text ..." | tee  "mime-multi-crlf.eml.out"
check_mime "mime-multi-crlf.eml"

echo "Checking mime-multi-nohdr.eml ..." | tee "mime-multi-nohdr.eml.out"
check_mime "mime-multi-nohdr.eml"

echo "Checking mime-multi-open-boundary.eml ..." | tee "mime-multi-open-boundary.eml.out"
check_mime "mime-multi-open-boundary.eml" '-Bweak'

exit 0
