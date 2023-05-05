dnl
dnl acsite.m4
dnl
dnl Copyright 2002, 2015 by Anthony Howe.  All rights reserved.
dnl

dnl
dnl SNERT_JOIN_UNIQ(var, word_list,[head|tail])
dnl
AC_DEFUN([SNERT_JOIN_UNIQ],[
	list=`eval echo \$$1`
	AS_IF([test -n "$2"],[
		for item in $2; do
			AS_IF([expr " $list " : ".* $item " >/dev/null],[
			],[
				AS_IF([test "$3" = 'head'],[
					list="$item${list:+ $list}"
				],[
					list="${list:+$list }$item"
				])
			])
		done
	])
	eval $1="\"$list\""
])

dnl
dnl SNERT_CHECK_DEFINE(symbol[, header_file])
dnl
dnl Without a header_file, check for a predefined macro.
dnl
AC_DEFUN([SNERT_CHECK_DEFINE],[
	AC_LANG_PUSH([C])
	AC_CACHE_CHECK([for $1 macro],ac_cv_define_$1,[
		AS_IF([test -z "$2"],[
			AC_RUN_IFELSE([
				AC_LANG_SOURCE([[
int main()
{
#ifdef $1
	return 0;
#else
	return 1;
#endif
}
				]])
			],[
				ac_cv_define_$1=yes
			],[
				ac_cv_define_$1=no
			])
		],[
			AC_RUN_IFELSE([
				AC_LANG_SOURCE([[
#include <$2>
int main()
{
#ifdef $1
	return 0;
#else
	return 1;
#endif
}
				]])
			],[
				ac_cv_define_$1=yes
			],[
				ac_cv_define_$1=no
			])
		])
	])
	AC_LANG_POP([C])
	AS_IF([test $ac_cv_define_$1 = 'yes'],[
		AC_DEFINE_UNQUOTED([HAVE_MACRO_]translit($1, [a-z], [A-Z]))
		AH_TEMPLATE([HAVE_MACRO_]translit($1, [a-z], [A-Z]))
	])
])

dnl
dnl SNERT_CHECK_PREDEFINE(symbol)
dnl
AC_DEFUN(SNERT_CHECK_PREDEFINE,[
	SNERT_CHECK_DEFINE($1)
])

dnl
dnl SNERT_DEFINE(name[, value])
dnl
AC_DEFUN([SNERT_DEFINE],[
	name=AS_TR_CPP($1)
	AS_IF([test -n "$2"],[value="$2"],[eval value="\$$name"])
	AC_DEFINE_UNQUOTED($name,["$value"])
])

dnl
dnl SNERT_FIND_FILE(wild_file, directories, if-found, not-found)
dnl if-found can reference the found $dir_val
dnl
AC_DEFUN([SNERT_FIND_FILE],[
	AS_VAR_PUSHDEF([snert_dir], [snert_find_file_$1])
	AS_VAR_SET([snert_dir],'no')
	AC_MSG_CHECKING([for location of $1])

	dnl File to find specifies an extension?
	AS_IF([expr "$1" : '.*\.[[0-9a-zA-Z]]' >/dev/null],[
		dnl Has an extension.
		pattern="$1"
	],[
		dnl No extension, so look for any extension (.a and .so variants).
		dnl Without the dot (.) to mark the end of the name prefix
		dnl we can inadvertantly match libraries with similar prefixes,
		dnl ie. libz and libzephyr
		pattern="$1.*"
	])
	for d in $2; do
		AS_IF([ls -1 $d/$pattern >/dev/null 2>&1],[
			AS_VAR_SET([snert_dir],[$d])
			break
		])
	done

	AS_VAR_COPY([dir_val],[snert_dir])
dnl	AS_IF([test "$dir_val" = 'no'],AC_MSG_RESULT(no),AC_MSG_RESULT(yes))
	AC_MSG_RESULT($dir_val)
	AS_VAR_IF([snert_dir],[no],[$4],[$3])
	AS_VAR_POPDEF([snert_dir])
])

dnl
dnl SNERT_FIND_DIR(subdir, directories, if-found, not-found)
dnl
AC_DEFUN([SNERT_FIND_DIR],[
	AS_VAR_PUSHDEF([snert_dir], [snert_find_file_$1])
	AS_VAR_SET([snert_dir],'no')
	AC_MSG_CHECKING([for location of $1])

	for d in $2; do
		AS_IF([test -d "$d/$1"],[
			AS_VAR_SET([snert_dir],[$d])
			break
		])
	done

	AS_VAR_COPY([dir_val],[snert_dir])
	AC_MSG_RESULT($dir_val)
	AS_VAR_IF([snert_dir],[no],[$4],[$3])
	AS_VAR_POPDEF([snert_dir])
])


dnl
dnl SNERT_CHECK_PACKAGE_HEADER(header, if-found, not-found[, extra_dirs])
dnl
AC_DEFUN([SNERT_CHECK_PACKAGE_HEADER],[
	SNERT_FIND_FILE([$1],[$4 /usr/pkg/include /usr/local/include /usr/include],[$2],[$3])
])

dnl
dnl SNERT_CHECK_PACKAGE_LIB(library, if-found, not-found[, extra_dirs])
dnl
AC_DEFUN([SNERT_CHECK_PACKAGE_LIB],[
	SNERT_FIND_FILE([$1],[$4 /usr/pkg/lib /usr/local/lib /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib /lib64 /lib/x86_64-linux-gnu /lib],[$2],[$3])
])

dnl
dnl SNERT_IF_SYSTEM_DIR(word, if-system, not-system)
dnl
m4_define([SNERT_IF_SYSTEM_DIR],[
	AS_CASE([$1],[/usr/include|/usr/lib64|/usr/lib/x86_64-linux-gnu|/usr/lib|/lib64|/lib/x86_64-linux-gnu|/lib],[$2],[$3])
])

dnl
dnl SNERT_CHECK_PACKAGE(
dnl	name, headers, libs, [funcs],
dnl	with_base, with_inc, with_lib,
dnl	[extra_includes], [define_and_subst=true]
dnl )
dnl
AC_DEFUN([SNERT_CHECK_PACKAGE],[
	AS_ECHO()
	AS_ECHO("Checking for $1 package...")
	AS_ECHO()

dnl	with_name=`AS_ECHO([$1]) | tr '[[a-z -]]' '[[A-Z_]]'`

	dnl Watch out for leading and trailing whitespace with m4 macros;
	dnl everything delimited by open/close paren and/or commas is
	dnl part of the argument, so pretty formatting for readability
	dnl can screw with string compares.  Use echo to trim whitespace.
	with_base=`AS_ECHO([$5])`

AS_IF([test "$with_base" != 'no'],[
	dnl Careful with --with options as they can be specified,
	dnl without a base directory path, in which case ignore it.
	AS_IF([test "$with_base" = 'yes'],[
		with_base=''
	])

dnl	dnl Save any preset flags.
dnl	eval extra_LIBS="\${LIBS_$1}"
dnl	eval extra_LDFLAGS="\${LDFLAGS_$1}"
dnl	eval extra_CPPFLAGS="\${CPPFLAGS_$1}"

	found=0
	headers=0
	for f in $2; do
		headers=`expr $headers + 1`
		cache_id=AS_TR_SH(ac_cv_header_$f)
		SNERT_CHECK_PACKAGE_HEADER([$f],[
			found=`expr $found + 1`
			dnl Remember the location we found the header
			dnl even if its a system directory.
			have=AS_TR_CPP(HAVE_$f)
			dnl Wanted to have the filepath saved in the
			dnl macro, but for backwards compatibility with
			dnl HAVE_header_h defines set by other tests best
			dnl keep it the same value to avoid warnings (or
			dnl error with -Werror).
			AC_DEFINE_UNQUOTED($have,[1])
			dnl Here we REALLY need the filepath for other
			dnl potential configure tests.
			AC_CACHE_VAL($cache_id,[eval $cache_id="\"$dir_val/$f\""])

			SNERT_IF_SYSTEM_DIR([$dir_val],[
				dnl Ignore system directories.
				SNERT_JOIN_UNIQ([CPPFLAGS_$1])
			],[
				SNERT_JOIN_UNIQ([CPPFLAGS_$1],["-I$dir_val"],[head])
			])
		],[
			AC_CACHE_VAL($cache_id,[eval $cache_id='no'])
		],[${with_base:+$with_base/include} $6])
	done

	dnl If we don't have all the headers, skip checking for the libraries.
	dnl Consider the case where we have libssl et al., but not the headers.
	AS_IF([test $headers -eq $found], [
		for f in $3; do
			SNERT_CHECK_PACKAGE_LIB([$f],[
				have=AS_TR_CPP(HAVE_$f)
				AC_DEFINE_UNQUOTED($have,["$dir_val"])
				SNERT_IF_SYSTEM_DIR([$dir_val],[
					lib=`basename $f | sed -e's/^lib//'`
					SNERT_JOIN_UNIQ([LIBS_$1],["-l$lib"],[tail])
				],[
					SNERT_JOIN_UNIQ([LDFLAGS_$1],["-L$dir_val"])
					AS_IF([expr "$f" : '.*\.a$' >/dev/null],[
						dnl Explicit static library.
						SNERT_JOIN_UNIQ([LIBS_$1],["$dir_val/$f"],[tail])
					],[
						lib=`basename $f | sed -e's/^lib//'`
						SNERT_JOIN_UNIQ([LIBS_$1],["-l$lib"],[tail])
					])
				])
			],[],[${with_base:+$with_base/lib} $7])
		done
	])

	define_and_subst=`AS_ECHO([$9])`
	AS_CASE([$define_and_subst],
	[false|no|0],[
		dnl Caller wants to take care of this, possibly
		dnl to append extra flags before committing the
		dnl defines and substutions.
		:
	],[
		dnl Default.
		SNERT_DEFINE(CPPFLAGS_[$1])
		SNERT_DEFINE(LDFLAGS_[$1])
		SNERT_DEFINE(LIBS_[$1])
		AC_SUBST(CPPFLAGS_[$1])
		AC_SUBST(LDFLAGS_[$1])
		AC_SUBST(LIBS_[$1])

		dnl Deprecated versions to phase out.
		SNERT_DEFINE(CFLAGS_[$1],`eval echo \$CPPFLAGS_[$1]`)
		SNERT_DEFINE(HAVE_LIB_[$1],`eval echo \$LIBS_[$1]`)
		AC_SUBST(CFLAGS_[$1],`eval echo \$CPPFLAGS_[$1]`)
		AC_SUBST(HAVE_LIB_[$1],`eval echo \$LIBS_[$1]`)
	])

	AS_IF([test -n "$4"],[
		save_LIBS="$LIBS"
		save_LDFLAGS="$LDFLAGS"
		save_CPPFLAGS="$CPPFLAGS"

		eval LIBS=\"\$LIBS_$1 $LIBS\"
		eval LDFLAGS=\"\$LDFLAGS_$1 $LDFLAGS\"
		eval CPPFLAGS=\"\$CPPFLAGS_$1 $CPPFLAGS\"

		AC_CHECK_FUNCS([$4],[],[],[$8])

		CPPFLAGS="$save_CPPFLAGS"
		LDFLAGS="$save_LDFLAGS"
		LIBS="$save_LIBS"
	])
],[
	AC_MSG_NOTICE([Package $1 has been explicitly disabled.])
])
])

dnl
dnl SNERT_OPTION_ENABLE_DEBUG
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_DEBUG,[
	dnl Assert that CFLAGS is defined. When AC_PROC_CC is called to
	dnl check the compiler and CC == gcc is found and CFLAGS is
	dnl undefined, then it gets assigned "-g -O2", which is just
	dnl annoying when you want the default to no debugging.
	CPPFLAGS="${CPPFLAGS}"
	CFLAGS="${CFLAGS}"

	AC_ARG_ENABLE(debug,[AS_HELP_STRING([--enable-debug],[enable compiler debug option])])
	AS_IF([test ${enable_debug:-no} = 'yes'],[
		CFLAGS="-g -O0${CFLAGS:+ $CFLAGS}"
	],[
		AC_DEFINE(NDEBUG,[1],[Disable debug code])
		enable_debug='no'
	])
])

dnl
dnl SNERT_OPTION_ENABLE_TRACK
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_TRACK,[
	AC_ARG_ENABLE(track,[AS_HELP_STRING([--enable-track],[enable memory leak tracking])],[
		dnl We define this through -D instead of config.h.
		CFLAGS="-DTRACK${CFLAGS:+ $CFLAGS}"
	])
])

