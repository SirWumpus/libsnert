<?php
// Load composer framework
require('./vendor/autoload.php');

use phpWhois\Whois;
//use phpWhois\Utils;

//
// @param optstring
//	getopt(3) style options string.
//
// @return
//	An array of option character and value pairs.  Boolean options
//	have a TRUE value.  The special key 'optind' contains the index
//	of the first argument in $argv[].  For unknown options or missing
//	option value, a string of the invalid option character.
//
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

function array_grep($data, $pattern)
{
	$out = Array();
	foreach ($data as $row) {
		if (preg_match($pattern, $row)) {
			$out[] = trim(preg_replace('/^.+: */', '', $row));
		}
	}
	return $out;
}

function array_grep_join($glue, $data, $pattern)
{
	$out = array_grep($data, $pattern);
	if (count($out) <= 0)
		return '';
	return implode($glue, $out);
}

function whois_error_handler($errlevel, $errstr, $errfile, $errline)
{
	// Ignore phpWhois library errors.
	if (error_reporting() == 0)
		return;
	if (strpos($errlevel, "deprecated") === TRUE)
		return TRUE;

//	throw new Exception($errstr, $errlevel);
}

function whois_domain($domain)
{
	global $options;

	$whois = new Whois();
	set_error_handler('whois_error_handler');
	$result = $whois->lookup($domain, false);
	restore_error_handler();

	if (!isset($options['c'])) {
		foreach ($result['rawdata'] as $line)
			printf("%s\n", $line);
		return;
	}

	$fields = Array();
	$fields[] = $domain;

	// ICANN 2013 Standard Whois Format
	// https://www.icann.org/resources/pages/approved-with-specs-2013-09-17-en#whois
	$fields[] = array_grep_join('', $result['rawdata'], '/^.*Organization:/i');
	$fields[] = array_grep_join(', ', $result['rawdata'], '/Registrant (Street|City|State|Post|Country)/i');
	$fields[] = array_grep_join(' ', $result['rawdata'], '/Registrant Phone/i');
	$fields[] = array_grep_join('', $result['rawdata'], '/Registrant Email/i');
	$fields[] = array_grep_join('', $result['rawdata'], '/Admin Email/i');
	$fields[] = array_grep_join('', $result['rawdata'], '/Tech Email/i');

	// Prior to 2013 registrars whois servers could return data
	// in a non-standard format.
	if (empty($fields[3])) {
		// Non-standard format.  Try find any/all phone numbers.
		$fields[3] = array_grep_join(', ', $result['rawdata'], '/\+\d+[-. \d]+/i');
	}
	if (empty($fields[4])) {
		// Non-standard format.  Try find any/all email addresses.
		$fields[4] = array_grep_join(', ', $result['rawdata'], '/@\w+(.\w+)+/i');
	}

	fputcsv(STDOUT, $fields);
}

function whois_file($file)
{
	global $options;

	if ($fd = fopen($file, 'r')) {
		while ($line = fgets($fd)) {
			if (!isset($options['c']))
				printf("\n----=_%s\n", $line);
			whois_domain(trim($line));
		}
		fclose($fd);
	}
}

function usage()
{
	print("usage: whois.php [-c] domain|file ...\n");
	exit(2);
}

$options = getopt2('c');
if (is_string($options)) {
	print("Unknown option {$options}\n");
	usage();
} else if ($argc <= $options['optind']) {
	usage();
}

if (isset($options['c']))
	print("domain, owner_organization, owner_address, owner_phone, owner_email, admin_email, tech_email\n");

for ($argi = $options['optind']; $argi < $argc; $argi++) {
	if (file_exists($argv[$argi]))
		whois_file($argv[$argi]);
	else
		whois_domain($argv[$argi]);
}
?>
