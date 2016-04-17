<?php
$scriptdir = dirname($_SERVER['SCRIPT_FILENAME']);
$config = parse_ini_file($scriptdir.'/smtp-profile.cf');
$jobdir = $config['JOBDIR'];
$profile = $scriptdir.'/smtp-profile.sh -v';
$self = 'http://'. $_SERVER['SERVER_NAME'] . $_SERVER['PHP_SELF'];

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
		sleep(4);
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
				continue;
			}
			if (file_exists($jobdir.'/'.$job.'.job')) {
				unlink($jobdir.'/'.$job.'.job');
				unlink($jobdir.'/'.$job.'.csv');
				unlink($jobdir.'/'.$job.'.log');
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
	<div class="page">

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
if (count($jobs) > 0 || file_exists("jobs/spamhaus.txt")) {
	print "<h2>Completed Jobs</h2><ul>";

	if (file_exists("jobs/spamhaus.txt")) {
		print "<li>";
		print "<input type='checkbox' name='job[]' value='spamhaus.txt'/> SpamHaus Hit List ...";
		print "&nbsp;&nbsp;<a href=\"jobs/spamhaus.txt\">[.txt]</a>";
		print "</li>\n";
	}

	foreach ($jobs as $job) {
		print "<li>";
		print "<input type='checkbox' name='job[]' value='{$job}'/> {$job} ...";
		print "&nbsp;&nbsp;<a href=\"jobs/{$job}.csv\">[.csv]</a>";
		print "&nbsp;&nbsp;<a href=\"jobs/{$job}.log\">[.log]</a>";
		print "&nbsp;&nbsp;<a href=\"jobs/{$job}.job\">[.job]</a>";
//		print "&nbsp;&nbsp;<a href=\"jobs/{$job}.mx\">[.mx]</a>";
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
