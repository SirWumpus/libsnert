#!/bin/ksh

SK=./sk

if ! cc -g -osk sk.c ; then
	exit $?
fi

pass=0
fail=0

function sk_test 
{
	while read expect line ; do
                if expr "$expect" : '#' >/dev/null ; then
                        continue
                fi
		$SK "$line"
		ex=$?
		if [ $expect = $ex ]; then
			((pass = $pass + 1))
			echo "-OK- $SK $line"
		else
			((fail = $fail + 1))
			echo "FAIL expected $expect, got $ex: $SK $line"
		fi
	done
}

sk_test <<EOF
0 all
0 ( all )
0 not all
0 text atom
0 or all all
0 or all (all)
0 or (all) all
0 or (all) (all)
0 or (all text word) all
0 text word text word text word all
0 text "some clever \"words\"" all
0 larger 123 smaller 1024
0 1
0 1,2,3
0 1:3
0 1,2:10,30 all
0 not ( 1,2:10,30 )
1 foo
1 ( all 
1 ( all ) )
1 all foo
1 not foo
1 text
1 or
1 or all
1 text word bogus text string
1 1;10
1 1all
EOF

echo
echo "-OK- $pass"
echo "FAIL $fail"