dnl
dnl SNERT_SNERT
dnl
AC_DEFUN([SNERT_SNERT],[
	LIBSNERT='-lsnert'

	# Makefile macro to compile C into an object file.
	COMPILE='${CC} ${CFLAGS} ${CC_O} $''*$O -c $<'
dnl	COMPILE='${CC} ${CFLAGS} -c $<'

	# Makefile macro to archive an object file.
	ARCHIVE='${AR} rc ${LIB} $$obj'

	# Assume the following traditional extensions.
	ac_libext='a'
	LIBEXT=$ac_libext

	AC_SUBST(COMPILE)
	AC_SUBST(ARCHIVE)
	AC_SUBST(LIBEXT)
	AC_SUBST(LIBSNERT)

	AC_CHECK_TOOL(RANLIB, ranlib, true)
])

AC_DEFUN([SNERT_CC_INFO],[
	AC_REQUIRE([SNERT_OPTION_ENABLE_DEBUG])
	AC_REQUIRE([AC_PROG_CC])
	AC_USE_SYSTEM_EXTENSIONS
	AC_LANG([C])

	AS_IF([test "$GCC" = 'yes'],[
		GCC_MAJOR=`$CC -dM -E -xc /dev/null | sed -n -e 's/.*__GNUC__ \(.*\)/\1/p'`
		GCC_MINOR=`$CC -dM -E -xc /dev/null | sed -n -e 's/.*__GNUC_MINOR__ \(.*\)/\1/p'`
		GCC_PATCH=`$CC -dM -E -xc /dev/null | sed -n -e 's/.*__GNUC_PATCHLEVEL__ \(.*\)/\1/p'`
		dnl Nothing wrong using a char for a subscript.
		AS_IF([test $GCC_MAJOR -ge 3],[CFLAGS="-Wno-char-subscripts${CFLAGS:+ $CFLAGS}"])
dnl This list keeps getting bigger with each version of gcc.  Turn them all off.
dnl		dnl Option to ignore extra support functions.
dnl		AS_IF([test $GCC_MAJOR -gt 4 -o \( $GCC_MAJOR -eq 4 -a $GCC_MINOR -ge 3 \) ],[CFLAGS="-Wno-unused-function${CFLAGS:+ $CFLAGS}"])
dnl		dnl Option to silience Valgrind and ProtoThread macro warnings.
dnl		AS_IF([test $GCC_MAJOR -gt 4 -o \( $GCC_MAJOR -eq 4 -a $GCC_MINOR -ge 6 \) ],[CFLAGS="-Wno-unused-but-set-variable${CFLAGS:+ $CFLAGS}"])
dnl		dnl Option to silience warnings for const I might need.
dnl		AS_IF([test $GCC_MAJOR -gt 6 -o \( $GCC_MAJOR -eq 6 -a $GCC_MINOR -ge 2 \) ],[CFLAGS="-Wno-unused-const-variable${CFLAGS:+ $CFLAGS}"])
		AS_IF([test $GCC_MAJOR -gt 3],[CFLAGS="-Wno-unused${CFLAGS:+ $CFLAGS}"])
		CFLAGS="-Wall $CFLAGS"
	])
	AS_IF([test ${platform:-UNKNOWN} = 'CYGWIN'],[
		AS_IF([test ${enable_debug:-no} = 'no'],[CFLAGS="-s${CFLAGS:+ $CFLAGS}"])
		CFLAGS="-I/usr/include/w32api${CFLAGS:+ $CFLAGS}"
		LDFLAGS="-L/usr/lib/w32api${LDFLAGS:+ $LDFLAGS}"

dnl 		AS_IF([test ${enable_win32:-no} = 'yes'],[
dnl 			dnl -s		strip, no symbols
dnl 			dnl -mno-cygwin	native windows console app
dnl 			dnl -mwindows	native windows gui app
dnl 			dnl -lws2_32	WinSock2 library
dnl 			CFLAGS="-mno-cygwin ${CFLAGS}"
dnl 			LIBS="-lws2_32 ${LIBS}"
dnl 		])
	])

	dnl Tradional cc options.
	dnl NOTE SunOS as(1) _wants_ a space between -o and its argument.
	CC_E='-o'
	CC_E_NAME='-o $@'
	CC_O='-o'
	CC_O_NAME='-o $''*$O'
	LD=$CC

	AC_SUBST(CC_E)
	AC_SUBST(CC_E_NAME)
	AC_SUBST(CC_O)
	AC_SUBST(CC_O_NAME)

	dnl Check for recent ANSI C additions that HAVE_HEADER_STDC check
	dnl doesn't distinguish between C89 and C99.
	AC_CHECK_HEADERS([stdarg.h])
	SNERT_CHECK_DEFINE([va_copy], [stdarg.h])

	AC_CHECK_SIZEOF(short)
	AC_CHECK_SIZEOF(int)
	AC_CHECK_SIZEOF(long)
	AC_CHECK_SIZEOF(size_t)
	AC_CHECK_SIZEOF(off_t)
	AC_CHECK_SIZEOF(double)
	AC_CHECK_SIZEOF(long double)
	AC_CHECK_SIZEOF(long long int)

	SNERT_SNERT
])

AC_DEFUN([SNERT_TAR_SETTINGS],[
	AC_MSG_CHECKING(for tar file list option to use)
	AS_IF([tar --version 2>&1 | grep '(GNU tar)' >/dev/null],[
		TAR_I='-T'
	],[
		TAR_I='-I'
	])
	AC_SUBST(TAR_I)
	AC_MSG_RESULT($TAR_I)
])

dnl
dnl SNERT_CHECK_LIB(lib, symbol)
dnl
dnl Only check AC_CHECK_LIB if $lib not already in $LIBS
dnl
AC_DEFUN(SNERT_CHECK_LIB,[
	AS_IF([echo "$LIBS" | grep -- "-l$1" >/dev/null],[
		echo "checking for $2 in $1... (cached) yes"
	],[
		dnl For SunOS. Beware of using AC_SEARCH_LIBS() on SunOS
		dnl platforms, because some functions appear as stubs in
		dnl other libraries.
		AC_CHECK_LIB([$1], [$2])
	])
])

dnl
dnl SNERT_GET_NUMERIC_DEFINE(header_file, symbol)
dnl
AC_DEFUN(SNERT_GET_NUMERIC_DEFINE,[
	AS_VAR_PUSHDEF([ac_Header], [snert_cv_define_$1_$2])dnl
	AC_CACHE_CHECK([for $2],[ac_Header],[
		AC_RUN_IFELSE([
			AC_LANG_SOURCE([[
#include <stdio.h>
#include <$1>
int main()
{
#ifdef $2
	FILE *fp;
	if ((fp = fopen("snert_output.txt", "w")) == NULL)
		return 1;
	fprintf(fp, "%d", $2);
	fclose(fp);
	return 0;
#else
	return 1;
#endif
}
			]])
		],[
			AS_VAR_SET([ac_Header], [[`cat snert_output.txt`]])
			rm -f snert_output.txt
		])
	])
	AS_VAR_POPDEF([ac_Header])dnl
])

AC_DEFUN(SNERT_OPTION_DB_185,[
	AC_ARG_ENABLE(db-185
		[AS_HELP_STRING([[--enable-db-185 ]],[link with DB 1.85])]
	)
])

dnl
dnl SNERT_BERKELEY_DB
dnl
dnl	Sets $with_db to yes or no
dnl	Sets $db_lib to the library -l option or an empty string.
dnl
AC_DEFUN(SNERT_OPTION_WITH_DB,[
	AC_ARG_WITH(db, [[  --with-db[=DIR]         Berkeley DB package, optional base directory]])
	AC_ARG_WITH(db-inc, [[  --with-db-inc=DIR       specific Berkeley DB include directory]])
	AC_ARG_WITH(db-lib, [[  --with-db-lib=DIR       specific Berkeley DB library directory]])
])
AC_DEFUN(SNERT_BERKELEY_DB,[
AS_IF([test ${with_db:-yes} != 'no'],[
	echo
	echo "Check for Berkeley DB support..."
	echo

	bdb_save_LIBS=$LIBS
	bdb_save_CFLAGS=$CFLAGS
	bdb_save_LDFLAGS=$LDFLAGS

	dnl Short list of system directories to try.
	AS_IF([ test -d "$with_db_inc" ],[
		BDB_DIRS="$with_db_inc"
	],[ test -d "$with_db/include" ],[
		BDB_DIRS="$with_db/include"
	],[
		BDB_DIRS="/opt /usr/pkg/include /usr/local/include /usr/include"
	])

	dnl Find all instances of db.h
	AC_LANG_PUSH(C)
	bdb_best_major=-1
	bdb_best_minor=-1
	for h in `find $BDB_DIRS -name db.h 2>/dev/null`; do
		AC_MSG_CHECKING($h)
		I_DIR=`dirname $h`

		dnl Version subdirectory.
		v=`basename $I_DIR`
		if test $v = 'include' ; then
			d=$I_DIR
			v=''
		else
			d=`dirname $I_DIR`
		fi
		d=`dirname $d`

		dnl Determine matching lib directory.
		if test -d "$with_db_lib" ; then
			L_DIR="$with_db_lib"
		elif test -d $d/lib64/$v ; then
			L_DIR="$d/lib64/$v"
		elif test -d $d/lib/$v ; then
			L_DIR="$d/lib/$v"
		elif test -d $d/lib64 ; then
			L_DIR="$d/lib64"
		else
			L_DIR="$d/lib"
		fi

		dnl Don't need to add system locations to options.
		if test ${I_DIR} != '/usr/include' ; then
			CFLAGS="-I$I_DIR $CFLAGS"
		fi
		if test ${L_DIR} != '/usr/lib64' -a ${L_DIR} != '/usr/lib' ; then
			LDFLAGS="-L$L_DIR $LDFLAGS"
		fi

		dnl Extract the version number.
		bdb_major=`grep DB_VERSION_MAJOR $h | cut -f 3`
		if test -n "$bdb_major" ; then
			bdb_minor=`grep DB_VERSION_MINOR $h | cut -f 3`
			bdb_create='db_create'
		else
			dnl Assume oldest version commonly found used by BSD variants.
			bdb_major=1
			bdb_minor=85
			bdb_create='dbopen'
			check_libc='c'
		fi

		AC_MSG_RESULT(${bdb_major}.${bdb_minor})

		dnl Library search based on include version directory.
		for l in $v db $check_libc ; do
			if test "$l" != 'c'; then
				LIBS="-l$l $LIBS"
			fi
			bdb_name=$l

			AC_MSG_CHECKING([for $bdb_create in lib$bdb_name])
			AC_LINK_IFELSE([
				AC_LANG_SOURCE([[
#include <stdlib.h>
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#include <db.h>

int
main(int argc, char **argv)
{
#ifdef DB_VERSION_MAJOR
	DB *db = NULL;
	return db_create(&db, NULL, 0) != 0 || db == NULL;
#else
	DB *db = dbopen("access.db", 0, 0, 0, NULL);
	return 0;
#endif
}
				]])
			],[
				found=yes
				AC_MSG_RESULT(yes)
				AC_DEFINE_UNQUOTED(HAVE_DB_H)
				if test $bdb_major = 1 ; then
					AC_DEFINE_UNQUOTED(HAVE_DBOPEN)
				else
					AC_DEFINE_UNQUOTED(HAVE_DB_CREATE)
				fi

				dnl Assume newest is best.
				if test $bdb_major -gt $bdb_best_major \
				-o \( $bdb_major -eq $bdb_best_major -a $bdb_minor -gt $bdb_best_minor \); then
					bdb_best_major=$bdb_major
					bdb_best_minor=$bdb_minor
					if test -n "$l" ; then
						BDB_I_DIR=$I_DIR
						BDB_L_DIR=$L_DIR

						AC_SUBST(HAVE_LIB_DB, "-l$l")
						AC_SUBST(CFLAGS_DB, "-I$BDB_I_DIR")
						AC_SUBST(LDFLAGS_DB, "-L$BDB_L_DIR")

						AC_DEFINE_UNQUOTED(HAVE_LIB_DB, "-l$l")
						AC_DEFINE_UNQUOTED(CFLAGS_DB, "-I$BDB_I_DIR")
						AC_DEFINE_UNQUOTED(LDFLAGS_DB, "-L$BDB_L_DIR")
					fi
				fi
			],[
				AC_MSG_RESULT(no)
			])
			LIBS="$bdb_save_LIBS"
		done

		LDFLAGS="$bdb_save_LDFLAGS"
		CFLAGS="$bdb_save_CFLAGS"
	done
	AC_LANG_POP(C)
	if test $bdb_best_major -gt -1; then
		bdb_version="$bdb_best_major.$bdb_best_minor"
		AC_DEFINE_UNQUOTED(HAVE_DB_MAJOR, $bdb_best_major)
		AC_DEFINE_UNQUOTED(HAVE_DB_MINOR, $bdb_best_minor)
		AC_MSG_RESULT([checking best Berkeley DB version... $bdb_version])
	else
		AC_MSG_RESULT([checking best Berkeley DB version... not found])
	fi

	AH_VERBATIM([HAVE_DB_MAJOR],[
/*
 * Berkeley DB
 */
#undef IGNORE_CORRUPT_CACHE_ISSUE_WITH_DB_185
#undef HAVE_DB_MAJOR
#undef HAVE_DB_MINOR
#undef HAVE_DB_CREATE
#undef HAVE_DBOPEN
#undef HAVE_DB_H
#undef HAVE_LIB_DB
#undef LDFLAGS_DB
#undef CFLAGS_DB
	])

	LDFLAGS="$bdb_save_LDFLAGS"
	CFLAGS="$bdb_save_CFLAGS"
	LIBS="$bdb_save_LIBS"
])
])

