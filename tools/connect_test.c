/* Test of connect() behavior when interrupted */

/* David Madore <david.madore@ens.fr> - 2003-04-25 - Public Domain */

/* This program forks to two processes: the father process does
 * nothing but continuously send USR1 signals to the child process,
 * and dies when the latter exits.  The child process ignores USR1
 * signals, but they will likely cause interrupted system calls.  The
 * child process attempts to connect to CONNECT_ADDRESS on
 * CONNECT_PORT.  The goal is to produce an interrupted system call on
 * connect() to check its behavior.  Therefore, as long as this does
 * not occurr, the child process will close the connection as soon as
 * it succeeds, and try again (up to GIVEUP attempts will be made).
 * Once connect() is interrupted, there are two possible tests: if
 * TEST_TWO is set to 1, the program will poll() for completion of the
 * asynchronous connection attempt that SUSv3 prescribes will then
 * take place; if TEST_TWO is undefined or set to 0, the program will
 * retry the connect() call with the same arguments, and persist while
 * the latter returns EINTR (if it does). */

/* Please define CONNECT_ADDRESS to the IP address of some web server
 * (if possible, one which will not respond too rapidly - but not too
 * slowly either). */

/* If we read the SUSv3 to the letter, the following output should be
 * produced (assuming CONNECT_ADDRESS has been set to 216.239.33.99):
 * with TEST_TWO, "Will try to connect to 216.239.33.99 on port
 * 80\n(connect has been interrupted and now completed
 * successfully)\n"; and without TEST_TWO, "Will try to connect to
 * 216.239.33.99 on port 80\n(connect had been interrupted and now
 * produced an error)\nconnect: Operation already in progress\n" (I
 * think this behavior is utterly stupid, but SUSv3 does seem to
 * require it!). */

/* On Linux, without TEST_TWO, we get "Will try to connect to
 * 216.239.33.99 on port 80\n(connect has been interrupted and now
 * completed successfully)\n" (the same as with TEST_TWO).  On FreeBSD
 * and OpenBSD, we get "Will try to connect to 216.239.33.99 on port
 * 80\n(connect had been interrupted and now produced an
 * error)\nconnect: Adress already in use\n".  On Solaris, we get the
 * per-spec required behavior described above.  All systems function
 * as expected when TEST_TWO is set. */

/* Also see
 * <URL: http://www.eleves.ens.fr:8080/home/madore/computers/connect-intr.html >
 */

#ifndef CONNECT_ADDRESS
#define CONNECT_ADDRESS "127.0.0.1"
#endif

#ifndef CONNECT_PORT
#define CONNECT_PORT 80
#endif

#ifndef GIVEUP
/* 0 means "never". */
#define GIVEUP 100
#endif

#ifndef VERBOSE
#define VERBOSE 0
#endif

#ifndef TEST_TWO
#define TEST_TWO 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#if TEST_TWO
#include <sys/poll.h>
#endif

#include <errno.h>

static void
ignore_handler (int useless)
{
  /* Nothing! */
}

static volatile sig_atomic_t terminate;

static void
terminate_handler (int useless)
{
  terminate = 1;
}

static pid_t child;

static void
killing_loop (void)
{
  while ( ! terminate )
    {
      int retval;

      /* Note race condition here.  Too annoying to fix. */
      retval = kill (child, SIGUSR1);
      if ( retval == -1 )
	{
	  if ( errno == EINTR )
	    continue;
	  perror ("kill");
	  kill (child, SIGTERM);
	  exit (EXIT_FAILURE);
	}
    }
}

static long nbtries;
static long nbsubtries;

