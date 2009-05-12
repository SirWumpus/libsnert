#!/bin/sh

make mime >/dev/null

if which md5 >/dev/null; then
	MD5=`which md5`
elif which md5sum >/dev/null; then
	MD5=`which md5sum`
fi

echo "MD5=${MD5}"

function check_mime
{
	file=$1
	echo "$ ./mime -l ${file} " >>${file}.out
	num_parts=`./mime -l ${file} | tee -a ${file}.out | sed -n -e '$s/^0*//' -e '$s/://p' | tr -d '\015'`

	i=0
	while test $i -lt $num_parts ; do
		encoded=`./mime -l ${file} | sed -n -e "/^${i}: X-MD5-Encoded:/s/^${i}: X-MD5-Encoded: //p" | tr -d '[:space:]'`
		decoded=`./mime -l ${file} | sed -n -e "/^${i}: X-MD5-Decoded:/s/^${i}: X-MD5-Decoded: //p" | tr -d '[:space:]'`

		echo "$ ./mime -p${i} ${file} " >>${file}.out
		computed=`./mime -p${i} ${file} | tee -a ${file}.out | tee $$.tmp | $MD5`
		if test ${computed} != ${encoded} ; then
			echo "${file}: part $i encoded $encoded computed $computed" | tee -a ${file}.out
			echo ---- | tee -a ${file}.out
			cat $$.tmp | od -c | tee -a ${file}.out
			echo ---- | tee -a ${file}.out
		fi

		echo "$ ./mime -d -p${i} ${file} " >>${file}.out
		computed=`./mime -d -p${i} ${file} | tee -a ${file}.out | tee $$.tmp  | $MD5`
		if test ${computed} != ${decoded} ; then
			echo "${file}: part $i decoded $decoded computed $computed"| tee -a ${file}.out
			echo ---- | tee -a ${file}.out
			cat $$.tmp | od -c | tee -a ${file}.out
			echo ---- | tee -a ${file}.out
		fi

		i=`expr $i + 1`
	done
	rm -f $$.tmp

	return 0
}

echo "Checking mime-eicar-test.eml with CRLF newline..." | tee  mime-eicar-test.eml.out
check_mime mime-eicar-test.eml

echo "Checking mime-eicar-test-lf.eml with LF newline..." | tee  mime-eicar-test-lf.eml.out
check_mime mime-eicar-test-lf.eml