dnl
dnl SNERT_FIND_LIB([name],[found],[notfound])
dnl
m4_define([SNERT_FIND_LIB],[
	AS_VAR_PUSHDEF([snert_lib], [snert_find_lib_$1])dnl
	AS_ECHO
	AS_ECHO("Finding dynamic library $1 ...")
	AS_ECHO
	AS_VAR_SET([snert_lib], 'no')
	AC_CHECK_HEADER([dlfcn.h], [
		AC_DEFINE(HAVE_DLFCN_H,[],[dynamic library support])
		AC_CHECK_TOOL(ldd_tool, ldd)
		AS_IF([test ${ldd_tool:-no} != 'no'],[
			AC_CHECK_LIB([dl],[dlopen])
			AC_MSG_CHECKING([for $1])
			AC_RUN_IFELSE([
				AC_LANG_SOURCE([[
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif

int
main(int argc, char **argv)
{
	void *handle;
	handle = dlopen(argv[1], RTLD_NOW);
	return dlerror() != NULL;
}
				]])
			],[
				libpath=[`$ldd_tool ./conftest$ac_exeext | sed -n -e "/lib$1/s/.* \([^ ]*lib$1[^ ]*\).*/\1/p"`]
				AS_IF([./conftest$ac_exeext ${libpath:-unknown}],[
					AS_VAR_SET([snert_lib], [${libpath}])
				],[
					AS_VAR_SET([snert_lib], 'no')
				])
			])
			AC_MSG_RESULT(AS_VAR_GET([snert_lib]))
		])
	])
	AS_IF([test AS_VAR_GET([snert_lib]) != 'no'], [$2], [$3])[]
	AS_VAR_POPDEF([snert_lib])
])

AC_DEFUN(SNERT_FIND_LIBC,[
	SNERT_FIND_LIB([c],[
		AC_DEFINE_UNQUOTED(LIBC_PATH, ["$snert_find_lib_c"])
		AH_TEMPLATE([LIBC_PATH],[C Library Path (see DebugMalloc)])
	], [])
])


dnl
dnl SNERT_PLATFORM
dnl
AC_DEFUN(SNERT_PLATFORM,[
	dnl TODO migrate $platform to $target_os
	dnl AC_CANONICAL_TARGET

	platform=`uname -s|sed -e 's/^\([[a-zA-Z0-9]]*\)[[^a-zA-Z0-9]].*/\1/'`
	AC_SUBST(platform, $platform)
	AS_ECHO("platform is... $platform")
	AS_IF([test -e /etc/debian_version],[
		echo "this Linux is a Debian" `cat /etc/debian_version`
		apt-get install -y gcc libc6-dev
		isDebian='yes'
	])

	AC_PATH_PROGS([MD5SUM],[md5sum md5])

	AS_CASE([$platform],
	[NetBSD],[
dnl		LDFLAGS="-Wl,-R/usr/pkg/lib $LDFLAGS"
		:
	])

	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_PLATFORM, [["${platform}"]])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_BUILD_HOST, [["`hostname`"]])
])

dnl
dnl SNERT_CHECK_CONFIGURE
dnl
AC_DEFUN(SNERT_CHECK_CONFIGURE,[
	# When we have no makefile, do it ourselves...
dnl	snert_configure_command="$[]0 $[]@"

	AC_PATH_PROGS([AUTOCONF],[autoconf-2.69 autoconf],[no])

	AS_IF([test ${AUTOCONF:-no} != 'no' -a \( acsite.m4 -nt configure -o configure.ac -nt configure \)],[
		AS_ECHO('Rebuilding the configure script first...')
		${AUTOCONF} -f
		AS_ECHO('Restarting configure script...')
		AS_ECHO
		AS_ECHO("$snert_configure_command")
 		exec $snert_configure_command
	])
])

dnl
dnl SNERT_LIBSNERT
dnl
AC_DEFUN(SNERT_LIBSNERT,[
	saved_cflags=$CFLAGS
	saved_ldflags=$LDFLAGS

	CFLAGS="-I../../include $CFLAGS"
	LDFLAGS="-L../../lib $LDFLAGS"

	AC_CHECK_LIB(snert, parsePath)
	if test "$ac_cv_lib_snert_parsePath" = 'no'; then
		AS_ECHO
		AC_MSG_WARN([The companion library, LibSnert, is required.])
		AS_ECHO
		ac_cv_lib_snert_parsePath='required'
		CFLAGS=$saved_cflags
		LDFLAGS=$saved_ldflags
	fi
	snert_libsnert=$ac_cv_lib_snert_parsePath

	])

AC_DEFUN(SNERT_OPTION_ENABLE_32BIT,[
	AC_ARG_ENABLE(32bit,
		[AS_HELP_STRING([--enable-32bit ],[enable compile & link options for 32-bit])],
		[
			CFLAGS="-m32${CFLAGS:+ $CFLAGS}"
			LDFLAGS="-m32${LDFLAGS:+ $LDFLAGS}"
		]
	)
])

AC_DEFUN(SNERT_OPTION_ENABLE_64BIT,[
	AC_ARG_ENABLE(64bit,
		[AS_HELP_STRING([--enable-64bit ],[enable compile & link options for 64-bit])],
		[
			CFLAGS="-m64${CFLAGS:+ $CFLAGS}"
			LDFLAGS="-m64${LDFLAGS:+ $LDFLAGS}"
		],[
			dnl Option not specified, then choose based on CPU.
			case `uname -m` in
			x86_64|amd64)
				CFLAGS="-m64${CFLAGS:+ $CFLAGS}"
				LDFLAGS="-m64${LDFLAGS:+ $LDFLAGS}"
				;;
			i386)
				CFLAGS="-m32${CFLAGS:+ $CFLAGS}"
				LDFLAGS="-m32${LDFLAGS:+ $LDFLAGS}"
				;;
			esac
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_WIN32
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_MINGW,[
	AC_ARG_ENABLE(mingw,
		[AS_HELP_STRING([--enable-mingw ],[generate native Windows application using mingw])],
		[
			enable_win32='yes'
			AC_SUBST(ENABLE_MINGW, 'yes')
 			CFLAGS="-mno-cygwin -I/usr/include/w32api ${CFLAGS}"
			LDFLAGS="-mno-cygwin -L/usr/lib/w32api ${LDFLAGS}"
		]
	)
])

dnl
dnl SNERT_OPTION_WITH_WINDOWS_SDK
dnl
AC_DEFUN(SNERT_OPTION_WITH_WINDOWS_SDK,[
	AC_ARG_WITH(windows-sdk,
		[AS_HELP_STRING([--with-windows-sdk=dir ],[Windows Platform SDK base directory])],
		[
			enable_win32='yes'
			AC_SUBST(WITH_WINDOWS_SDK, $with_windows_sdk)
			AC_DEFINE(WITH_WINDOWS_SDK, $with_windows_sdk,[Windows SDK])
 			CFLAGS="-mno-cygwin -I${with_windows_sdk}/include ${CFLAGS}"
			LDFLAGS="-mno-cygwin -L${with_windows_sdk}/lib ${LDFLAGS}"
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_BCC32
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_BCC32,[
	AC_ARG_ENABLE(bcc32,
		[AS_HELP_STRING([--enable-bcc32 ],[generate native Windows application using Borland C++ 5.5])],
		[
			enable_bcc32='yes'
			CC=bcc32
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_RUN_USER(default_user_name)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_RUN_USER,[
	AC_ARG_ENABLE(run-user,
		[AS_HELP_STRING([--enable-run-user=user ],[specifiy the process user name, default is "$1"])],
		[enable_run_user="$enableval"], [enable_run_user=$1]
	)
	AC_DEFINE_UNQUOTED(RUN_AS_USER, ["$enable_run_user"])
	AC_SUBST(enable_run_user)
])

dnl
dnl SNERT_OPTION_ENABLE_RUN_GROUP(default_group_name)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_RUN_GROUP,[
	AC_ARG_ENABLE(run-group,
		[AS_HELP_STRING([--enable-run-group=group ],[specifiy the process group name, default is "$1"])],
		[enable_run_group="$enableval"], [enable_run_group=$1]
	)
	AC_DEFINE_UNQUOTED(RUN_AS_GROUP, ["$enable_run_group"])
	AC_SUBST(enable_run_group)
])

dnl
dnl SNERT_OPTION_ENABLE_CACHE_TYPE(default_cache_type)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_CACHE_TYPE,[
	AC_ARG_ENABLE(cache-type,
		[AS_HELP_STRING([--enable-cache-type=type ],[specifiy the cache type: bdb, flatfile, hash])],
		[
			# Force a specific type.
			case "$enableval" in
			bdb|flatfile|hash)
				enable_cache_type="$enableval"
				AC_DEFINE_UNQUOTED(CACHE_TYPE, ["$enable_cache_type"])
				;;
			*)
				enable_cache_type="$1"
			esac
		], [
			# Depends on whether Berkeley DB is available.
			enable_cache_type="$1"
		]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_CACHE_FILE
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_CACHE_FILE,[
	AC_ARG_ENABLE(cache-file,
		[AS_HELP_STRING([--enable-cache-file=filepath ],[specifiy the cache file])],
		[
			enable_cache_file="$enableval"
		], [
			if test -d /var/cache ; then
				# Linux http://www.pathname.com/fhs/pub/fhs-2.3.html
				enable_cache_file='${localstatedir}/cache/${PACKAGE_NAME}.db'
			elif test -d /var/db ; then
				# FreeBSD, OpenBSD, NetBSD
				enable_cache_file='${localstatedir}/db/${PACKAGE_NAME}.db'
			else
				echo "Cannot find a suitable location for the cache file."
				echo "Please specify --enable-cache-file."
				exit 1
			fi
		]
	)
	snert_cache_file=`eval echo $enable_cache_file`
	AC_DEFINE_UNQUOTED(CACHE_FILE, ["`eval echo ${snert_cache_file}`"])
	AC_SUBST(snert_cache_file)
	AC_SUBST(enable_cache_file)
])

dnl
dnl SNERT_OPTION_ENABLE_PID(default)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_PID,[
	AC_ARG_ENABLE(pid,
		[AS_HELP_STRING([--enable-pid=filepath ],[specifiy an alternative pid file path])],
		[
		],[
			dnl Almost all unix machines agree on this location.
			if test -z "$1"; then
				enable_pid='${localstatedir}/run/${PACKAGE_NAME}.pid'
			else
				enable_pid=$1
			fi
		]
	)
	snert_pid_file=`eval echo $enable_pid`
	AC_DEFINE_UNQUOTED(PID_FILE, ["`eval echo ${snert_pid_file}`"])
	AC_SUBST(snert_pid_file)
	AC_SUBST(enable_pid)
])

dnl
dnl SNERT_OPTION_ENABLE_SOCKET(default)
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_SOCKET,[
	AC_ARG_ENABLE(socket,
		[AS_HELP_STRING([--enable-socket=filepath ],[specifiy an alternative Unix domain socket])],
		[
		],[
			dnl Almost all unix machines agree on this location.
			dnl Note though that if you have more than one file
			dnl here, its recommended to create a subdirectory
			dnl with all the related files.
			if test -z "$1"; then
				enable_socket='${localstatedir}/run/${PACKAGE_NAME}.socket'
			else
				enable_socket=$1
			fi
		]
	)
	snert_socket_file=`eval echo $enable_socket`
	AC_DEFINE_UNQUOTED(SOCKET_FILE, ["`eval echo ${snert_socket_file}`"])
	AC_SUBST(snert_socket_file)
	AC_SUBST(enable_socket)
])

dnl
dnl SNERT_OPTION_ENABLE_POPAUTH
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_POPAUTH,[
	AC_ARG_ENABLE(popauth,
		[AS_HELP_STRING([--enable-popauth ],[enable POP-before-SMTP macro checking in smf API])],
		[AC_DEFINE(HAVE_POP_BEFORE_SMTP,[],[Enable POP-before-SMTP])]
	)
])

dnl
dnl SNERT_OPTION_ENABLE_STARTUP_DIR
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_STARTUP_DIR,[
	AC_ARG_ENABLE(startup-dir,
		[AS_HELP_STRING([--enable-startup-dir=dir ],[specifiy the startup script directory location])],
		[
			STARTUP_DIR="$enableval"
			STARTUP_EXT='.sh'
		], [
			if test -d '/usr/local/etc/rc.d'; then
				# FreeBSD
				STARTUP_DIR='/usr/local/etc/rc.d'
				STARTUP_EXT='.sh'
			elif test -d '/etc/init.d'; then
				# SunOS, Debian Linux, System V variant
				STARTUP_DIR='/etc/init.d'
			elif test -d '/etc/rc.d/init.d'; then
				# Linux, System V variant
				STARTUP_DIR='/etc/rc.d/init.d'
			else
				# OpenBSD has no place to put startup
				# scripts that might be used by rc.local.
				STARTUP_DIR='/usr/local/sbin'
				STARTUP_EXT='.sh'
			fi
		]
	)
	AC_SUBST(STARTUP_DIR)
	AC_SUBST(STARTUP_EXT)
])

dnl
dnl SNERT_OPTION_WITH_SENDMAIL
dnl
AC_DEFUN(SNERT_OPTION_WITH_SENDMAIL,[
	AC_ARG_WITH(sendmail,
		[AS_HELP_STRING([--with-sendmail=dir ],[directory where sendmail.cf lives, default /etc/mail])],
		[with_sendmail="$withval"], [with_sendmail='/etc/mail']
	)
	AC_SUBST(with_sendmail)
])

dnl
dnl SNERT_OPTION_ENABLE_FORK
dnl
AC_DEFUN(SNERT_OPTION_ENABLE_FORK,[
	AC_ARG_ENABLE(
		fork, [AS_HELP_STRING([--enable-fork],[use process fork model instead of threads])],
		[
			AC_DEFINE(ENABLE_FORK,[],[use process fork model instead of threads])
		]
	)
	AC_SUBST(enable_fork)
])

AC_DEFUN(SNERT_OPTION_ENABLE_FCNTL_LOCKS,[
	AC_ARG_ENABLE(fcntl-locks,[AS_HELP_STRING([--enable-fcntl-locks],[use fcntl() file locking instead of flock()])],[
		AC_DEFINE(ENABLE_ALT_FLOCK,[1],[Enable alternative flock using fcntl.])
	],[
		dnl Option not specified, choose default based on OS.
		AS_CASE([$platform],
		[Linux],[
			enable_fcntl_locks='yes'
		])
	])
	AC_SUBST(enable_alt_flock)
])

AC_DEFUN(SNERT_FILE_LOCKS,[
	AS_ECHO()
	AS_ECHO("Check for file locking...")
	AS_ECHO()
	AC_CHECK_HEADERS([fcntl.h sys/file.h])
	AC_CHECK_FUNCS(flock fcntl lockf locking)
	SNERT_CHECK_DEFINE(O_BINARY, fcntl.h)
	SNERT_CHECK_DEFINE(LOCK_SH, fcntl.h)
	AH_VERBATIM(HAVE_LOCK_SH,[
/*
 * Define the flock() constants separately, since some systems
 * have flock(), but fail to define the constants in a header.
 * These values were taken from FreeBSD.
 */
#ifndef HAVE_MACRO_LOCK_SH
# define LOCK_SH	0x01		/* shared file lock */
# define LOCK_EX	0x02		/* exclusive file lock */
# define LOCK_NB	0x04		/* don't block when locking */
# define LOCK_UN	0x08		/* unlock file */
#endif
	])
])

dnl
dnl SNERT_POSIX_IO
dnl
AC_DEFUN(SNERT_POSIX_IO,[
	AS_ECHO()
	AS_ECHO("Check for POSIX File & Directory I/O support...")
	AS_ECHO()
	AC_HEADER_DIRENT
dnl autoconf says the following should be included:
dnl
dnl #if HAVE_DIRENT_H
dnl # include <dirent.h>
dnl # define NAMLEN(dirent) strlen((dirent)->d_name)
dnl #else
dnl # define dirent direct
dnl # define NAMLEN(dirent) (dirent)->d_namlen
dnl # if HAVE_SYS_NDIR_H
dnl #  include <sys/ndir.h>
dnl # endif
dnl # if HAVE_SYS_DIR_H
dnl #  include <sys/dir.h>
dnl # endif
dnl # if HAVE_NDIR_H
dnl #  include <ndir.h>
dnl # endif
dnl #endif

	AC_CHECK_HEADERS([unistd.h fcntl.h sys/stat.h utime.h])
	AC_CHECK_FUNCS([ \
		chdir getcwd mkdir rmdir closedir opendir readdir \
		chmod chown chroot fchmod stat fstat link rename symlink unlink umask utime \
		close creat dup dup2 ftruncate chsize truncate lseek open pipe read write \
		isatty getdtablesize fmemopen open_memstream open_wmemstream
	])
	AC_FUNC_CHOWN
])

dnl
dnl SNERT_POSIX_SIGNALS
dnl
AC_DEFUN(SNERT_POSIX_SIGNALS,[
	echo
	echo "Check for POSIX signal support..."
	echo
	AC_CHECK_HEADER([signal.h],[
		AC_DEFINE(HAVE_SIGNAL_H,[],[POSIX Signals])
		AC_CHECK_TYPES([sigset_t, struct sigaction, struct sigaltstack],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif
		])
		AC_CHECK_FUNCS([kill sigaltstack sigemptyset sigfillset sigaddset sigdelset sigismember])
		AC_CHECK_FUNCS([sigaction sigprocmask sigpending sigsuspend])

		dnl _POSIX_REAL_TIME_SIGNALS
		dnl AC_CHECK_FUNCS([sigwaitinfo sigtimedwait sigqueue])
	])
])

