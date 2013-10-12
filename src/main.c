/*
Copyright (C) 2012  Gregory Nietsky <gregory@distrotetch.co.za>
        http://www.distrotech.co.za

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/** @file
  * @brief Application framework
  * @ingroup LIB
  * @addtogroup LIB
  * @{*/

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>

#include "include/dtsapp.h"
#include "include/private.h"

static struct framework_core *framework_core_info = NULL;

#ifndef __WIN32__
/*
 * handle signals to cleanup gracefully on exit
 */
static void framework_sig_handler(int sig, siginfo_t *si, void *unused) {
	/* flag and clean all threads*/
	switch (sig) {
		case SIGUSR1:
		case SIGUSR2:
		case SIGHUP:
		case SIGALRM:
			if (!thread_signal(sig) && framework_core_info->sig_handler) {
				framework_core_info->sig_handler(sig, si, unused);
			}
			break;
		case SIGTERM:
		case SIGINT:
			stopthreads();
			/* no break */
		default
				:
			if (framework_core_info->sig_handler) {
				framework_core_info->sig_handler(sig, si, unused);
			} else {
				exit(-1);
			}
			/* no break */
	}
}
#endif

/** @brief Print a brief GNU copyright notice on console.
  * 
  * framework_mkcore() needs to be run to ininitilise the data
  * @see FRAMEWORK_MAIN()
  * @see framework_mkcore()*/
extern void printgnu() {
	struct framework_core *ci;
	if (framework_core_info && objref(framework_core_info)) {
		ci = framework_core_info;
	} else {
		return;
	}
	printf("%s\n\nCopyright (C) %i %s <%s>\n"
		   "        %s\n\n"
		   "    This program comes with ABSOLUTELY NO WARRANTY\n"
		   "    This is free software, and you are welcome to redistribute it\n"
		   "    under certain condition\n\n", ci->progname, ci->year, ci->developer, ci->email, ci->www);
	objunref(ci);
}

/** @brief Daemonise the application using fork/exit
  * 
  * This should be run early before file descriptors and threads are started
  * @see FRAMEWORK_MAIN()
  * @todo WIN32 options is there a alternative for this.
  * @return Application PID*/
extern void daemonize() {
#ifndef __WIN32__
	pid_t	forkpid;

	/* fork and die daemonize*/
	forkpid = fork();
	if (forkpid > 0) {
		/* im all grown up and can pass onto child*/
		exit(0);
	} else if (forkpid < 0) {
		/* could not fork*/
		exit(-1);
	}

	/* Dont want these as a daemon*/
	signal(SIGTSTP, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);
#endif
}

/** @brief Lock the run file in the framework application info.
  *
  * @todo make this more generic to allow passing file as param and not storing it in the struct.
  * @return 0 if no file is specified or not supported the file descriptor on success.*/
extern int lockpidfile(const char *runfile) {
	int lck_fd = 0;
#ifndef __WIN32__
	char pidstr[12];
	pid_t	mypid;

	mypid = getpid();
	sprintf(pidstr,"%i\n", (int)mypid);
	if (runfile && ((lck_fd = open(runfile, O_RDWR|O_CREAT, 0640)) > 0) && (!flock(lck_fd, LOCK_EX | LOCK_NB))) {
		if (write(lck_fd, pidstr, strlen(pidstr)) < 0) {
			close(lck_fd);
			lck_fd = -1;
		}
	/* file was opened and not locked*/
	} else if (runfile && lck_fd) {
		close(lck_fd);
		lck_fd = -1;
	}
#endif
	return (lck_fd);
}


#ifndef __WIN32__
/*
 * set up signal handler
 */
static void configure_sigact(struct sigaction *sa) {
	sa->sa_flags = SA_SIGINFO | SA_RESTART;
	sigemptyset(&sa->sa_mask);
	sa->sa_sigaction = framework_sig_handler;
	sigaction(SIGINT, sa, NULL);
	sigaction(SIGTERM, sa, NULL);

	/*internal interupts*/
	sigaction(SIGUSR1, sa, NULL);
	sigaction(SIGUSR2, sa, NULL);
	sigaction(SIGHUP, sa, NULL);
	sigaction(SIGALRM, sa, NULL);
}
#endif

