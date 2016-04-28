<?php
$scriptdir = dirname($_SERVER['SCRIPT_FILENAME']);
$config = parse_ini_file($scriptdir.'/smtp-profile.cf');
$password = $scriptdir.'/addpasswd.sh ';
$pwdfile = $_SERVER['DOCUMENT_ROOT'].'/users.pwd';

function check_pass_fields($p1, $p2)
{
	global $msg;

	if (empty($p1) || empty($p2)) {
		$msg = "Missing password fields.";
		return false;
	} else if ($p1 != $p2) {
		$msg = "Passwords do not match.  Please try again.";
		return false;
	}

	$msg = '';
	return true;
}

$msg = '';
if (empty($_POST['action']))
	$_POST['action'] = 'NONE';
switch ($_POST['action']) {
case 'ADD':
	if (empty($_POST['user'])) {
		$msg = 'Missing user name.';
	} else if (check_pass_fields($_POST['addpw1'], $_POST['addpw2'])) {
		$cmd = $password.' -p "'.$_POST['addpw1'].'" '.$pwdfile.' '.$_POST['user'];
		shell_exec($cmd);
	}
	break;

case 'CHANGE':
	if (check_pass_fields($_POST['pass1'], $_POST['pass2'])) {
		$cmd = $password.' -p "'.$_POST['pass1'].'" '.$pwdfile.' '.$_SERVER['PHP_AUTH_USER'];
		shell_exec($cmd);
	}
	break;

case 'DELETE':
	if (!empty($_POST['users'])) {
		foreach ($_POST['users'] as $user) {
			shell_exec($password.' -d '.$pwdfile.' '.$user);
		}
	}
	break;

default:
}

?>
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
<title>
SMTP Profiler User Management
</title>
<link rel="stylesheet" type="text/css" href="smtp-profile.css">
</head>
<body>
<div class="container">
	<div class="page lhs">

<div style="float: right;">
[<a href="index.php">Jobs</a>]<br/>
[<a href="admin.php"><?= $_SERVER['PHP_AUTH_USER'] ?></a>]<br/>
<!--
This doesn't work 100%; only reliable way is restart browser and login with different credentials
[<a href="http://_logout_:_out_@<?= $_SERVER['SERVER_NAME'] ?>">Logout</a>]
-->
</div>
<h1>User Management</h1>
<p class="error"><?= $msg ?></p>

<form name="admin" method="POST" enctype="multipart/form-data" action="<?= $_SERVER['PHP_SELF'] ?>">
<?php if ($_SERVER['PHP_AUTH_USER'] == 'admin' || $_SERVER['PHP_AUTH_USER'] == 'achowe') { ?>
	<h2>Add User</h2>
	<table cellpadding="0" cellspacing="3" border="0">
	<tr><td align="right">Username:</td><td><input type="text" name="user" size="32" /></td></tr>
	<tr><td align="right">Password:</td><td><input type="password" name="addpw1" size="32" /></td></tr>
	<tr><td align="right">Repeat Password:</td><td><input type="password" name="addpw2" size="32" /></td></tr>
	<tr><td align="right" colspan="2"><input type="submit" name="action" value="ADD"/></td></tr>
	</table>

<?php
	if (($fd = fopen($pwdfile, 'r'))) {
		print "<h2>Delete Users</h2>\n";
		while (fscanf($fd, '%[^:]:%s', $user, $hash) == 2) {
			if ($user == 'admin')
				continue;
			print "<input type='checkbox' name='users[]' value='{$user}'/> {$user}<br/>\n";
		}
		fclose($fd);
		print "<input type='submit' name='action' value='DELETE'/>\n";
	}
?>

<?php } ?>

<h2>Change Your Password</h2>
<table cellpadding="0" cellspacing="3" border="0">
<tr><td align="right">Password:</td><td><input type="password" name="pass1" size="32" /></td></tr>
<tr><td align="right">Repeat Password:</td><td><input type="password" name="pass2" size="32" /></td></tr>
<tr><td align="right" colspan="2"><input type="submit" name="action" value="CHANGE"/></td></tr>
</table>
</form>

	</div>
</div>
</body>
</html>
