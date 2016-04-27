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

<div style="float: right;">
[<a href="index.php">Jobs</a>]&nbsp;&nbsp;<br/>
</div>

<h1><?= $file ?></h1>

<div align="center">
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
		print "<td><a href='phpwhois.php?query={$row[0]}'>{$row[0]}</a></td>";
		for ($i = 1; $i < $cols; $i++) {
			print "<td>{$row[$i]}</td>";
		}
		print "</tr>\n";
	}

	fclose($fd);
}
?>
</table>
</div>

	</div>
</div>
</body>
</html>
