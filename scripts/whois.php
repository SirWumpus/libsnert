<?php
// Load composer framework
require(__DIR__.'/vendor/autoload.php');

use phpWhois\Whois;

$scriptdir = dirname($_SERVER['SCRIPT_FILENAME']);
$config = parse_ini_file($scriptdir.'/smtp-profile.cf');

if (empty($_GET['query'])) {
	$rc = NULL;
	$msg = "Missing domain name to lookup."
} else {
	$query = $_GET['query'];
	$whois = new Whois();
	$rc = $whois->lookup($query, false);
}
?>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<title>
Whois Viewer
</title>
<link rel="stylesheet" type="text/css" href="smtp-profile.css">
</head>
<body>

<?php if (isset($msg)) { ?>

	<p class="error"><?= $msg ?></p>

<?php } else { ?>

	<h1>Whois for <?= $query ?></h1>
	[<a href="index.php">Jobs</a>]<br/>

	<?php if (empty($rc)) { ?>

	<?php } else { ?>
		<pre>
		<?php
		print_r($result);
		?>
		</pre>
	<?php } ?>

	[<a href="index.php">Jobs</a>]<br/>
<?php } ?>

</body>
</html>
