/*
 * TextSplit.c
 *
 * Copyright 2001, 2006 by Anthony Howe. All rights reserved.
 */

#include <stdlib.h>
#include <com/snert/lib/version.h>
#include <com/snert/lib/util/Text.h>
#include <com/snert/lib/util/Token.h>

/***********************************************************************
 ***
 ***********************************************************************/

/**
 * <p>
 * The given string contains a list of substrings separated by the
 * specified delimiter characters. The substrings may contain quoted
 * strings and/or contain backslash-escaped characters. The common
 * backslash escape sequences are supported and return their ASCII
 * values.
 * </p>
 *
 * @param string
 *	A list represented as a string.
 *
 * @param delims
 *	A set of delimiter characters.
 *
 * @param returnEmptyTokens
 *	If false then a run of one or more delimeters is treated as a
 *	single delimeter separating tokens. Otherwise each delimeter
 *	separates a token that may be empty.
 *
 *	string		true		false
 *	-------------------------------------------
 *	[a,b,c]		[a] [b] [c]	[a] [b] [c]
 *	[a,,c]		[a] [] [c]	[a] [c]
 *	[a,,]		[a] [] [] 	[a]
 *	[,,]		[] [] []	(empty vector)
 *	[]		[]		(empty vector)
 *
 * @return
 *	A vector of C strings.
 */
Vector
TextSplit(const char *string, const char *delims, int returnEmptyTokens)
{
	char *token;
	Vector list;

	if ((list = VectorCreate(5)) == NULL)
		return NULL;

	VectorSetDestroyEntry(list, free);

	while ((token = TokenNext(string, &string, delims, returnEmptyTokens)) != NULL)
		VectorAdd(list, token);

	return list;
}