dnl
dnl SNERT_PROCESS
dnl
AC_DEFUN(SNERT_PROCESS,[
	echo
	echo "Check for process support..."
	echo
	AC_CHECK_HEADER([unistd.h],[
		AC_DEFINE_UNQUOTED(HAVE_UNISTD_H)
		AC_CHECK_FUNCS([getopt getuid getgid setuid setgid])
		AC_CHECK_FUNCS([geteuid getegid seteuid setegid getpgid setpgid])
		AC_CHECK_FUNCS([getresuid getresgid setresuid setresgid])
		AC_CHECK_FUNCS([setreuid getgroups setgroups initgroups])
		AC_CHECK_FUNCS([_exit exit daemon fork execl execle execlp execv execve execvp setsid])
	])
	AC_CHECK_HEADER([sys/wait.h],[
		AC_DEFINE(HAVE_SYS_WAIT_H,[],[Process Support])
		AC_CHECK_FUNCS([wait wait3 wait4 waitpid])
	])

	AC_CHECK_HEADER([sys/resource.h],[
		AC_DEFINE(HAVE_SYS_RESOURCE_H,[],[Process Resources])
		AC_CHECK_TYPES([struct rlimit, rlim_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <time.h>
#include <sys/resource.h>
		])
		AC_CHECK_FUNCS([getrlimit setrlimit])

	])
	AC_CHECK_HEADERS([limits.h sysexits.h])
	AC_CHECK_HEADERS([syslog.h],[
		# SunOS doesn't define PERROR.
		SNERT_CHECK_DEFINE(LOG_PERROR, syslog.h)
	])
])

dnl
dnl SNERT_SETJMP
dnl
AC_DEFUN(SNERT_SETJMP,[
	echo
	echo "Check for setjmp support..."
	echo
	AC_CHECK_HEADER([setjmp.h], [
		AC_DEFINE(HAVE_SETJMP_H,[],[setjmp support])
		AC_CHECK_TYPES([jmp_buf, sigjmp_buf],[],[],[
#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif
		])
		AH_VERBATIM([HAVE_SETJMP_H],[
#undef HAVE_SETJMP_H

#ifdef HAVE_SIGJMP_BUF
# define JMP_BUF		sigjmp_buf
# define SETJMP(e)		sigsetjmp(e, 0)
# define LONGJMP(e,v)		siglongjmp(e, v)
# define SIGSETJMP(e,f)		sigsetjmp(e, f)
# define SIGLONGJMP(e,v)	siglongjmp(e, v)
#else
# define JMP_BUF		jmp_buf
# define SETJMP(e)		setjmp(e)
# define LONGJMP(e, v)		longjmp(e, v)
# define SIGSETJMP(e,f)		setjmp(e)
# define SIGLONGJMP(e, v)	longjmp(e, v)
#endif

#ifndef SETJMP_PUSH
#define SETJMP_PUSH(this_jb) \
	{ JMP_BUF _jb; memcpy(&_jb, this_jb, sizeof (_jb))

#define SETJMP_POP(this_jb) \
	memcpy(this_jb, &_jb, sizeof (_jb)); }
#endif
		])
	])
])

dnl
dnl SNERT_OPTIONS
dnl
AC_DEFUN(SNERT_OPTIONS,[
	echo
	echo "Check for option support..."
	echo
	AC_CHECK_HEADER([unistd.h], [
		AC_DEFINE_UNQUOTED(HAVE_UNISTD_H)
		AC_CHECK_FUNCS([getopt])
	])
])

dnl
dnl SNERT_RANDOM
dnl
AC_DEFUN(SNERT_RANDOM,[
	echo
	echo "Check for random support..."
	echo
	AC_CHECK_HEADER([stdlib.h], [
		AC_DEFINE_UNQUOTED(HAVE_STDLIB_H)
		AC_CHECK_FUNCS([srand rand rand_r random srandom initstate setstate])
	])
])

dnl
dnl SNERT_POSIX_SEMAPHORES
dnl
AC_DEFUN(SNERT_POSIX_SEMAPHORES,[
	echo
	echo "Check for POSIX semaphore support..."
	echo
	AC_CHECK_HEADER([semaphore.h],[
		AC_DEFINE(HAVE_SEMAPHORE_H,[],[POSIX Semaphores])

		saved_libs=$LIBS
		LIBS=''

		AC_SEARCH_LIBS([sem_init],[rt pthread],[
			AS_IF([test "${ac_cv_search_sem_init}" = 'none required'],[ac_cv_search_sem_init=''])

			AC_DEFINE_UNQUOTED(HAVE_LIB_SEM, "${ac_cv_search_sem_init}")
			AH_TEMPLATE(HAVE_LIB_SEM,[POSIX Semaphores])
			AC_SUBST(HAVE_LIB_SEM, ${ac_cv_search_sem_init})
			SNERT_JOIN_UNIQ([NETWORK_LIBS],["${ac_cv_search_sem_init}"],[tail])
#			NETWORK_LIBS="${ac_cv_search_sem_init} $NETWORK_LIBS"
		])
		AC_CHECK_TYPES([sem_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SEMAPHORE_H
# include <semaphore.h>
#endif
		])
		AC_CHECK_FUNCS([sem_init sem_destroy sem_wait sem_post sem_trywait sem_timedwait])
		LIBS=$saved_libs
	])
])

dnl
dnl SNERT_SYSTEMV_SEMAPHORES
dnl
AC_DEFUN(SNERT_SYSTEMV_SEMAPHORES,[
	echo
	echo "Check for System V semaphore support..."
	echo
	snert_systemv_semaphores='yes'
	AC_CHECK_HEADERS([sys/ipc.h sys/sem.h],[],[snert_systemv_semaphores='no'],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
	])

	AC_CHECK_TYPES([union semun],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SYS_IPC_H
# include <sys/ipc.h>
#endif
#ifdef HAVE_SYS_SEM_H
# include <sys/sem.h>
#endif
	])

	AH_VERBATIM([HAVE_UNION_SEMUN],[
/* Netbsd sys/sem.h fails to define union semun. */
#ifndef HAVE_UNION_SEMUN
union semun {
	int     val;          	/* value for SETVAL */
	struct  semid_ds *buf;	/* buffer for IPC_{STAT,SET} */
	unsigned short *array;	/* array for GETALL & SETALL */
	struct seminfo *__buf;	/* buffer for IPC_INFO */
};
#endif
	])

	if test $snert_systemv_semaphores = 'yes'; then
		AC_CHECK_FUNCS([semget semctl semop],[],[
			snert_systemv_semaphores='no'
			break;
		])
	fi
])

