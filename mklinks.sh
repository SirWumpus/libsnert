#!/bin/sh

#
#	LibSnert Directory Structure. 
#
#	org/sqlite3/
#	com/snert/src/lib/
#	com/snert/include/com/snert/lib@	-> com/snert/src/lib/include
#	com/snert/include/org/valgrind@		-> com/snert/src/lib/include/valgrind
#

cd ../..
mkdir -p lib include/com/snert include/org
cd include/com/snert
ln -s ../../../src/lib/include lib
cd -
cd include/org
ln -s ../../src/lib/include/valgrind valgrind
cd -

exit 0
