#!/bin/sh

#### Method 1

cat <<'EOT' >ixhash1.eg
1 2	3   4 5		6 NL
7 8 9   10 11 12 13 14 15 16 NL
EOT

cat ixhash1.eg | tr -s '[:space:]' | tr -d '[:graph:]' | tee ixhash1.out  | md5

#### Method 2

cat <<'EOT' >ixhash2.eg
<html><body><!-- page-break-control-character --><a
href="http://www.snert.com/">Snert.com</a>__wow__percent(%)ampersand(&)hash(#)semi-colon(;)equals(=)</html>
EOT

cat ixhash2.eg | tr -d '[:cntrl:][:alnum:]%&#;=' | tr '_' '.' | tr -s '[:print:]' | tee ixhash2.out | md5

#### Method 3

cat <<'EOT' >ixhash3.eg
abcdeeefgh
EOT

cat ixhash3.eg | tr -d '[:cntrl:][:space:]=' | tr -s '[:graph:]' | tee ixhash3.out | md5

exit 0
