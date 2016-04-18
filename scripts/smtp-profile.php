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
	$out = shell_exec("echo '{$profile} {$file}; rm {$file}' | at -M now 2>&1");
	if (is_null($out)) {
		$msg = 'Failed to start job.';
	} else {
		$msg = 'Started '.$out;
		sleep(2);
	}
}

$msg = '';
if (empty($_POST['action']))
	$_POST['action'] = 'NONE';

switch ($_POST['action']) {
case 'UPLOAD':
	if (file_exists($_FILES['file']['tmp_name'])) {
		$tmp = tempnam($jobdir, "job_");
		move_uploaded_file($_FILES['file']['tmp_name'], $tmp);
		chmod($tmp, 0644);
		start_job($tmp);
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
				unlink($jobdir.'/'.$job);
			} else if (file_exists($jobdir.'/'.$job.'.log')) {
				array_map('unlink', glob($jobdir.'/'.$job.'*'));
			}
		}
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
<div style="float: right;">
[<a href="admin.php"><?= $_SERVER['PHP_AUTH_USER'] ?></a>]</div>
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

<textarea name="list" rows="6" cols="40"></textarea>
<br/>
<input type="submit" name="action" value="SUBMIT"/>
<br/>

<?php
if (0 < count($busy)) {
	print "<h2>Jobs In Progress</h2><ul>";
	foreach ($busy as $job) {
		if (($fd = fopen($jobdir.'/'.$job.'.count', 'r'))) {
			fscanf($fd, '%d %d', $count, $total);
			fclose($fd);
		}
		print "<li>{$job} ... {$count} / {$total}</li>\n";
	}
	print "<br/><input type='submit' name='action' value='REFRESH'></ul>\n";
}
?>
<?php
if (count($jobs) > 0 || file_exists("{$jobdir}/spamhaus.txt")) {
	print "<h2>Completed Jobs</h2><ul>";

	if (file_exists("{$jobdir}/spamhaus.txt")) {
		print "<li>";
		print "<input type='checkbox' name='job[]' value='spamhaus.txt'/> SpamHaus Hit List ...";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/spamhaus.txt\">[.txt]</a>";
		print "</li>\n";
	}

	foreach ($jobs as $job) {
		print "<li>";
		print "<input type='checkbox' name='job[]' value='{$job}'/> {$job} ...";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/{$job}.csv\">[.csv]</a>";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/{$job}.log\">[.log]</a>";
		print "&nbsp;&nbsp;<a href=\"{$joburi}/{$job}.job\">[.job]</a>";
		print "</li>\n";
	}
	print '<br/><input type="submit" name="action" value="DELETE">&nbsp;&nbsp;<input type="submit" name="action" value="REFRESH">';
}
?>
</ul>
</form>

	</div>
</div>
</body>
</html>