dnl
dnl SNERT_PAM
dnl
AC_DEFUN(SNERT_PAM,[
	echo
	echo "Check for PAM support..."
	echo

	dnl Take care in detecting libpam, since its often an .so and
	dnl not all my software requires it. However, by detecting it,
	dnl its automatically added to $LIBS and so a version specific
	dnl reference could end up appearing in the dynamic library
	dnl start sequence. If the library has a major update, the old
	dnl one deleted, and the application didn't really require the
	dnl library, then it will refuse to start due to library version
	dnl change. Reported by Andrey Chernov
	dnl

	saved_libs=$LIBS

	AC_CHECK_LIB(pam, pam_start)
	if test "$ac_cv_lib_pam_pam_start" != 'no'; then
		AC_SUBST(HAVE_LIB_PAM, '-lpam')
	fi

	AC_CHECK_HEADERS(security/pam_appl.h)

	if test ${isDebian:-no} = 'yes' -a "$ac_cv_header_security_pam_appl_h" = 'no'; then
		apt-get install -y libpam0g-dev
		AS_UNSET(ac_cv_header_security_pam_appl_h)
		echo 'retrying after development package update...'
		AC_CHECK_HEADERS(security/pam_appl.h)
	fi

	AC_CHECK_FUNCS(pam_start pam_authenticate pam_end)

	LIBS=$saved_libs

])

dnl
dnl SNERT_ANSI_STRING
dnl
AC_DEFUN(SNERT_ANSI_STRING,[
	echo
	echo "Check for ANSI string functions..."
	echo
	AC_CHECK_FUNCS(memchr memcmp memcpy memmove memset)
	AC_CHECK_FUNCS(strcat strncat strcpy strncpy strcmp strncmp strxfrm)
	AC_CHECK_FUNCS(strchr strcspn strerror strlen strpbrk strrchr strspn strstr strtok)
	AC_FUNC_STRCOLL
	AC_FUNC_STRERROR_R
])

dnl
dnl SNERT_ANSI_TIME
dnl
AC_DEFUN(SNERT_ANSI_TIME,[
	AS_ECHO()
	AS_ECHO("Check for ANSI & supplemental time support...")
	AS_ECHO()

dnl	saved_libs=$LIBS

	case "${platform}" in
	Linux|SunOS|Solaris)
		SNERT_CHECK_LIB(rt, clock_gettime)
		if test "$ac_cv_lib_rt_clock_gettime" != 'no'; then
			AC_SUBST(HAVE_LIB_RT, '-lrt')
		fi
		;;
	esac

	AC_CHECK_HEADERS(time.h sys/time.h)
	AC_CHECK_FUNCS(clock difftime mktime time asctime ctime gmtime localtime tzset sleep usleep nanosleep)
	AC_CHECK_FUNCS(asctime_r ctime_r gmtime_r localtime_r clock_gettime gettimeofday)
	AC_CHECK_FUNCS(alarm getitimer setitimer)
	dnl These are typically macros:  timerclear timerisset timercmp timersub timeradd
	AC_FUNC_MKTIME
	AC_LIBOBJ(mktime)
	AC_FUNC_STRFTIME
	AC_CHECK_TYPES([struct timespec, struct timeval],[],[],[
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <time.h>
	])

	AC_STRUCT_TM
	AC_CHECK_MEMBERS([struct tm.tm_gmtoff],[],[],[
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#include <time.h>
	])
	AC_STRUCT_TIMEZONE
	AC_CHECK_FUNCS(timegm)

dnl	LIBS=$saved_libs
])

dnl
dnl SNERT_EXTRA_STDIO
dnl
AC_DEFUN(SNERT_EXTRA_STDIO,[
	echo
	echo "Check for supplemental stdio support..."
	echo
	SNERT_CHECK_PREDEFINE(__CYGWIN__)
	AC_CHECK_HEADERS([io.h err.h])
	AC_CHECK_FUNCS(getdelim getline getprogname setprogname err errx warn warnx verr verrx vwarn vwarnx)
])

dnl
dnl SNERT_EXTRA_STRING
dnl
AC_DEFUN(SNERT_EXTRA_STRING,[
	echo
	echo "Check for supplemental string support..."
	echo
	AC_CHECK_FUNCS(strdup strtol strlcpy strlcat strcasecmp strncasecmp)
	AC_CHECK_FUNCS(snprintf vsnprintf setproctitle)
	AC_FUNC_VPRINTF
])

AC_DEFUN([SNERT_REGEX],[
	AS_ECHO()
	AS_ECHO("Check for regex...")
	AS_ECHO()
	AC_CHECK_HEADERS([regex.h],[
		AC_SEARCH_LIBS([regcomp], [regex])
		dnl Redo function tests; see SNERT_PCRE.
		AS_UNSET(ac_cv_func_regcomp)
		AS_UNSET(ac_cv_func_regexec)
		AS_UNSET(ac_cv_func_regerror)
		AS_UNSET(ac_cv_func_regfree)
		AC_CHECK_FUNCS(regcomp regexec regerror regfree)
	])
])

AC_DEFUN(SNERT_PCRE,[
	dnl Redo function tests; see SNERT_REGEX.
	AS_UNSET(ac_cv_func_regcomp)
	AS_UNSET(ac_cv_func_regexec)
	AS_UNSET(ac_cv_func_regerror)
	AS_UNSET(ac_cv_func_regfree)
	SNERT_CHECK_PACKAGE([PCRE],
		[pcre.h pcreposix.h],[libpcre libpcreposix],
		[pcre_compile pcre_exec pcre_free regcomp regexec regerror regfree],
		[$with_pcre],[$with_pcre_inc],[$with_pcre_lib] )
dnl 	AC_SUBST(LIBS_PCRE)
dnl 	AC_SUBST(CPPFLAGS_PCRE)
dnl 	AC_SUBST(LDFLAGS_PCRE)
	AH_VERBATIM(LIBS_PCRE,[
#undef HAVE_PCRE_H
#undef HAVE_PCREPOSIX_H
#undef HAVE_LIBPCRE
#undef HAVE_LIBPCREPOSIX
#undef HAVE_PCRE_COMPILE
#undef HAVE_PCRE_EXEC
#undef HAVE_PCRE_FREE
#undef HAVE_REGCOMP
#undef HAVE_REGEXEC
#undef HAVE_REGERROR
#undef HAVE_REGFREE
#undef CPPFLAGS_PCRE
#undef LDFLAGS_PCRE
#undef LIBS_PCRE
	])
])

dnl
dnl SNERT_HASHES
dnl
AC_DEFUN(SNERT_HASHES,[
	echo
	echo "Check for common hashes..."
	echo
	AC_CHECK_HEADERS([md4.h md5.h rmd160.h sha1.h sha2.h],[
		AC_SEARCH_LIBS([SHA256Init],[md])
		AS_IF([expr "$ac_cv_search_SHA256Init" : '-l' >/dev/null],[
			LIBS_MD="$ac_cv_search_SHA256Init"
			AC_DEFINE_UNQUOTED(LIBS_MD,"$LIBS_MD",[Message Digest Library])
			AC_SUBST(LIBS_MD)
		])
	],[],[/* */])
])

dnl
dnl SNERT_TERMIOS
dnl
AC_DEFUN(SNERT_TERMIOS,[
	echo
	echo "Check for termios..."
	echo
	AC_CHECK_HEADERS([termios.h],[
		AC_CHECK_FUNCS(tcgetattr tcsetattr tcgetwinsize tcsetwinsize ctermid)
	])
	AC_CHECK_TYPES([struct winsize],[],[],[
#include <termios.h>
	])
])

AC_DEFUN(SNERT_OPTION_WITH_PTHREAD,[
	AC_ARG_WITH(pthread,[AS_HELP_STRING([--with-pthread],[POSIX threads optional base directory])])
])
AC_DEFUN(SNERT_PTHREAD,[
AS_IF([test "$enable_win32" = 'yes'],[
	AS_ECHO
	AS_ECHO("Checking for PTHERAD...")
	AS_ECHO
	AS_ECHO("POSIX thread support... limited Windows native")
	AC_CACHE_VAL(ac_cv_func_pthread_create,[ac_cv_func_pthread_create='limited'])
],[
	AS_CASE([$platform],
	[FreeBSD|OpenBSD|NetBSD],[
		dnl OpenBSD requires -pthread in order to link sigwait.
		SNERT_JOIN_UNIQ([CFLAGS_PTHREAD],[-pthread])
		SNERT_JOIN_UNIQ([CPPFLAGS_PTHREAD],[-pthread])
		SNERT_JOIN_UNIQ([LDFLAGS_PTHREAD],[-pthread])
	])

	SNERT_CHECK_PACKAGE([PTHREAD],
		[pthread.h],[libpthread],[ \
		pthread_create pthread_cancel pthread_equal pthread_exit pthread_join \
		pthread_kill pthread_self pthread_detach pthread_yield pthread_sigmask sigwait \
		pthread_attr_init pthread_attr_destroy pthread_attr_getdetachstate \
		pthread_attr_setdetachstate pthread_attr_getstackaddr pthread_attr_setstackaddr \
		pthread_attr_getstacksize pthread_attr_setstacksize pthread_attr_getscope \
		pthread_attr_setscope pthread_mutex_init pthread_mutex_destroy pthread_mutex_lock \
		pthread_mutex_trylock pthread_mutex_unlock pthread_mutexattr_init \
		pthread_mutexattr_destroy pthread_mutexattr_setprioceiling \
		pthread_mutexattr_getprioceiling pthread_mutexattr_setprotocol \
		pthread_mutexattr_getprotocol pthread_mutexattr_settype pthread_mutexattr_gettype \
		pthread_cond_broadcast pthread_cond_destroy pthread_cond_init pthread_cond_signal \
		pthread_cond_timedwait pthread_cond_wait pthread_spin_init pthread_spin_destroy \
		pthread_spin_lock pthread_spin_trylock pthread_spin_unlock pthread_rwlock_init \
		pthread_rwlock_destroy pthread_rwlock_unlock pthread_rwlock_rdlock \
		pthread_rwlock_wrlock pthread_rwlock_tryrdlock pthread_rwlock_trywrlock \
		pthread_lock_global_np pthread_unlock_global_np pthread_key_create \
		pthread_key_delete pthread_getspecific pthread_setspecific pthread_once \
		pthread_atfork
		],
		[$with_pthread],[$with_pthread_inc],[$with_pthread_lib],[],[no] )

	saved_LIBS="$LIBS"
	saved_LDFLAGS="$LDFLAGS"
	saved_CPPFLAGS="$CPPFLAGS"
	saved_CFLAGS="$CFLAGS"

	LIBS="$LIBS_PTHREAD $saved_LIBS"
	LDFLAGS="$LDFLAGS_PTHREAD $saved_LDFLAGS"
	CPPFLAGS="$CPPFLAGS_PTHREAD $saved_CPPFLAGS"
	CFLAGS="$CFLAGS_PTHREAD $saved_CFLAGS"

	AC_CHECK_TYPES([pthread_t, pthread_attr_t, pthread_mutex_t, pthread_mutexattr_t, pthread_once_t, sigset_t],[],[],[
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif
#ifdef HAVE_PTHREAD_H
# include <pthread.h>
#endif
	])

	AH_TEMPLATE(HAVE_PTHREAD_CLEANUP_PUSH)
	AC_CHECK_FUNC([pthread_cleanup_push],[
		AC_DEFINE_UNQUOTED(HAVE_PTHREAD_CLEANUP_PUSH)
	],[
		SNERT_CHECK_DEFINE(pthread_cleanup_push, pthread.h)
		AS_IF([test $ac_cv_define_pthread_cleanup_push = 'yes'],[
			AC_DEFINE_UNQUOTED(HAVE_PTHREAD_CLEANUP_PUSH)
		])
	])
	AH_TEMPLATE(HAVE_PTHREAD_CLEANUP_POP)
	AC_CHECK_FUNC([pthread_cleanup_pop],[
		AC_DEFINE_UNQUOTED(HAVE_PTHREAD_CLEANUP_POP)
	],[
		SNERT_CHECK_DEFINE(pthread_cleanup_pop, pthread.h)
		AS_IF([test $ac_cv_define_pthread_cleanup_pop = 'yes'],[
			AC_DEFINE_UNQUOTED(HAVE_PTHREAD_CLEANUP_POP)
		])
	])

	SNERT_FIND_LIB([pthread],[
		AC_DEFINE_UNQUOTED(LIBPTHREAD_PATH, ["$snert_find_lib_pthread"])
		AH_TEMPLATE(LIBPTHREAD_PATH)
	])

	LIBS="$saved_LIBS"
	LDFLAGS="$saved_LDFLAGS"
	CPPFLAGS="$saved_CPPFLAGS"
	CFLAGS="$saved_CFLAGS"

	dnl The BSDs use -pthread instead of -lpthread.
	AS_CASE([$platform],
	[FreeBSD|OpenBSD|NetBSD],[
		AS_UNSET([LIBS_PTHREAD])
	])

	SNERT_DEFINE([LIBS_PTHREAD])
	SNERT_DEFINE([LDFLAGS_PTHREAD])
	SNERT_DEFINE([CPPFLAGS_PTHREAD])

 	AC_SUBST(LIBS_PTHREAD)
 	AC_SUBST(LDFLAGS_PTHREAD)
 	AC_SUBST(CPPFLAGS_PTHREAD)

	dnl Deprecated macros.
	SNERT_DEFINE([CFLAGS_PTHREAD],[$CPPFLAGS_PTHREAD])
	SNERT_DEFINE([HAVE_LIB_PTHREAD],[$LIBS_PTHREAD])
 	AC_SUBST(CFLAGS_PTHREAD,[$CPPFLAGS_PTHREAD])
 	AC_SUBST(HAVE_LIB_PTHREAD,[$LIBS_PTHREAD])

	AH_VERBATIM(LIBS_PTHREAD,[
/*
 * POSIX Thread & Mutex Functions
 */
#undef HAVE_PTHREAD_H
#undef HAVE_PTHREAD_T
#undef HAVE_PTHREAD_ATTR_T
#undef HAVE_PTHREAD_MUTEX_T
#undef HAVE_PTHREAD_MUTEXATTR_T
#undef HAVE_PTHREAD_ONCE_T
#undef HAVE_PTHREAD_CANCEL
#undef HAVE_PTHREAD_CREATE
#undef HAVE_PTHREAD_DETACH
#undef HAVE_PTHREAD_EQUAL
#undef HAVE_PTHREAD_EXIT
#undef HAVE_PTHREAD_JOIN
#undef HAVE_PTHREAD_KILL
#undef HAVE_PTHREAD_SELF
#undef HAVE_PTHREAD_YIELD
#undef HAVE_PTHREAD_SIGMASK
#undef HAVE_SIGWAIT
#undef HAVE_PTHREAD_ATFORK
#undef HAVE_PTHREAD_ATTR_INIT
#undef HAVE_PTHREAD_ATTR_DESTROY
#undef HAVE_PTHREAD_ATTR_GETDETACHSTATE
#undef HAVE_PTHREAD_ATTR_SETDETACHSTATE
#undef HAVE_PTHREAD_ATTR_GETSTACKADDR
#undef HAVE_PTHREAD_ATTR_SETSTACKADDR
#undef HAVE_PTHREAD_ATTR_GETSTACKSIZE
#undef HAVE_PTHREAD_ATTR_SETSTACKSIZE
#undef HAVE_PTHREAD_ATTR_GETSCOPE
#undef HAVE_PTHREAD_ATTR_SETSCOPE
#undef HAVE_PTHREAD_MUTEX_INIT
#undef HAVE_PTHREAD_MUTEX_DESTROY
#undef HAVE_PTHREAD_MUTEX_LOCK
#undef HAVE_PTHREAD_MUTEX_TRYLOCK
#undef HAVE_PTHREAD_MUTEX_UNLOCK
#undef HAVE_PTHREAD_MUTEXATTR_INIT
#undef HAVE_PTHREAD_MUTEXATTR_DESTROY
#undef HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING
#undef HAVE_PTHREAD_MUTEXATTR_GETPRIOCEILING
#undef HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL
#undef HAVE_PTHREAD_MUTEXATTR_GETPROTOCOL
#undef HAVE_PTHREAD_MUTEXATTR_SETTYPE
#undef HAVE_PTHREAD_MUTEXATTR_GETTYPE
#undef HAVE_PTHREAD_RWLOCK_INIT
#undef HAVE_PTHREAD_RWLOCK_DESTROY
#undef HAVE_PTHREAD_RWLOCK_UNLOCK
#undef HAVE_PTHREAD_RWLOCK_RDLOCK
#undef HAVE_PTHREAD_RWLOCK_WRLOCK
#undef HAVE_PTHREAD_RWLOCK_TRYRDLOCK
#undef HAVE_PTHREAD_RWLOCK_TRYWRLOCK
#undef HAVE_PTHREAD_SPIN_INIT
#undef HAVE_PTHREAD_SPIN_DESTROY
#undef HAVE_PTHREAD_SPIN_LOCK
#undef HAVE_PTHREAD_SPIN_TRYLOCK
#undef HAVE_PTHREAD_SPIN_UNLOCK
#undef HAVE_PTHREAD_LOCK_GLOBAL_NP
#undef HAVE_PTHREAD_UNLOCK_GLOBAL_NP
#undef HAVE_PTHREAD_COND_BROADCAST
#undef HAVE_PTHREAD_COND_DESTROY
#undef HAVE_PTHREAD_COND_INIT
#undef HAVE_PTHREAD_COND_SIGNAL
#undef HAVE_PTHREAD_COND_TIMEDWAIT
#undef HAVE_PTHREAD_COND_WAIT
#undef HAVE_PTHREAD_KEY_CREATE
#undef HAVE_PTHREAD_KEY_DELETE
#undef HAVE_PTHREAD_GETSPECIFIC
#undef HAVE_PTHREAD_SETSPECIFIC
#undef HAVE_PTHREAD_CLEANUP_PUSH
#undef HAVE_PTHREAD_CLEANUP_POP
#undef HAVE_PTHREAD_ONCE
#undef LIBS_PTHREAD
#undef LDFLAGS_PTHREAD
#undef CPPFLAGS_PTHREAD
#undef CFLAGS_PTHREAD
#undef LIBPTHREAD_PATH
	])
])
])

