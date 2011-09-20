handle SIG32 nostop noprint pass
handle SIG33 nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
handle SIGPIPE nostop pass
handle SIGTERM nostop noprint pass
handle SIGQUIT nostop pass
#handle SIGINT nostop pass
set listsize 24

b main
#b server_io_cb
#b client_io_cb
#b client_close_cb
#b mx_open
#b mx_read
#b mx_io_cb
#b mx_error_cb
b cmd_quit
#b cmd_accept
#b cmd_xclient
#b client_io_cb
#b stdin_bootstrap_cb
#b cmd_interpret
#b lua_hook_do
#b dns_open
#b dns_close
#b eventsWait
#b dns_io_db
#run +test script=smtpe.lua
#b lua_hook_initaccept

run file=./smtpe.cf
