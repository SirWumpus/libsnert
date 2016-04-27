<?php
$scriptdir = dirname($_SERVER['SCRIPT_FILENAME']);
$config = parse_ini_file($scriptdir.'/smtp-profile.cf');
$jobdir = $config['JOBDIR'];
$joburi = 'jobs';

// Separate jobs by authenticated users.  Not secure.
if (isset($_SERVER['PHP_AUTH_USER'])) {
	$userdir = $jobdir.'/'.$_SERVER['PHP_AUTH_USER'];
	$joburi = $joburi.'/'.$_SERVER['PHP_AUTH_USER'];
	if (!file_exists($userdir)) {
		mkdir($userdir, 0777, true);
		chmod($userdir, 0777);
		chmod($jobdir, 0777);
	}
	$jobdir=$userdir;
}

$smtpping = $scriptdir."/smtp-profile.sh -v -j {$jobdir}";

function start_job($file)
{
	global $msg, $smtpping;
	// This places the long running script into a scheduled
	// background task.  For this to work, the nginx user
	// account needs an assigned shell, not /sbin/nologin.
	$out = shell_exec("echo '{$smtpping} -r{$_POST['retry']} -p{$_POST['pause']} {$file}; rm {$file}' | at -M now 2>&1");
	if (is_null($out)) {
		$msg = 'Failed to start job.';
		return false;
	} else {
		$msg = 'Started SMTP Ping '.$out;
		sleep(4);
	}
	return true;
}

$msg = '';
if (empty($_POST['action']))
	$_POST['action'] = 'NONE';

switch ($_POST['action']) {
case 'PING':
	if (!empty($_POST['list'])) {
		$tmp = tempnam($jobdir, "ping_");
		if (($fd = fopen($tmp, "w"))) {
			chmod($tmp, 0644);
			foreach ($_POST['list'] as $domain) {
				fwrite($fd, $domain);
				fwrite($fd, "\n");
			}
			fclose($fd);
			if (start_job($tmp)) {
				// Redirect back to main page.
				$scripturl = dirname($_SERVER['SCRIPT_NAME']);
				header("Location: http://{$_SERVER['SERVER_NAME']}{$scripturl}");
				exit();
			}
		} else {
			$msg = "{$tmp}: open error";
		}
	}
	break;

default:
	// Nothing.
}

?>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<title>
SMTP Pinger
</title>
<link rel="stylesheet" type="text/css" href="smtp-profile.css">
</head>
<body>
<div class="container">
	<div class="page lhs">

<div style="float: right;">
[<a href="index.php">Jobs</a>]<br/>
</div>

<h1>SMTP Pinger</h1>
<p class="error"><?= $msg ?></p>
<h2>Start Job</h2>

<form name="ping" method="POST" enctype="multipart/form-data" action="<?= $_SERVER['PHP_SELF'] ?>">

<table cellpadding="3" cellspacing="1" border="0" width="75%">
<tr>
	<td align='right'>Retries Per Domain:</td>
	<td><input type="text" size="5" maxlength="2" name="retry" value="<?= $config['PINGRETRY'] ?>"/></td>
	<td align='right'>Pause Between Domains:</td>
	<td><input type="text" size="5" maxlength="4" name="pause" value="<?= $config['PINGPAUSE'] ?>"/></td>
</tr>
</table>

<h2>Domains Using Public SpamHaus Zen BL</h2>
<?php
if (($fd = fopen("{$jobdir}/spamhaus.txt", 'r'))) {
	while (($line = fgets($fd))) {
		$domain = trim($line);
		print "<input type='checkbox' name='list[]' value='{$domain}'/> {$domain} <br/>\n";
	}
	fclose($fd);
}
?>
<br/><input type="submit" name="action" value="PING">
</form>

	</div>
</div>
</body>
</html>