AC_DEFUN(SNERT_SCHED,[
	SNERT_CHECK_PACKAGE([SCHED],
		[sched.h],[librt],[ \
		sched_getparam sched_get_priority_max sched_get_priority_min \
		sched_rr_get_interval sched_getscheduler sched_setparam \
		sched_setscheduler sched_yield
		],[$with_sched],[$with_sched_inc],[$with_sched_lib] )
	AH_VERBATIM(LIBS_SCHED,[
#undef HAVE_SCHED_H
#undef HAVE_SCHED_GETPARAM
#undef HAVE_SCHED_GET_PRIORITY_MAX
#undef HAVE_SCHED_GET_PRIORITY_MIN
#undef HAVE_SCHED_RR_GET_INTERVAL
#undef HAVE_SCHED_GETSCHEDULER
#undef HAVE_SCHED_SETPARAM
#undef HAVE_SCHED_SETSCHEDULER
#undef HAVE_SCHED_YIELD
#undef CPPFLAGS_SCHED
#undef LDFLAGS_SCHED
#undef LIBS_SCHED
	])
])

AC_DEFUN(SNERT_OPTION_WITH_LIBEV,[
	AC_ARG_WITH(libev,[AS_HELP_STRING([--with-libev],[use libev in place of Snert Event API, optional base directory])])
dnl	AC_ARG_WITH(libev-inc,[AS_HELP_STRING([--with-libev-inc],[specific libev include directory])])
dnl	AC_ARG_WITH(libev-lib,[AS_HELP_STRING([--with-libev-lib],[specific libev library directory])])
])
AC_DEFUN(SNERT_LIBEV,[
	AS_CASE([$platform],
	[NetBSD],[
		with_libev_inc='/usr/pkg/include/ev'
		with_libev_lib='/usr/pkg/lib/ev'
	])
	SNERT_CHECK_PACKAGE([LIBEV],
		[ev.h ev/ev.h],[libev],[ev_run],
		[$with_libev],[$with_libev_inc],[$with_libev_lib] )
	dnl Not default and not explicitly disabled, ie. explicitly set.
	AS_IF([test X"$with_libev" != X -a "$with_libev" != 'no'],[
		AC_DEFINE_UNQUOTED(USE_LIBEV)
	])
	AH_VERBATIM(LIBS_LIBEV,[
#undef USE_LIBEV
#undef HAVE_EV_H
#undef HAVE_EV_RUN
#undef CPPFLAGS_LIBEV
#undef LDFLAGS_LIBEV
#undef LIBS_LIBEV
	])
])

AC_DEFUN(SNERT_OPTION_WITH_LUA,[
	AC_ARG_WITH(lua, [[  --with-lua[=DIR]     Lua package, optional base directory]])
dnl	AC_ARG_WITH(lua-inc, [[  --with-lua-inc=DIR     specific Lua include directory]])
dnl	AC_ARG_WITH(lua-lib, [[  --with-lua-lib=DIR     specific Lua library directory]])
])
AC_DEFUN(SNERT_LUA,[
	LIBS_LUA="-lm"
	SNERT_CHECK_PACKAGE([LUA],
		[lua.h],[liblua],[luaL_newstate],
		[$with_lua],[$with_lua_inc],[$with_lua_lib] )
	AH_VERBATIM(LIBS_LUA,[
#undef HAVE_LUA_H
#undef HAVE_LUAL_NEWSTATE
#undef CPPFLAGS_LUA
#undef LDFLAGS_LUA
#undef LIBS_LUA
	])
])

AC_DEFUN(SNERT_OPTION_WITH_MILTER,[
	AC_ARG_WITH(milter, [[  --with-milter[=DIR]     Milter package, optional base directory]])
dnl	AC_ARG_WITH(milter-inc, [[  --with-milter-inc=DIR     specific Milter include directory]])
dnl	AC_ARG_WITH(milter-lib, [[  --with-milter-lib=DIR     specific Milter library directory]])
])
AC_DEFUN(SNERT_LIBMILTER,[
	AC_REQUIRE([SNERT_PTHREAD])
	AS_IF([test "$with_milter" != 'no'],[
		SNERT_JOIN_UNIQ([LIBS_MILTER],[$LIBS_PTHREAD])
		SNERT_JOIN_UNIQ([LDFLAGS_MILTER],[$LDFLAGS_PTHREAD])
		SNERT_JOIN_UNIQ([CPPFLAGS_MILTER],[$CPPFLAGS_PTHREAD])
		SNERT_JOIN_UNIQ([CFLAGS_MILTER],[$CFLAGS_PTHREAD])
	])
	SNERT_CHECK_PACKAGE([MILTER],
		[libmilter/mfapi.h],[libmilter],[ \
		smfi_addheader smfi_addrcpt smfi_addrcpt_par smfi_chgfrom \
		smfi_chgheader smfi_delrcpt smfi_getpriv smfi_getsymval \
		smfi_insheader smfi_main smfi_opensocket smfi_progress \
		smfi_quarantine smfi_register smfi_replacebody smfi_setbacklog \
		smfi_setconn smfi_setdbg smfi_setmaxdatasize smfi_setmlreply \
		smfi_setpriv smfi_setreply smfi_setsymlist smfi_settimeout \
		smfi_stop smfi_version
		],
		[$with_milter],[$with_milter_inc],[$with_milter_lib] )

	AH_VERBATIM(LIBS_MILTER,[
#undef HAVE_LIBMILTER_MFAPI_H
#undef CPPFLAGS_MILTER
#undef LDFLAGS_MILTER
#undef HAVE_LIBMILTER
#undef LIBS_MILTER
	])
])

AC_DEFUN([SNERT_OPTION_WITH_OPENSSL],[
	AC_ARG_WITH([openssl],[AS_HELP_STRING([--with-openssl=DIR],[OpenSSL package, optional base directory])])
dnl	AC_ARG_WITH([openssl-inc],[AS_HELP_STRING([--with-openssl-inc=DIR],[specific OpenSSL include directory])])
dnl	AC_ARG_WITH([openssl-lib],[AS_HELP_STRING([--with-openssl-lib=DIR],[specific OpenSSL library directory])])
])
AC_DEFUN([SNERT_OPENSSL],[
	AC_REQUIRE([SNERT_NETWORK])
	SNERT_CHECK_PACKAGE([SSL],
		[openssl/ssl.h openssl/bio.h openssl/err.h openssl/crypto.h],
		[libssl libcrypto],[SSL_library_init EVP_cleanup],
		[$with_openssl],[$with_openssl_inc],[$with_openssl_lib] )
	SNERT_CHECK_DEFINE(OpenSSL_add_all_algorithms, openssl/evp.h)
	SNERT_FIND_DIR([certs],[/etc/ssl /etc/openssl], [
		SNERT_DEFINE([ETC_SSL],[$dir_val])
		AC_SUBST(ETC_SSL,[$dir_val])
	],[])
dnl 	AC_SUBST(LIBS_SSL)
dnl 	AC_SUBST(CPPFLAGS_SSL)
dnl 	AC_SUBST(LDFLAGS_SSL)
	AH_VERBATIM(LIBS_SSL,[
#undef HAVE_LIBSSL
#undef HAVE_LIBCRYPTO
#undef HAVE_OPENSSL_SSL_H
#undef HAVE_OPENSSL_BIO_H
#undef HAVE_OPENSSL_ERR_H
#undef HAVE_OPENSSL_CRYPTO_H
#undef HAVE_EVP_CLEANUP
#undef HAVE_SSL_LIBRARY_INIT
#undef HAVE_MACRO_OPENSSL_ADD_ALL_ALGORITHMS
#undef CPPFLAGS_SSL
#undef LDFLAGS_SSL
#undef LIBS_SSL
#undef ETC_SSL
	])
])

