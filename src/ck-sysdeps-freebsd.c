/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007 Joe Marcus Clarke <marcus@FreeBSD.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <paths.h>
#include <ttyent.h>
#include <kvm.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/ioctl.h>

#include <sys/consio.h>

#define DEV_ENCODE(M,m) ( \
  ( (M&0xfff) << 8) | ( (m&0xfff00) << 12) | (m&0xff) \
)

#include "ck-sysdeps.h"

#ifndef ERROR
#define ERROR -1
#endif

/* adapted from procps */
struct _CkProcessStat
{
        int pid;
        int ppid;                       /* stat,status     pid of parent process */
        char state;                     /* stat,status     single-char code for process state (S=sleeping) */
        char cmd[16];                   /* stat,status     basename of executable file in call to exec(2) */
        unsigned long long utime;       /* stat            user-mode CPU time accumulated by process */
        unsigned long long stime;       /* stat            kernel-mode CPU time accumulated by process */
        unsigned long long cutime;      /* stat            cumulative utime of process and reaped children */
        unsigned long long cstime;      /* stat            cumulative stime of process and reaped children */
        unsigned long long start_time;  /* stat            start time of process -- seconds since 1-1-70 */
        unsigned long start_code;       /* stat            address of beginning of code segment */
        unsigned long end_code;         /* stat            address of end of code segment */
        unsigned long start_stack;      /* stat            address of the bottom of stack for the process */
        unsigned long kstk_esp;         /* stat            kernel stack pointer */
        unsigned long kstk_eip;         /* stat            kernel instruction pointer */
        unsigned long wchan;            /* stat (special)  address of kernel wait channel proc is sleeping in */
        long priority;                  /* stat            kernel scheduling priority */
        long nice;                      /* stat            standard unix nice level of process */
        long rss;                       /* stat            resident set size from /proc/#/stat (pages) */
        long alarm;                     /* stat            ? */
        unsigned long rtprio;           /* stat            real-time priority */
        unsigned long sched;            /* stat            scheduling class */
        unsigned long vsize;            /* stat            number of pages of virtual memory ... */
        unsigned long rss_rlim;         /* stat            resident set size limit? */
        unsigned long flags;            /* stat            kernel flags for the process */
        unsigned long min_flt;          /* stat            number of minor page faults since process start */
        unsigned long maj_flt;          /* stat            number of major page faults since process start */
        unsigned long cmin_flt;         /* stat            cumulative min_flt of process and child processes */
        unsigned long cmaj_flt;         /* stat            cumulative maj_flt of process and child processes */
        int     pgrp;                   /* stat            process group id */
        int session;                    /* stat            session id */
        int nlwp;                       /* stat    number of threads, or 0 if no clue */
        int tty;                        /* stat            full device number of controlling terminal */
        int tpgid;                      /* stat            terminal process group id */
        int exit_signal;                /* stat            might not be SIGCHLD */
        int processor;                  /* stat            current (or most recent?) CPU */
        uintptr_t penv;                 /* stat            address of initial environment vector */
        char tty_text[16];              /* stat            device name */

};

pid_t
ck_process_stat_get_ppid (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, -1);

        return stat->ppid;
}

char *
ck_process_stat_get_cmd (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, NULL);

        return g_strdup (stat->cmd);
}

char *
ck_process_stat_get_tty (CkProcessStat *stat)
{
        g_return_val_if_fail (stat != NULL, NULL);

        return g_strdup (stat->tty_text);
}

static gboolean
get_kinfo_proc (pid_t pid,
                struct kinfo_proc *p)
{
        int    mib[4];
        size_t len;

        len = 4;
        sysctlnametomib ("kern.proc.pid", mib, &len);

        len = sizeof(struct kinfo_proc);
        mib[3] = pid;

        if (sysctl (mib, 4, p, &len, NULL, 0) == -1) {
                return FALSE;
        }

        return TRUE;
}

/* return 1 if it works, or 0 for failure */
static gboolean
stat2proc (pid_t        pid,
           CkProcessStat *P)
{
        struct kinfo_proc p;
        char              *ttname;
        int               num;
        int               tty_maj;
        int               tty_min;

        if (! get_kinfo_proc (pid, &p)) {
                return FALSE;
        }

        num = OCOMMLEN;
        if (num >= sizeof P->cmd) {
                num = sizeof P->cmd - 1;
        }

        memcpy (P->cmd, p.ki_ocomm, num);

        P->cmd[num]   = '\0';
        P->pid        = p.ki_pid;
        P->ppid       = p.ki_ppid;
        P->pgrp       = p.ki_pgid;
        P->session    = p.ki_sid;
        P->rss        = p.ki_rssize;
        P->vsize      = p.ki_size;
        P->start_time = p.ki_start.tv_sec;
        P->wchan      = (unsigned long) p.ki_wchan;
        P->state      = p.ki_stat;
        P->nice       = p.ki_nice;
        P->flags      = p.ki_sflag;
        P->tpgid      = p.ki_tpgid;
        P->processor  = p.ki_oncpu;
        P->nlwp       = p.ki_numthreads;

        /* we like it Linux-encoded :-) */
        tty_maj = major (p.ki_tdev);
        tty_min = minor (p.ki_tdev);
        P->tty = DEV_ENCODE (tty_maj,tty_min);

        snprintf (P->tty_text, sizeof P->tty_text, "%3d,%-3d", tty_maj, tty_min);

        if (p.ki_tdev != NODEV && (ttname = devname (p.ki_tdev, S_IFCHR)) != NULL) {
                memcpy (P->tty_text, ttname, sizeof P->tty_text);
        }

        if (p.ki_tdev == NODEV) {
                memcpy (P->tty_text, "   ?   ", sizeof P->tty_text);
        }

        if (P->pid != pid) {
                return FALSE;
        }

        return TRUE;
}

