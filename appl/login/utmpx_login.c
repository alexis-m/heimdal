/* Author: Wietse Venema <wietse@wzv.win.tue.nl> */

#include "bsd_locl.h"

RCSID("$Id$");

/* utmpx_login - update utmp and wtmp after login */

#ifndef HAVE_UTMPX_H
int utmpx_login(char *line, char *user, char *host) { return 0; }
#else

static
void
utmpx_update(struct utmpx *ut, char *line, char *user, char *host)
{
    struct timeval tmp;

    strncpy(ut->ut_line, line, sizeof(ut->ut_line));
    strncpy(ut->ut_user, user, sizeof(ut->ut_user));
    strncpy(ut->ut_host, host, sizeof(ut->ut_host));
#ifdef HAVE_UT_SYSLEN
    ut->ut_syslen = strlen(host) + 1;
    if (ut->ut_syslen > sizeof(ut->ut_host))
        ut->ut_syslen = sizeof(ut->ut_host);
#endif
    ut->ut_type = USER_PROCESS;
    gettimeofday (&tmp, 0);
    ut->ut_tv.tv_sec = tmp.tv_sec;
    ut->ut_tv.tv_usec = tmp.tv_usec;
    pututxline(ut);
#ifdef WTMPX_FILE
    updwtmpx(WTMPX_FILE, ut);
#endif
}

int
utmpx_login(char *line, char *user, char *host)
{
    struct utmpx *ut;
    pid_t   mypid = getpid();
    int     ret = (-1);

    /*
     * SYSV4 ttymon and login use tty port names with the "/dev/" prefix
     * stripped off. Rlogind and telnetd, on the other hand, make utmpx
     * entries with device names like /dev/pts/nnn. We therefore cannot use
     * getutxline(). Return nonzero if no utmp entry was found with our own
     * process ID for a login or user process.
     */

    while ((ut = getutxent())) {
        /* Try to find a reusable entry */
	if (ut->ut_pid == mypid
	    && (   ut->ut_type == INIT_PROCESS
		|| ut->ut_type == LOGIN_PROCESS
		|| ut->ut_type == USER_PROCESS)) {
	    utmpx_update(ut, line, user, host);
	    ret = 0;
	    break;
	}
    }
    if (ret == -1) {
        /* Grow utmpx file by one record. */
        struct utmpx newut;
	memset(&newut, 0, sizeof(newut));
	newut.ut_pid = mypid;
	snprintf(newut.ut_id, sizeof(newut.ut_id), "lo%04x", (unsigned)mypid);
        utmpx_update(&newut, line, user, host);
	ret = 0;
    }
    endutxent();
    return (ret);
}
#endif /* HAVE_UTMPX_H */
