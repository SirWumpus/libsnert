/*
 * jspr (Jasper) - JSON string path recovery
 *
 */

#ifndef __jspr_h__
#define __jspr_h__   1

#ifdef __cplusplus
extern "C" {
#endif

extern int jspr_debug;

#define	JSPR_KEY_NAME		0x0001

/**
 * @param js
 *	A JSON object or array string.
 *
 * @param labels
 *	A NULL terminated array of JSON labels to search.  A label
 *	can be an array index.  If a label is an index and the
 *	current value being walked is an object, then index will
 *	refer to the Nth key found.
 *
 * @param span
 *	Passback the span of the value string found.
 *
 * @param flags
 *	If JSPR_KEY_NAME is passed, return the key name found
 *	at object index instead, the value.
 *
 * @return
 *	A pointer within js to the start of the JSON value string.
 *	If value is a string, the opening double quote is skipped,
 *	and span reduced by one to skip the closing double quote.
 */
extern const char *jspr_find_labels(const char *js, const char **labels, int *span, int flags);

/**
 * @param js
 *	A JSON object or array string.
 *
 * @param path
 *	A dot separated string of labels.  A label can be a array
 *	index.  If a label is an index and the current value being
 *	walked is an object, then index will refer to the Nth key
 *	found.
 *
 * @param span
 *	Passback the span of the value string found.
 *
 * @param flags
 *	If JSPR_KEY_NAME is passed, return the key name found
 *	at object index instead, the value.
 *
 * @return
 *	A pointer within js to the start of the JSON value string.
 *	If value is a string, the opening double quote is skipped,
 *	and span reduced by one to skip the closing double quote.
 */
extern const char *jspr_find_path(const char *js, const char *path, int *span, int flags);

#ifdef  __cplusplus
}
#endif

#endif /* __jspr_h__ */


