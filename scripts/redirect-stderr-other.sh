#!/bin/sh
# https://stackoverflow.com/questions/9112979/pipe-stdout-and-stderr-to-two-different-processes-in-shell-script

# General structure:
#
#	{ command 2>&1 1>&3 3>&- | stderr_command; } 3>&1 1>&2 | stdout_command
# 
# Notice:
# 
#	3>&- is required to prevent fd 3 from being inherited by command.
#	(As this can lead to unexpected results depending on what command
#	does inside.)
# 
# Parts explained:
# 
#   Outer part first:
#	3>&1 -- fd 3 for { ... } is set to what fd 1 was (i.e. stdout)
#	1>&2 -- fd 1 for { ... } is set to what fd 2 was (i.e. stderr)
#	| stdout_command -- fd 1 (was stdout) is piped through stdout_command
# 
#   Inner part inherits file descriptors from the outer part:
#	2>&1 -- fd 2 for command is set to what fd 1 was (i.e. stderr as per outer part)
#	1>&3 -- fd 1 for command is set to what fd 3 was (i.e. stdout as per outer part)
#	3>&- -- fd 3 for command is set to nothing (i.e. closed)
#	| stderr_command -- fd 1 (was stderr) is piped through stderr_command
# 

# Silly example that takes sftp-server stderr (-e) and pipes to logger.  
# Without -e sftp-server already logs to syslog.  However one can replace
# logger with some clever filter that fires events based on put/get/rename
# actions for example.

{ sft-server -e -l INFO "$@" 2>&1 1&3 3>&- | logger -i -t stderr ; } 3>&1 1>&2