AC_DEFUN([SNERT_OPTION_WITH_SASL2],[
	AC_ARG_WITH([sasl2],[AS_HELP_STRING([--with-sasl2=DIR],[SASL2 package, optional base directory])])
dnl	AC_ARG_WITH([sasl2-inc],[AS_HELP_STRING([--with-sasl2-inc=DIR],[specific SASL2 include directory])])
dnl	AC_ARG_WITH([sasl2-lib],[AS_HELP_STRING([--with-sasl2-lib=DIR],[specific SASL2 library directory])])
])
AC_DEFUN([SNERT_SASL2],[
	AC_REQUIRE([SNERT_NETWORK])
	SNERT_CHECK_PACKAGE([SASL2],
		[sasl/sasl.h sasl/saslutil.h],[libsasl2],[prop_get sasl_checkapop],
		[$with_sasl2],[$with_sasl2_inc],[$with_sasl2_lib] )
dnl 	AC_SUBST(LIBS_SASL2)
dnl 	AC_SUBST(CPPFLAGS_SASL2)
dnl 	AC_SUBST(LDFLAGS_SASL2)
	AH_VERBATIM(LIBS_SASL2,[
#undef HAVE_LIBSASL2
#undef HAVE_SASL_SASL_H
#undef HAVE_SASL_SASLUTIL_H
#undef HAVE_SASL_CHECKAPOP
#undef HAVE_PROP_GET
#undef CPPFLAGS_SASL2
#undef LDFLAGS_SASL2
#undef LIBS_SASL2
	])
])

AC_DEFUN(SNERT_OPTION_WITH_SQLITE3,[
	AC_ARG_WITH([sqlite3],[AS_HELP_STRING([--with-sqlite3=DIR],[SQLite3 package, optional base directory])])
dnl	AC_ARG_WITH([sqlite3-inc],[AS_HELP_STRING([--with-sqlite3-inc=DIR],[specific SQLite3 include directory])])
dnl	AC_ARG_WITH([sqlite3-lib],[AS_HELP_STRING([--with-sqlite3-lib=DIR],[specific SQLite3 library directory])])
])
AC_DEFUN(SNERT_SQLITE3,[
	AC_REQUIRE([SNERT_PTHREAD])
	AS_IF([test "$with_sqlite3" != 'no'],[
		SNERT_JOIN_UNIQ([LIBS_SQLITE3],[$LIBS_PTHREAD])
		SNERT_JOIN_UNIQ([LDFLAGS_SQLITE3],[$LDFLAGS_PTHREAD])
		SNERT_JOIN_UNIQ([CPPFLAGS_SQLITE3],[$CPPFLAGS_PTHREAD])
		SNERT_JOIN_UNIQ([CFLAGS_SQLITE3],[$CFLAGS_PTHREAD])
	])
	SNERT_CHECK_PACKAGE([SQLITE3],[sqlite3.h],[libsqlite3],[sqlite3_open],
		[$with_sqlite3],[$with_sqlite3_inc],[$with_sqlite3_lib] )

	save_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS_SQLITE3 $CPPFLAGS"
	SNERT_GET_NUMERIC_DEFINE([sqlite3.h],[SQLITE_VERSION_NUMBER])
	CPPFLAGS="$save_CPPFLAGS"

	AS_IF([test $snert_cv_define_sqlite3_h_SQLITE_VERSION_NUMBER -ge 3025000],[
		SNERT_JOIN_UNIQ([LIBS_SQLITE3],[-lm])
	])
dnl 	AC_SUBST(LIBS_SQLITE3)
dnl 	AC_SUBST(CPPFLAGS_SQLITE3)
dnl 	AC_SUBST(LDFLAGS_SQLITE3)
	AH_VERBATIM(LIBS_SQLITE3,[
#undef HAVE_SQLITE3_H
#undef HAVE_SQLITE3_OPEN
#undef CPPFLAGS_SQLITE3
#undef LDFLAGS_SQLITE3
#undef LIBS_SQLITE3
	])
])

AC_DEFUN(SNERT_OPTION_WITH_MYSQL,[
	AC_ARG_WITH([mysql],[AS_HELP_STRING([--with-mysql=DIR],[MySQL package, optional base directory])])
dnl	AC_ARG_WITH([mysql-inc],[AS_HELP_STRING([--with-mysql-inc=DIR],[specific MySQL include directory])])
dnl	AC_ARG_WITH([mysql-lib],[AS_HELP_STRING([--with-mysql-lib=DIR],[specific MySQL library directory])])
])
AC_DEFUN(SNERT_MYSQL,[
	SNERT_CHECK_PACKAGE([MYSQL],[mysql.h mysql/mysql.h],[libmysqlclient mysql/libmysqlclient ],[mysql_select_db],
		[$with_mysql],[$with_mysql_inc],[$with_mysql_lib],[],[no] )

	AC_PATH_PROG([MYSQL_CONFIG],[mysql_config],[false])
	AS_IF([test "$MYSQL_CONFIG" = 'false'],[
		with_mysql='no'
		AS_UNSET([LIBS_MYSQL])
		AS_UNSET([LDFLAGS_MYSQL])
		AS_UNSET([CPPFLAGS_MYSQL])
		AC_MSG_WARN([mysql_config not found, disabling MySQL support.])
	],[
		dnl Override found flags with those supplied by tool.
		CPPFLAGS_MYSQL=`$MYSQL_CONFIG --include`
		LIBS_MYSQL=`$MYSQL_CONFIG --libs`
	])

	SNERT_DEFINE([LIBS_MYSQL])
	SNERT_DEFINE([LDFLAGS_MYSQL])
	SNERT_DEFINE([CPPFLAGS_MYSQL])

 	AC_SUBST(LIBS_MYSQL)
 	AC_SUBST(LDFLAGS_MYSQL)
 	AC_SUBST(CPPFLAGS_MYSQL)

	AH_VERBATIM(LIBS_MYSQL,[
#undef HAVE_MYSQL_LIBMYSQLCLIENT
#undef HAVE_LIBMYSQLCLIENT
#undef HAVE_MYSQL_MYSQL_H
#undef HAVE_MYSQL_H
#undef HAVE_MYSQL_SELECT_DB
#undef CPPFLAGS_MYSQL
#undef LDFLAGS_MYSQL
#undef LIBS_MYSQL
	])
])

AC_DEFUN(SNERT_OPTION_WITH_PGSQL,[
	AC_ARG_WITH([pgsql],[AS_HELP_STRING([--with-pgsql=DIR],[PostgreSQL package, optional base directory])])
dnl	AC_ARG_WITH([pgsql-inc],[AS_HELP_STRING([--with-pgsql-inc=DIR],[specific PostgreSQL include directory])])
dnl	AC_ARG_WITH([pgsql-lib],[AS_HELP_STRING([--with-pgsql-lib=DIR],[specific PostgreSQL library directory])])
])
AC_DEFUN(SNERT_PGSQL,[
	SNERT_CHECK_PACKAGE([PGSQL],[libpq-fe.h],[libpq],[PQconnectdb],
		[$with_pgsql],[$with_pgsql_inc],[$with_pgsql_lib] )
dnl 	AC_SUBST(LIBS_PGSQL)
dnl 	AC_SUBST(CPPFLAGS_PGSQL)
dnl 	AC_SUBST(LDFLAGS_PGSQL)
	AH_VERBATIM(LIBS_PGSQL,[
#undef HAVE_LIBPQ
#undef HAVE_LIBPQ_FE_H
#undef HAVE_PQCONNECTDB
#undef CPPFLAGS_PGSQL
#undef LDFLAGS_PGSQL
#undef LIBS_PGSQL
	])
])

dnl
dnl SNERT_BUILD_THREADED_SQLITE3
dnl
AC_DEFUN(SNERT_BUILD_THREADED_SQLITE3,[
AS_IF([test ${with_sqlite3:-default} = 'default'],[
	echo
	AC_MSG_CHECKING([for bundled SQLite3])

	AC_SUBST(LIBSNERT_SQLITE3_DIR, ${srcdir}/../../../../org/sqlite)
	AS_IF([ test `ls -t1 ${LIBSNERT_SQLITE3_DIR}/sqlite*.gz | wc -l` -gt 0 ],[
		AC_MSG_RESULT([yes])

		libsnertdir=`pwd`
		cd ${LIBSNERT_SQLITE3_DIR}
		with_sqlite3=`pwd`

		dnl Assume the most recent .tar.gz is the most current version.
		tarfile=`ls -t1 sqlite*tar.gz | head -n 1`
		dir=`basename $tarfile .tar.gz`

		is_amalgamation=false
		AS_IF([expr ${dir} : 'sqlite-autoconf-.*' >/dev/null],[is_amalgamation=true])
		AS_IF([! $is_amalgamation -a expr ${dir} : 'sqlite-amalgamation-.*'],[is_amalgamation=true; i=`echo ${dir} | sed -e 's/amalgamation-//'`; dir=${i}])

		AC_SUBST(LIBSNERT_SQLITE3_VERSION, ${dir})

		echo "sqlite directory... $with_sqlite3"
		echo "bundled version..." ${LIBSNERT_SQLITE3_VERSION}
		AC_MSG_CHECKING([for previously built threaded SQLite3])
		if test -f "$with_sqlite3/include/sqlite3.h"; then
			AC_MSG_RESULT([yes])
		else
			AC_MSG_RESULT([no])

			AC_MSG_CHECKING([for tar file])
			AC_MSG_RESULT($tarfile)

			AC_MSG_CHECKING([if unpacked])
			AS_IF([ test -d $dir ],[
				AC_MSG_RESULT([yes])
			],[
				tar -zxf $tarfile
				AC_MSG_RESULT([unpacked])
				make patch
			])

			cd $dir
			AC_MSG_CHECKING([sqlite3 build directory])
			pwd
			if test ! -f config.status ; then
				echo
				echo 'Configuring threaded SQLite3...'

				sqlite3_cflags="-DSQLITE_ENABLE_UNLOCK_NOTIFY ${CFLAGS}"
				sqlite3_configure_options="--prefix=$with_sqlite3 --enable-threadsafe"

				AS_IF([$is_amalgamation],
					[sqlite3_configure_options="${sqlite3_configure_options} --disable-dynamic-extensions"],
					[sqlite3_configure_options="${sqlite3_configure_options} --disable-amalgamation --disable-tcl --without-tcl"
					 AS_IF([test ${enable_debug:-no} = 'yes'],[sqlite3_configure_options="${sqlite3_configure_options} --enable-debug"])
					]
				)
				AS_IF([test ${platform} != 'Darwin'],[sqlite3_configure_options="${sqlite3_configure_options} --enable-static --disable-shared"])

				echo ./configure CFLAGS="'${sqlite3_cflags}'" ${sqlite3_configure_options}
				./configure CFLAGS="${sqlite3_cflags}" ${sqlite3_configure_options}
				echo
			fi
			echo
			echo 'Installing threaded SQLite3...'
			echo
			make clean install
			cd -
		fi

		SQLITE3_I_DIR="$with_sqlite3/include"
		SQLITE3_L_DIR="$with_sqlite3/lib"
		cd $libsnertdir
	],[
		AC_MSG_RESULT([no])
	])
	echo
])
])

