#!/usr/local/bin/php
<?php

/**
 * @param optstring
 *	getopt(3) style options string.
 *
 * @return
 *	An array of option character and value pairs.  Boolean options
 *	have a TRUE value.  The special key 'optind' contains the index
 *	of the first argument in $argv[].  For unknown options or missing
 *	option value, a string of the invalid option character.
 */
function getopt2($optstring)
{
	global $argc, $argv;

	$options = Array();
	$optlen = strlen($optstring);

	for ($argi = 1; $argi < $argc; $argi++) {
		$arg = $argv[$argi];
		$arglen = strlen($arg);
		if ($optlen == 0)
			continue;

		// Start of arguments?
		if ($arg[0] != '-')
			break;

		// Scan option string.
		for ($i = 1; $i < $arglen; $i++) {
			// Valid option?
			if (($pos = strpos($optstring, $arg[$i])) === FALSE)
				// Return invalid option character.
				return $arg[$i];

			// Option parameter?
			if ($pos+1 < $optlen && $optstring[$pos+1] == ':') {
				// Option parameter.
				if ($i+1 < $arglen) {
					// Joined option value.
					$options[$arg[$i]] = substr($arg, $i+1);
				} else if ($argi+1 < $argc) {
					// Separated option value.
					$options[$arg[$i]] = $argv[++$argi];
				} else {
					// Return invalid option character.
					return $arg[$i];
				}
				break;
			}

			// Boolean option.
			$options[$arg[$i]] = TRUE;
		}
	}
	$options['optind'] = $argi;

	return $options;
}

$options = getopt2("ac:b");
if (is_string($options)) {
	print("Unknown option {$options}\n");
	print("usage: getopt2.php [-ab][-c value] [...]\n");
	exit(2);
}

print_r($options);

for ($argi = $options['optind']; $argi < $argc; $argi++)
	printf("[%d] => %s\n", $argi, $argv[$argi]);
?>