static void
child_loop (void)
{
  int socketd;
  struct sockaddr_in addr;
  struct protoent *proto;
  int retval;
  char have_testcase;

  proto = getprotobyname ("tcp");
  if ( ! proto )
    {
      fprintf (stderr, "getprotobyname: Protocol not found\n");
      exit (EXIT_FAILURE);
    }
  memset (&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  retval = inet_pton (AF_INET, CONNECT_ADDRESS, &addr.sin_addr);
  if ( retval == -1 )
    {
      perror ("inet_pton");
      exit (EXIT_FAILURE);
    }
  else if ( retval == 0 )
    {
      fprintf (stderr, "inet_pton: Failed to convert address\n");
      exit (EXIT_FAILURE);
    }
  addr.sin_port = htons (CONNECT_PORT);
  nbtries = 0;
  have_testcase = 0;
  while ( ! have_testcase )
    {
      socketd = socket (PF_INET, SOCK_STREAM, proto->p_proto);
      if ( socketd == -1 )
	{
	  perror ("socket");
	  exit (EXIT_FAILURE);
	}
#if VERBOSE
      fprintf (stderr, "socket: Success\n");
#endif
      nbsubtries = 0;
#if TEST_TWO
      if ( connect (socketd, (struct sockaddr *)&addr,
		    sizeof(addr)) == -1 )
	{
	  if ( errno == EINTR )
	    {
	      struct pollfd unix_really_sucks;
	      int some_more_junk;
	      socklen_t yet_more_useless_junk;

	      have_testcase = 1;
#if VERBOSE
	      fprintf (stderr, "connect: Interrupted system call "
		       "- waiting for asynchronous completion\n");
#endif
	      unix_really_sucks.fd = socketd;
	      unix_really_sucks.events = POLLOUT;
	      while ( poll (&unix_really_sucks, 1, -1) == -1 )
		{
		  if ( errno == EINTR )
		    continue;
		  perror ("poll");
		  exit (EXIT_FAILURE);
		}
	      yet_more_useless_junk = sizeof(some_more_junk);
	      if ( getsockopt (socketd, SOL_SOCKET, SO_ERROR,
			       &some_more_junk,
			       &yet_more_useless_junk) == -1 )
		{
		  perror ("getsockopt");
		  exit (EXIT_FAILURE);
		}
	      if ( some_more_junk != 0 )
		{
		  fprintf (stderr, "(connect had been interrupted, "
			   "and now polling produced an error)\n");
		  fprintf (stderr, "connect: %s\n",
			   strerror (some_more_junk));
		  exit (EXIT_FAILURE);
		}
	    }
	  else
	    {
	      perror ("connect");
	      exit (EXIT_FAILURE);
	    }
	}
#else
      while ( connect (socketd, (struct sockaddr *)&addr,
		       sizeof(addr)) == -1 )
	{
	  if ( errno == EINTR )
	    {
	      have_testcase = 1;
	      nbsubtries++;
#if VERBOSE
	      fprintf (stderr, "connect: Interrupted system call "
		       "- retrying\n");
#endif
	      if ( GIVEUP && nbsubtries >= GIVEUP )
		{
		  fprintf (stderr, "connect: Cannot complete without "
			   "interruption - giving up\n");
		  exit (EXIT_FAILURE);
		}
	      continue;
	    }
	  if ( have_testcase )
	    fprintf (stderr, "(connect had been interrupted "
		     "and now produced an error)\n");
	  perror ("connect");
	  exit (EXIT_FAILURE);
	}
#endif
      if ( have_testcase )
	fprintf (stderr, "(connect has been interrupted "
		 "and now completed successfully)\n");
#if VERBOSE
      fprintf (stderr, "connect: Success\n");
#endif
      while ( close (socketd) == -1 )
	{
	  if ( errno == EINTR )
	    continue;
	  perror ("close");
	  exit (EXIT_FAILURE);
	}
#if VERBOSE
      fprintf (stderr, "close: Success\n");
#endif
      nbtries++;
      if ( GIVEUP && nbtries >= GIVEUP )
	{
	  fprintf (stderr, "connect: Never interrupted "
		   "- giving up\n");
	  exit (EXIT_FAILURE);
	}
    }
}

int
main (void)
{
  fprintf (stderr, "Will try to connect to %s on port %d\n",
	   CONNECT_ADDRESS, CONNECT_PORT);
  if ( 1 )
    {
      struct sigaction catchsig;

      memset (&catchsig, 0, sizeof(catchsig));
      catchsig.sa_handler = ignore_handler;
      if ( sigaction (SIGUSR1, &catchsig, NULL) == -1 )
	{
	  perror ("sigaction");
	  exit (EXIT_FAILURE);
	}
    }
  child = fork ();
  if ( child == -1 )
    {
      perror ("fork");
      exit (EXIT_FAILURE);
    }
  if ( child )
    {
      struct sigaction childsig;

      memset (&childsig, 0, sizeof(childsig));
      childsig.sa_handler = terminate_handler;
      if ( sigaction (SIGCHLD, &childsig, NULL) == -1 )
	{
	  perror ("sigaction");
	  kill (child, SIGTERM);
	  exit (EXIT_FAILURE);
	}
      killing_loop ();
    }
  else
    child_loop ();
  exit (EXIT_SUCCESS);
  return 0;
}
