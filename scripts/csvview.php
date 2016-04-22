<?php
$scriptdir = dirname($_SERVER['SCRIPT_FILENAME']);
$config = parse_ini_file($scriptdir.'/smtp-profile.cf');
$file = $_GET['file'];

?>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<title>
CSV Viewer
</title>
<link rel="stylesheet" type="text/css" href="smtp-profile.css">
</head>
<body>

<h1><?= $file ?></h1>

<div align="center">
[<a href="index.php">Jobs</a>]<br/>
<table cellpadding="3" cellspacing="1" border="1" width="98%">
<?php
if (($fd = fopen($file, 'r')) && ($row = fgetcsv($fd))) {
	$cols = count($row);

	print "<tr>";
	for ($i = 0; $i < $cols; $i++) {
		print "<th>{$row[$i]}</th>";
	}
	print "</tr>\n";

	while (($row = fgetcsv($fd))) {
		print "<tr>";
		for ($i = 0; $i < $cols; $i++) {
			print "<td>{$row[$i]}</td>";
		}
		print "</tr>\n";
	}

	fclose($fd);
}
?>
</table>
[<a href="index.php">Jobs</a>]<br/>
</div>

	</div>
</div>
</body>
</html>