gboolean
ck_process_stat_new_for_unix_pid (pid_t           pid,
                                  CkProcessStat **stat,
                                  GError        **error)
{
        gboolean       res;
        GError        *local_error;
        CkProcessStat *proc;

        g_return_val_if_fail (pid > 1, FALSE);

        if (stat == NULL) {
                return FALSE;
        }

        proc = g_new0 (CkProcessStat, 1);
        proc->pid = pid;
        res = stat2proc (pid, proc);
        if (res) {
                *stat = proc;
        } else {
                g_propagate_error (error, local_error);
                *stat = NULL;
        }

        return res;
}

void
ck_process_stat_free (CkProcessStat *stat)
{
        g_free (stat);
}

GHashTable *
ck_unix_pid_get_env_hash (pid_t pid)
{
        GHashTable       *hash;
        char            **penv;
        kvm_t            *kd;
        struct kinfo_proc p;
        int               i;

        kd = kvm_openfiles (_PATH_DEVNULL, _PATH_DEVNULL, NULL, O_RDONLY, NULL);
        if (kd == NULL) {
                return NULL;
        }

        if (! get_kinfo_proc (pid, &p)) {
                return NULL;
        }

        penv = kvm_getenvv (kd, &p, 0);
        if (penv == NULL) {
                return NULL;
        }

        hash = g_hash_table_new_full (g_str_hash,
                                      g_str_equal,
                                      g_free,
                                      g_free);

        for (i = 0; penv[i] != NULL; i++) {
                char **vals;

                vals = g_strsplit (penv[i], "=", 2);
                if (vals != NULL) {
                        g_hash_table_insert (hash,
                                             g_strdup (vals[0]),
                                             g_strdup (vals[1]));
                        g_strfreev (vals);
                }
        }

        kvm_close (kd);

        return hash;
}

char *
ck_unix_pid_get_env (pid_t       pid,
                     const char *var)
{
        GHashTable *hash;
        char       *val;

        /*
         * Would probably be more efficient to just loop through the
         * environment and return the value, avoiding building the hash
         * table, but this works for now.
         */
        hash = ck_unix_pid_get_env_hash (pid);
        val  = g_strdup (g_hash_table_lookup (hash, var));
        g_hash_table_destroy (hash);

        return val;
}

uid_t
ck_unix_pid_get_uid (pid_t pid)
{
        uid_t             uid;
        gboolean          res;
        struct kinfo_proc p;

        g_return_val_if_fail (pid > 1, 0);

        uid = -1;

        res = get_kinfo_proc (pid, &p);

        if (res) {
                uid = p.ki_uid;
        }

        return uid;
}

gboolean
ck_unix_pid_get_login_session_id (pid_t  pid,
                                  char **idp)
{
        g_return_val_if_fail (pid > 1, FALSE);

        return FALSE;
}

gboolean
ck_get_max_num_consoles (guint *num)
{
        int      max_consoles;
        int      res;
        gboolean ret;
        struct ttyent *t;

        ret = FALSE;
        max_consoles = 0;

        res = setttyent ();
        if (res == 0) {
                goto done;
        }

        while ((t = getttyent ()) != NULL) {
                if (t->ty_status & TTY_ON && strncmp (t->ty_name, "ttyv", 4) == 0)
                        max_consoles++;
        }

        /* Increment one more so that all consoles are properly counted
         * this is arguable a bug in vt_add_watches().
         */
        max_consoles++;

        ret = TRUE;

        endttyent ();

done:
        if (num != NULL) {
                *num = max_consoles;
        }

        return ret;
}

gboolean
ck_supports_activatable_consoles (void)
{
        return TRUE;
}

char *
ck_get_console_device_for_num (guint num)
{
        char *device;

        /* The device number is always one less than the VT number. */
        num--;

        device = g_strdup_printf ("/dev/ttyv%u", num);

        return device;
}

gboolean
ck_get_console_num_from_device (const char *device,
                                guint      *num)
{
        guint    n;
        gboolean ret;

        n = 0;
        ret = FALSE;

        if (device == NULL) {
                return FALSE;
        }

        if (sscanf (device, "/dev/ttyv%u", &n) == 1) {
                /* The VT number is always one more than the device number. */
                n++;
                ret = TRUE;
        }

        if (num != NULL) {
                *num = n;
        }

        return ret;
}

gboolean
ck_get_active_console_num (int    console_fd,
                           guint *num)
{
        gboolean ret;
        int      res;
        int      active;

        g_assert (console_fd != -1);

        active = 0;
        ret = FALSE;

        res = ioctl (console_fd, VT_GETACTIVE, &active);
        if (res == ERROR) {
                perror ("ioctl VT_GETACTIVE");
                goto out;
        }

        g_debug ("Active VT is: %d (ttyv%d)", active, active - 1);
        ret = TRUE;

 out:
        if (num != NULL) {
                *num = active;
        }

        return ret;
}
