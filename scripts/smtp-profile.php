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

$profile = $scriptdir."/smtp-profile.sh -v -j {$jobdir}";

function start_job($file)
{
	global $msg, $profile;
	// This places the long running script into a scheduled
	// background task.  For this to work, the nginx user
	// account needs an assigned shell, not /sbin/nologin.
	$out = shell_exec("echo \"{$profile} '{$file}'; rm '{$file}'\" | at -M now 2>&1");
	if (is_null($out)) {
		$msg = 'Failed to start job.';
	} else {
		$msg = 'Started '.$out;
		sleep(4);
	}
}

$msg = '';
if (empty($_POST['action']))
	$_POST['action'] = 'NONE';

switch ($_POST['action']) {
case 'UPLOAD':
	if (file_exists($_FILES['file']['tmp_name'])) {
		$tmp = $jobdir.'/'.$_FILES['file']['name'];
		if (move_uploaded_file($_FILES['file']['tmp_name'], $tmp)) {
			chmod($tmp, 0644);
			start_job($tmp);
		} else {
			$msg = 'Error moving uploaded file.';
		}
	}
	break;

case 'SUBMIT':
	if (!empty($_POST['list'])) {
		$tmp = tempnam($jobdir, "job_");
		if (($fd = fopen($tmp, "w"))) {
			fwrite($fd, $_POST['list']);
			fwrite($fd, "\n");
			fclose($fd);
			chmod($tmp, 0644);
			start_job($tmp);
		}
	}
	break;

case 'DELETE':
	if (!empty($_POST['job'])) {
		foreach ($_POST['job'] as $job) {
			if ($job == 'spamhaus.txt' && file_exists($jobdir.'/'.$job)) {
				unlink($jobdir.'/spamhaus.txt');
				unlink($jobdir.'/whois.csv');
			} else if (file_exists($jobdir.'/'.$job.'.log')) {
				array_map('unlink', glob($jobdir.'/'.$job.'*'));
			}
		}
	}
	break;

case 'CLEAN':
	if ($dir = opendir($jobdir)) {
		while ($entry = readdir($dir)) {
			if (!preg_match('/\.(busy|count|mx|lock)$/', $entry)
			&&   preg_match('/^\.|^[0-9]|^spamhaus|^whois/', $entry))
				continue;
			unlink($jobdir.'/'.$entry);
		}
		closedir($dir);
	}
	break;

case 'REMOVE':
	if (!empty($_POST['queue'])) {
		$ids = implode(' ', $_POST['queue']);
		$out = shell_exec("at -r {$ids} 2>&1");
		if (!empty($out))
			$msg = $out;
	}
	break;

default:
	// Nothing.
}

// Get list of jobs.
$jobs = Array();
$busy = Array();
if ($dir = opendir($jobdir)) {
	while ($entry = readdir($dir)) {
		$path = pathinfo($entry);

		if (!isset($path['extension'])) {
			;
		} else if ($path['extension'] == 'count') {
			$busy[] = $path['filename'];
		} else if ($path['extension'] == 'job') {
			$jobs[] = $path['filename'];
		}
	}
	closedir($dir);

	natsort($busy);
	natsort($jobs);
	$busy = array_reverse($busy);
	$jobs = array_reverse($jobs);
}


?>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<title>
SMTP Profiler
</title>
<link rel="stylesheet" type="text/css" href="smtp-profile.css">
</head>
<body>
<div class="container">
	<div class="page lhs">
<div style="float: right;">[<a href="admin.php"><?= $_SERVER['PHP_AUTH_USER'] ?></a>]</div>
<h1>SMTP Profiler</h1>
<p class="error"><?= $msg ?></p>
<h2>Start Job</h2>

<form name="admin" method="POST" enctype="multipart/form-data" action="<?= $_SERVER['PHP_SELF'] ?>">
<p>
Supply a text file containing domains and/or email addresses:
</p>
<input type="file" name="file" size="60"/>
<input type="submit" name="action" value="UPLOAD"/>

<p>
Or enter one or more domains and/or email addresses below:
</p>

<textarea name="list" rows="5" cols="60"></textarea>
<br/>
<input type="submit" name="action" value="SUBMIT"/>
<br/>

<?php
$queued = shell_exec("atq");
if (!empty($queued)) {
?>

<h2>Job Queue</h2>
<table cellpadding="2" cellspacing="1" border="0" width="100%">

<?php
	$lines = explode(PHP_EOL, $queued);
	foreach ($lines as $line) {
		if (empty($line))
			continue;
		$words = preg_split("/\s+/", $line);

		// Is job is in progress?
		if ($words[6] == '=')
			printf("<td>&nbsp;&nbsp; %s</td></tr>\n", $line);
		else
			printf("<td><input type='checkbox' name='queue[]' value='%s'/> %s</td></tr>\n", $words[0], $line);
	}
?>
</table>
<br/>
<input type='submit' name='action' value='REMOVE'>

<?php } ?>

<?php if (0 < count($busy)) { ?>

<h2>Jobs In Progress</h2>
<table cellpadding="2" cellspacing="1" border="0" width="100%">

<?php
	foreach ($busy as $job) {
		if (($fd = fopen($jobdir.'/'.$job.'.count', 'r'))) {
			fscanf($fd, '%d %d', $count, $total);
			fclose($fd);
		}
		print "<tr><td width='50%'>{$job}</td><td>... {$count} / {$total}</td></tr>\n";
	}
?>

</table>
<br/>
<input type='submit' name='action' value='REFRESH'>

<p>
If the machine is rebooted with jobs pending or in progress, then
job working files might appear in the list above.  Click this button
to
</p>
<input type='submit' name='action' value='CLEAN'>


<?php } ?>

<?php if (count($jobs) > 0 || file_exists("{$jobdir}/spamhaus.txt")) { ?>

<h2>Completed Jobs</h2>
<script>
<!--
function checkAll(source)
{
	var checkboxes = document.getElementsByName(source.name);
	for (var i = 0; i < checkboxes.length; i++) {
		if (checkboxes[i].type == 'checkbox') {
			checkboxes[i].checked = source.checked;
		}
	}
}
-->
</script>
<table cellpadding="2" cellspacing="1" border="0" width="100%">
<tr><td width="50%"><input type='checkbox' name='job[]' onchange="checkAll(this)"/></td></tr>
<?php
	if (file_exists("{$jobdir}/spamhaus.txt")) {
		print "<tr>";
		print "<td width='50%'><input type='checkbox' name='job[]' value='spamhaus.txt'/> <a href='smtp-ping.php'>SpamHaus Hit List</a></td>";
		print "<td>... <a href=\"{$joburi}/spamhaus.txt\">[.txt]</a>";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/whois.csv\">[whois.csv]</a>";
		print "</td></tr>\n";
	}

	foreach ($jobs as $job) {
		print "<tr>";
		print "<td width='50%'><input type='checkbox' name='job[]' value='{$job}'/> <a href=\"csvview.php?file={$joburi}/{$job}.csv\">{$job}</a></td>";
		print "<td>... <a href=\"{$joburi}/{$job}.csv\">[.csv]</a>";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/{$job}.log\">[.log]</a>";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/{$job}.job\">[.job]</a>";
		print "</td></tr>\n";
	}
?>
</table>
<br/>
<input type="submit" name="action" value="DELETE">&nbsp;&nbsp;<input type="submit" name="action" value="REFRESH">

<?php } ?>
</form>

	</div>
</div>
</body>
</html>
