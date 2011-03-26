handle SIG32 nostop noprint pass
handle SIG33 nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
handle SIGPIPE nostop pass
handle SIGTERM nostop pass
handle SIGQUIT nostop pass
handle SIGINT nostop pass
set listsize 24

b main
#b mx_open
#b mx_read
#b mx_io_cb
#b mx_error_cb
b cmd_quit
run -daemon script=smtpe.lua -rfc2920-pipelining-reject +rfc2920-pipelining-enable +smtp-xclient smtp-smart-host=127.0.0.1:26 verbose="+warn +info +smtp +dns"
