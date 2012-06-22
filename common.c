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
			ti->name = strdup(s);
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
		pabort("open stat");

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

void put_task_info(struct task_info *ti)
{
	free(ti->name);;
}
