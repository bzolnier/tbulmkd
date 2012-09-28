/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>
 *
 * heavily based on Userspace low memory killer daemon:
 * Copyright 2012  Linaro Limited
 * Author: Anton Vorontsov <anton.vorontsov@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "common.h"

#define PFX "tbulmkd: "

void pabort(const char *s)
{
	perror(s);
	abort();
}

void print_timestamp(void)
{
	int ret;
	struct timespec ts;

	ret = clock_gettime(CLOCK_REALTIME, &ts);
	if (ret)
		pabort("clock_gettime");
	printf(PFX "[%ld.%.9ld] ", ts.tv_sec, ts.tv_nsec);
}

/**
 *	parse_stat - parse /proc/$pid/stat information
 *	@s: stat string
 *	@ti: task info instance
 *
 *	Parse @s stat string extracting task name, TTY number
 *	and RSS value in pages (stat entries 1, 6 and 23) and
 *	storing them in @ti task info instance.
 */
static void parse_stat(char *s, struct task_info *ti)
{
	char *_s = s;
	int i = 0;

	while (1) {
		s = strtok(_s, " ");
		if (!s)
			break;
		switch (i) {
		case 1:
			ti->name = strdup(s + 1);
			ti->name[strlen(s) - 2] = '\0';
			break;
		case 6:
			ti->tty_nr = atoi(s);
			break;
		case 23:
			ti->rss = atoi(s);
			return;
		}
		_s = NULL;
		i++;
	}
	pabort("/proc/pid/stat parse");
}

/**
 *	get_task_info_stat - get task information from /proc/$pid/stat
 *	@pid: task PID number
 *	@dname: task PID string
 *	@ti: task info instance
 *
 *	Get task information (task name, TTY number and RSS value in bytes)
 *	from /proc/$pid/stat using either @pid task PID number or @dname
 *	task PID string and store it in @ti task info instance.
 *
 *	Returns 0 on success, EBADF on failure.
 */
int get_task_info_stat(pid_t pid, const char *dname, struct task_info *ti)
{
	int stat_fd;
	ssize_t sz;
	char buf[4096];
	char *name = buf;
	char *t;

	t = buf;
	t = stpcpy(t, "/proc/");
	if (pid)
		t += sprintf(t, "%d", pid);
	else
		t = stpcpy(t, dname);
	t = stpcpy(t, "/stat");

	stat_fd = open(name, O_RDONLY);
	if (stat_fd < 0)
//		pabort("open stat");
		return EBADF;

	sz = read(stat_fd, buf, sizeof(buf));
	if (sz <= 0) {
		close(stat_fd);
		return EBADF;
	}
//		pabort("read stat");

	parse_stat(buf, ti);
	ti->rss = ti->rss * sysconf(_SC_PAGESIZE);

	close(stat_fd);

	return 0;
}

/**
 *	get_task_info - get task information
 *	@pid: task PID number
 *	@dname: task PID string
 *	@ti: task info instance
 *
 *	Get task information (task name, TTY number and RSS value in bytes)
 *	from /proc/$pid/stat using either @pid task PID number or @dname
 *	task PID string and store it in @ti task info instance. Also get
 *	information about task activity from /proc/$pid/activity and time
 *	of last activity change from /proc/$pid/activity_time.
 *
 *	Returns 0 on success, EBADF on failure.
 */
int get_task_info(pid_t pid, const char *dname, struct task_info *ti)
{
	int activity_time_fd;
	int activity_fd;
	int stat_fd;
	ssize_t sz;
	char buf[4096];
	char *name = buf;
	char *t;
	char *pid_dir_end;

	t = buf;
	t = stpcpy(t, "/proc/");
	if (pid)
		t += sprintf(t, "%d", pid);
	else
		t = stpcpy(t, dname);
	pid_dir_end = t;
	t = stpcpy(t, "/activity_time");

	activity_time_fd = open(name, O_RDONLY);
	if (activity_time_fd < 0)
		return EBADF;

	t = pid_dir_end;
	t = stpcpy(t, "/activity");

	activity_fd = open(name, O_RDONLY);
	if (activity_fd < 0)
		pabort("open activity");

	t = pid_dir_end;
	t = stpcpy(t, "/stat");
	stat_fd = open(name, O_RDONLY);
	if (stat_fd < 0)
		pabort("open stat");

	sz = read(activity_time_fd, buf, sizeof(buf));
	if (sz <= 0)
		pabort("read activity_time");

	ti->time = atoi(buf);

	sz = read(activity_fd, buf, sizeof(buf));
	if (sz <= 0)
		pabort("read activity");

	ti->activity = atoi(buf);

	sz = read(stat_fd, buf, sizeof(buf));
	if (sz <= 0)
		pabort("read stat");

	parse_stat(buf, ti);
	ti->rss = ti->rss * sysconf(_SC_PAGESIZE);

	close(stat_fd);
	close(activity_fd);
	close(activity_time_fd);

	return 0;
}

/**
 *	put_task_info - put task information
 *	@ti: task info instance
 *
 *	Free memory used for task name.
 */
void put_task_info(struct task_info *ti)
{
	free(ti->name);;
}