/*
 * free core
 */
static void framework_free(void *data) {
	struct framework_core *ci = data;
	framework_core_info = NULL;

	if (ci->developer) {
		free((char *)ci->developer);
	}
	if (ci->email) {
		free((char *)ci->email);
	}
	if (ci->www) {
		free((char *)ci->www);
	}
	if (ci->sa) {
		free(ci->sa);
	}
	if (ci->flock >= 0) {
		close(ci->flock);
	}
	if (ci->runfile) {
		if (ci->flock >= 0) {
			unlink(ci->runfile);
		}
		free((char *)ci->runfile);
	}
}

/** @brief Initilise application data structure and return a reference
  * @note The returned value must be un referenced
  * @warning failure to supply a signal handler on non WIN32 systems
  * will deafault to exiting with -1 on SIGINT/SIGKILL.
  * @todo does threads actually work in windows with no sighandler.
  * @warning do not call this function without calling framework_init as the memory
  * allocated will not be freed.
  * @param progname Descrioptive program name.
  * @param name Copyright holder.
  * @param email Copyright email address.
  * @param web Website address.
  * @param year Copyright year.
  * @param runfile Run file that will store the pid and be locked (flock).
  * @param flags Application flags.
  * @param sigfunc Signal handler.*/
extern void framework_mkcore(char *progname, char *name, char *email, char *web, int year, char *runfile, int flags, syssighandler sigfunc) {
	struct framework_core *core_info;
	if (framework_core_info) {
		objunref(framework_core_info);
		framework_core_info = NULL;
	}

	if (!(core_info = objalloc(sizeof(*core_info), framework_free))) {
		return;
	}

#ifndef __WIN32__
	if (core_info && !(core_info->sa = malloc(sizeof(*core_info->sa)))) {
		free(core_info);
		return;
	}
#endif

	ALLOC_CONST(core_info->developer, name);
	ALLOC_CONST(core_info->email, email);
	ALLOC_CONST(core_info->www, web);
	ALLOC_CONST(core_info->runfile, runfile);
	ALLOC_CONST(core_info->progname, progname);
	core_info->year = year;
	core_info->flags = flags;
#ifndef __WIN32__
	core_info->sig_handler = sigfunc;
#endif
	/* Pass reference to static system variable*/
	framework_core_info = core_info;
}

/** @brief Initilise the application daemonise and join the manager thread.
  * @warning failure to pass a callback will require running stopthreads
  * and jointhrea..
  * @warning framework information configured by framework_mkcore will be freed on exit.
  * @param argc Argument count argv[0] will be program name.
  * @param argv Argument array.
  * @param callback Function to pass control too.*/
extern int framework_init(int argc, char *argv[], frameworkfunc callback) {
	int ret = 0;

	seedrand();
	sslstartup();

	/*prinit out a GNU licence summary*/
	if (!(framework_core_info->flags & FRAMEWORK_FLAG_NOGNU)) {
		printgnu();
	}

	/* fork the process to daemonize it*/
	if (framework_core_info->flags & FRAMEWORK_FLAG_DAEMON) {
		daemonize();
	}

	if ((framework_core_info->flock = lockpidfile(framework_core_info->runfile) < 0)) {
		printf("Could not lock pid file Exiting\n");
		objunref(framework_core_info);
		return (-1);
	}

#ifndef __WIN32__
	/* interupt handler close clean on term so physical is reset*/
	configure_sigact(framework_core_info->sa);
#endif

	/*init the threadlist start thread manager*/
	if (!(framework_core_info->flags & FRAMEWORK_FLAG_NOTHREAD) && !startthreads()) {
		printf("Memory Error could not start threads\n");
		objunref(framework_core_info);
		return (-1);
	}

	/*run the code from the application*/
	if (callback) {
		ret = callback(argc, argv);
		/* wait for all threads to end*/
		usleep(100000);
		stopthreads();
		jointhreads();
	}

	/* turn off the lights*/
	objunref(framework_core_info);
	return (ret);
}

/** @}*/