dnl
dnl SNERT_NETWORK
dnl
AC_DEFUN(SNERT_NETWORK,[
	echo
	echo "Check for Network services..."
	echo
	SNERT_CHECK_PREDEFINE(__WIN32__)
	SNERT_CHECK_PREDEFINE(__CYGWIN__)

	AS_IF([test "$ac_cv_define___WIN32__" = 'no'],[
		AS_CASE([$platform],
		[SunOS],[
			SNERT_JOIN_UNIQ([NETWORK_LIBS],["-lresolv -lsocket -lnsl"],[tail])
			AC_SUBST(NETWORK_LIBS, ${NETWORK_LIBS})
		],[
			AC_SEARCH_LIBS([socket], [socket nsl], [
				AS_IF([test "${ac_cv_search_socket}" = 'none required'],[ac_cv_search_socket=''])
				SNERT_JOIN_UNIQ([NETWORK_LIBS],["${ac_cv_search_socket}"],[tail])
				AC_SUBST(NETWORK_LIBS, ${NETWORK_LIBS})
			])
			AC_SEARCH_LIBS([inet_aton], [resolv socket nsl], [
				AS_IF([test "${ac_cv_search_inet_aton}" = 'none required'],[ac_cv_search_inet_aton=''])
				SNERT_JOIN_UNIQ([NETWORK_LIBS],["${ac_cv_search_inet_aton}"],[tail])
				AC_SUBST(NETWORK_LIBS, ${NETWORK_LIBS})
			])
		])

		AC_CHECK_HEADERS([ \
			sys/socket.h netinet/in.h netinet/in6.h netinet6/in6.h \
			netinet/tcp.h poll.h sys/poll.h sys/select.h sys/un.h \
			arpa/inet.h \
		])

dnl When using poll() use this block.
dnl
dnl #ifdef HAVE_POLL_H
dnl # include <poll.h>
dnl # ifndef INFTIM
dnl #  define INFTIM	(-1)
dnl # endif
dnl #endif

dnl When using kqueue() use this block.
dnl
dnl #ifdef HAVE_SYS_EVENT_H
dnl # include <sys/types.h>
dnl # include <sys/event.h>
dnl # include <sys/time.h>
dnl # ifndef INFTIM
dnl #  define INFTIM	(-1)
dnl # endif
dnl #endif

		AC_CHECK_FUNCS([ \
			inet_pton inet_aton inet_addr inet_ntoa inet_ntop \
			accept bind connect listen poll select shutdown socket \
			getpeereid getpeername getsockname getsockopt setsockopt \
			recv recvfrom recvmsg send sendmsg sendto \
			htonl htons ntohl ntohs \
		])

		AC_CHECK_HEADERS([sys/event.h],[AC_CHECK_FUNCS([kqueue kevent])])
		AC_CHECK_HEADERS([sys/epoll.h],[AC_CHECK_FUNCS([epoll_create epoll_ctl epoll_wait epoll_pwait])])

		AC_CHECK_HEADERS([netdb.h],[
			AC_CHECK_FUNCS([ \
				getaddrinfo freeaddrinfo getnameinfo \
				gethostname gethostbyname gethostbyname2 gethostbyaddr \
				gethostbyname_r gethostbyname2_r gethostbyaddr_r \
				gethostent sethostent endhostent hstrerror herror \
				getservent getservbyport getservbyname setservent endservent \
				getprotoent getprotobynumber getprotobyname setprotoent endprotoent \
			])
		])

		AC_CHECK_HEADERS([ifaddrs.h],[
			AC_CHECK_FUNCS([ \
				getifaddrs freeifaddrs
			])
		])
		AC_CHECK_HEADERS([net/if.h],[
			AC_CHECK_FUNCS([ \
				if_nameindex if_freenameindex if_nametoindex if_indextoname
			])
		])
	],[
		AC_CHECK_HEADERS(windows.h)
		AC_CHECK_HEADER(winsock2.h,[
			AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_]winsock2.h),[],[Windows BSD Socket API])
		],[],[
#if defined(__WIN32__)
# if defined(HAVE_WINDOWS_H)
#  include  <windows.h>
# endif
#endif
		])
		AC_CHECK_HEADER(ws2tcpip.h,[
			AC_SUBST(HAVE_LIB_WS2_32, '-lws2_32')
			AC_DEFINE(AS_TR_CPP([HAVE_]ws2tcpip.h),[],[Windows TCP/IP API])
		],[],[
#if defined(__WIN32__)
# if defined(HAVE_WINDOWS_H)
#  include  <windows.h>
# endif
# if defined(HAVE_WINSOCK2_H)
#  include  <winsock2.h>
# endif
#endif
		])
		AC_CHECK_HEADER(Iphlpapi.h,[
			AC_SUBST(HAVE_LIB_IPHLPAPI, '-lIphlpapi')
			AC_DEFINE(AS_TR_CPP([HAVE_]Iphlpapi.h),[],[Windows IP Helper library])
		],[],[
#if defined(__WIN32__)
# if defined(HAVE_WINDOWS_H)
#  include  <windows.h>
# endif
#endif
		])

		for i in \
			accept \
			bind \
			closesocket \
			connect \
			endservent \
			getpeername \
			getprotobyname \
			getprotobynumber \
			getservbyname \
			getservbyport \
			getservent \
			getsockname \
			getsockopt \
			htonl \
			htons \
			inet_addr \
			inet_ntoa \
			listen \
			ntohl \
			ntohs \
			recv \
			recvfrom \
			select \
			send \
			sendto \
			setservent \
			setsockopt \
			shutdown \
			socket \
			getaddrinfo freeaddrinfo getnameinfo \
			gethostname gethostbyname gethostbyaddr
		do
			AC_MSG_CHECKING([for $i])
			AC_DEFINE(AS_TR_CPP([HAVE_]$i),[],[function $1])
			AC_MSG_RESULT([assumed in winsock2.h & ws2tcpip.h])
		done
	])

	AS_IF([test ${ac_cv_define___CYGWIN__:-no} != 'no' -o ${ac_cv_define___WIN32__:-no} != 'no'],[
		NETWORK_LIBS="-lws2_32 -lIphlpapi $NETWORK_LIBS"
		AC_SUBST(NETWORK_LIBS, ${NETWORK_LIBS})
	])

	AC_CHECK_TYPES([struct sockaddr_in6, struct in6_addr, struct sockaddr_un, socklen_t, sockaddr_storage],[],[],[
#if defined(__WIN32__)
# define WINVER	0x0501
# if defined(HAVE_WINDOWS_H)
#  include  <windows.h>
# endif
# if defined(HAVE_WINSOCK2_H)
#  include  <winsock2.h>
# endif
# if defined(HAVE_WS2TCPIP_H)
#  include <ws2tcpip.h>
# endif
#else
# ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
# endif
# ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
# endif
# ifdef HAVE_SYS_UN_H
#  include <sys/un.h>
# endif
# ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
# endif
# ifdef HAVE_NETINET_IN6_H
#  include <netinet/in6.h>
# endif
# ifdef HAVE_NETINET6_IN6_H
#  include <netinet6/in6.h>
# endif
#endif
	])
	AC_CHECK_MEMBERS([struct sockaddr.sa_len, struct sockaddr_in.sin_len, struct sockaddr_in6.sin6_len, struct sockaddr_un.sun_len],[],[],[
#if defined(__WIN32__)
# define WINVER	0x0501
# if defined(HAVE_WINDOWS_H)
#  include  <windows.h>
# endif
# if defined(HAVE_WINSOCK2_H)
#  include  <winsock2.h>
# endif
# if defined(HAVE_WS2TCPIP_H)
#  include <ws2tcpip.h>
# endif
#else
# ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
# endif
# ifdef HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
# endif
# ifdef HAVE_SYS_UN_H
#  include <sys/un.h>
# endif
# ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
# endif
# ifdef HAVE_NETINET_IN6_H
#  include <netinet/in6.h>
# endif
# ifdef HAVE_NETINET6_IN6_H
#  include <netinet6/in6.h>
# endif
#endif
	])

])

dnl
dnl SNERT_SYS
dnl
AC_DEFUN([SNERT_SYS],[
	AS_ECHO()
	AS_ECHO("Check for system kernel support...")
	AS_ECHO()
	dnl Linux
	AC_CHECK_HEADERS([sys/prctl.h],[
		AC_CHECK_FUNCS(prctl)
	])
	AC_CHECK_HEADERS([sys/sysinfo.h],[
		AC_CHECK_FUNCS(get_nprocs_conf get_nprocs)
	])
	dnl *BSD
	AC_CHECK_HEADERS([sys/param.h sys/sysctl.h],[
		AC_CHECK_FUNCS(sysctl)
	])
	AC_CHECK_HEADERS([stdlib.h],[
		AC_CHECK_FUNCS(getloadavg)
	])
	dnl POSIX / generic
	AC_CHECK_HEADERS([unistd.h],[
		AC_CHECK_FUNCS(fpathconf pathconf sysconf)
	])
])

dnl
dnl SNERT_BACKTRACE
dnl
AC_DEFUN(SNERT_BACKTRACE,[
	echo
	echo "Check for GNU backtrace support..."
	echo
	saved_ldflags=$LDFLAGS
	LDFLAGS="-rdynamic ${LDFLAGS}"
	AC_CHECK_FUNCS(backtrace backtrace_symbols backtrace_symbols_fd)
	AS_IF([test $ac_cv_func_backtrace = 'no'],[LDFLAGS="${saved_ldflags}"])
])

dnl
dnl SNERT_INIT($c_macro_prefix, $copyright, $build_id_file)
dnl
AC_DEFUN(SNERT_INIT,[
	snert_macro_prefix="$1"

	AC_COPYRIGHT([$2])

	dnl Used for summary display. Note that the build number should be
	dnl passed on the compiler command line within the makefiles using
	dnl -D in order to make sure we have the most recent build number.
	dnl
	dnl Placing the build number into config.h using substitutions
	dnl means we have to rerun ./configure in order update config.status
	dnl when the build number changes. This is slow and cumbersome during
	dnl development.

	AC_SUBST(package_copyright, ['$2'])

	AC_SUBST(package_version, [[`cat $srcdir/VERSION.TXT`]])
	AC_SUBST(package_major, [[`expr "$package_version" : "\([0-9]*\)\.[0-9]*\.[0-9]*"`]])
	AC_SUBST(package_minor, [[`expr "$package_version" : "[0-9]*\.\([0-9]*\)\.[0-9]*"`]])
	AC_SUBST(package_build, [[`expr "$package_version" : "[0-9]*\.[0-9]*\.\([0-9]*\)"`]])

	AC_SUBST(package_string, "${PACKAGE_NAME} ${package_version}")
	AC_SUBST(package_number, [[`printf "%d%03d%03d" $package_major $package_minor $package_build`]])

	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_NAME, ["$PACKAGE_NAME"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_MAJOR, $package_major)
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_MINOR, $package_minor)
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_BUILD, $package_build)
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_VERSION, ["$package_version"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_STRING, ["$package_string"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_NUMBER, $package_number)

	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_AUTHOR, ["$PACKAGE_BUGREPORT"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_COPYRIGHT, ["$package_copyright"])

	snert_configure_command="$[]0"
	for arg in $ac_configure_args; do
		dnl skip environment variables that should appear BEFORE configure
		AS_CASE([$arg],
		[CFLAGS=*|LDFLAGS=*],[
			continue
		])
		dnl Remove previous quoting of single quote, place single quotes around option value
		arg=`echo "$arg" | sed -e "s/'\\\\\\\\'//g" -e "s/^'\(.*\)'$/\1/" -e "s/\([[^=]]*=\)\([[^']].*[[^']]\)/\1'\2'/"`
		snert_configure_command="${snert_configure_command} [$]arg"
	done
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_CONFIGURE, ["$snert_configure_command"])

	snert_build_date=`date +'%a, %d %b %Y %H:%M:%S %z'`
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_BUILT, ["$snert_build_date"])

	SNERT_PLATFORM
	SNERT_CHECK_CONFIGURE

	dnl Define some default vaules for milters subsitutions.
	AS_IF([test X"$1" = X'MILTER'],[
		test "$prefix" = NONE -a "$localstatedir" = '${prefix}/var' && localstatedir='/var'
		AC_DEFINE_UNQUOTED(snert_milter_t_equate, [C:5m;S=10s;R=10s;E:5m])
		AH_TEMPLATE(snert_milter_t_equate,[Milter t= settings])
	])

	AS_ECHO
	AS_ECHO("$PACKAGE_NAME/$package_version")
	AS_ECHO("$package_copyright")
	AS_ECHO
])

AC_DEFUN(SNERT_FINI,[
	dnl Append CPPFLAGS to CFLAGS until we convert to use automake.
	CFLAGS="$CFLAGS $CPPFLAGS"

	dnl Escape double-quotes in CFLAGS for -DMACRO='"string"' case.
	quote_cflags=$(echo $CFLAGS | sed -e's/"/\\&/g')
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_CFLAGS, ["$quote_cflags"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_CPPFLAGS, ["$CPPFLAGS"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_LDFLAGS, ["$LDFLAGS"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_LIBS, ["$LIBS"])
	AC_DEFINE_UNQUOTED(${snert_macro_prefix}_SHARE, ["$datarootdir/share"])
])

AC_DEFUN(SNERT_SUMMARY,[
	AS_ECHO
	AS_ECHO("$PACKAGE_NAME/$package_major.$package_minor.$package_build")
	AS_ECHO("$package_copyright")
	AS_ECHO
	AC_MSG_RESULT([  Platform.......: $platform $CC ${GCC_MAJOR} ${GCC_MINOR} ${GCC_PATCH}])
	AC_MSG_RESULT([  CFLAGS.........: $CFLAGS])
	AC_MSG_RESULT([  CPPFLAGS.......: $CPPFLAGS])
	AC_MSG_RESULT([  LDFLAGS........: $LDFLAGS])
	AC_MSG_RESULT([  LIBS...........: $LIBS])
	AC_MSG_RESULT([  prefix.........: $prefix])
	AC_MSG_RESULT([  datarootdir....: $datarootdir])
])
