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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <string.h>
#include <poll.h>
#include "tbulmkd.h"
#include "common.h"

void free_cgroup(void)
{
	rmdir("/sys/fs/cgroup/memory/apps");
	rmdir("/sys/fs/cgroup/memory/daemons");
	umount("/sys/fs/cgroup/memory");
	rmdir("/sys/fs/cgroup/memory");
	umount("/sys/fs/cgroup");
}

void init_cgroups(void)
{
	FILE *f;
	char buf[4096];
	unsigned long int memtotal;
	int i;
	float t;

	f = fopen("/proc/meminfo", "r");
	if (!f)
		pabort("fopen /proc/meminfo");

	while (fgets(buf, sizeof(buf), f)) {
		if (strstr(buf, "MemTotal")) {
			if (sscanf(buf, "MemTotal: %lu kB", &memtotal) != 1) {
				fclose(f);
				return;
			} else {
				memtotal *= 1024;
				break;
			}
		}
	}

	fclose(f);

	if (DEBUG)
		printf("memtotal: %lu\n", memtotal);

	free_cgroup();

	/* mount -t tmpfs none /sys/fs/cgroup */
	if (mount(NULL, "/sys/fs/cgroup", "tmpfs", 0, NULL))
		pabort("mount /sys/fs/cgroup");

	/* mkdir /sys/fs/cgroup/memory */
	if (mkdir("/sys/fs/cgroup/memory", 755))
		pabort("mkdir /sys/fs/cgroup/memory");

	/* mount -t cgroup none /sys/fs/cgroup/memory -o memory */
	if (mount(NULL, "/sys/fs/cgroup/memory", "cgroup", 0, "memory"))
		pabort("mount /sys/fs/cgroup/memory");

	/* mkdir /sys/fs/cgroup/memory/daemons */
	mkdir("/sys/fs/cgroup/memory/daemons", 755);
//		pabort("mkdir /sys/fs/cgroup/memory/daemons");

	/* echo 80%*MemTotal > /sys/fs/cgroup/memory/daemons/memory.limit_in_bytes */
	f = fopen("/sys/fs/cgroup/memory/daemons/memory.limit_in_bytes", "w");
	if (!f)
		pabort("fopen /sys/fs/cgroup/memory/daemons/memory.limit_in_bytes");

	t = (float)daemons_mem_percent / 100 * memtotal;
	i = sprintf(buf, "%lu", (unsigned long int)t);
	if (DEBUG)
		printf("daemons limit: %s\n", buf);
	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite daemons\n");

	fclose(f);

	/* mkdir /sys/fs/cgroup/memory/apps */
	mkdir("/sys/fs/cgroup/memory/apps", 755);
//		pabort("mkdir /sys/fs/cgroup/memory/apps");

	/* echo 80%*MemTotal > /sys/fs/cgroup/memory/apps/memory.limit_in_bytes */
	f = fopen("/sys/fs/cgroup/memory/apps/memory.limit_in_bytes", "w");
	if (!f)
		pabort("fopen /sys/fs/cgroup/memory/apps/memory.limit_in_bytes");

	t = (float)apps_mem_percent / 100 * memtotal;
	i = sprintf(buf, "%lu", (unsigned long int)t);
	// debug
//	i = sprintf(buf, "%lu", 100000000UL);
	if (DEBUG)
		printf("apps limit: %s\n", buf);
	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite apps\n");

	fclose(f);

	/* disable kernel OOM killer */
	i = sprintf(buf, "1");

	f = fopen("/sys/fs/cgroup/memory/daemons/memory.oom_control", "w");
	if (!f)
		pabort("fopen /sys/fs/cgroup/memory/daemons/memory.oom_control");

	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite daemons/memory.oom_control\n");

	fclose(f);

	f = fopen("/sys/fs/cgroup/memory/apps/memory.oom_control", "w");
	if (!f)
		pabort("fopen /sys/fs/cgroup/memory/apps/memory.oom_control");

	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite apps/memory.oom_control\n");

	fclose(f);
}

void add_pid_to_daemons_cgroup(pid_t pid)
{
	FILE *f;
	char buf[4096];
	int i;

	f = fopen("/sys/fs/cgroup/memory/daemons/tasks", "w");
	if (!f)
		pabort("fopen /sys/fs/cgroup/memory/deamons/tasks");

	i = sprintf(buf, "%u", (unsigned int)pid);
	if (DEBUG)
		printf("adding pid %u to daemons cgroup\n", pid);
	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite daemons tasks\n");

	fclose(f);
}

void add_pid_to_apps_cgroup(pid_t pid)
{
	FILE *f;
	char buf[4096];
	int i;

	f = fopen("/sys/fs/cgroup/memory/apps/tasks", "w");
	if (!f)
		pabort("fopen /sys/fs/cgroup/memory/apps/tasks");

	i = sprintf(buf, "%u", (unsigned int)pid);
	if (DEBUG)
		printf("adding pid %u to apps cgroup\n", pid);
	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite apps tasks\n");

	fclose(f);
}

static char *cg_class[] = { "daemons", "apps" };

static long long get_mem_limit(int idx)
{
	char buf[100];
	int mfd;
	int i;
	long long thresb;

	i = sprintf(buf, "/sys/fs/cgroup/memory/%s/memory.limit_in_bytes",
		    cg_class[idx]);
	mfd = open(buf, O_RDONLY);
	if (mfd < 0)
		pabort("open limit_in_bytes");

	i = read(mfd, buf, sizeof(buf));
	if (i <= 0)
		pabort("read limit_in_bytes");

	thresb = strtoll(buf, NULL, 10);
	if (DEBUG)
		printf("%s: limit_in_bytes=%lld\n", cg_class[idx], thresb);

	close(mfd);

	return thresb;
}

long long get_mem_usage(int idx)
{
	char buf[100];
	int mfd;
	int i;
	long long thresb;

	i = sprintf(buf, "/sys/fs/cgroup/memory/%s/memory.usage_in_bytes",
		    cg_class[idx]);
	mfd = open(buf, O_RDONLY);
	if (mfd < 0)
		pabort("open usage_in_bytes");

	i = read(mfd, buf, sizeof(buf));
	if (i <= 0)
		pabort("read usage_in_bytes");

	thresb = strtoll(buf, NULL, 10);
	if (DEBUG)
		printf("%s: usage_in_bytes=%lld\n", cg_class[idx], thresb);

	close(mfd);

	return thresb;
}

/**
 *	setup_events - setup eventfd event
 *	@pollfds: pollfd instance
 *	@idx: task type index
 *
 *	Setups eventfd event for crossing mem_thresholds[@idx]
 *	memory threshold (which is setup to memory.limit_in_bytes
 *	minus 6 MiB) by memory.usage_in_bytes.
 */
int setup_events(struct pollfd *pollfds, int idx)
{
	struct mem_threshold *thres = &mem_thresholds[idx];
	char buf[100];
	char *ctl;
	int mfd, cfd, efd;
	long long thresb;
	int ret;
	ssize_t sz;
	int i;

	thresb = thres->mem_limit = get_mem_limit(idx) - (6 << 20);

	i = sprintf(buf, "/sys/fs/cgroup/memory/%s/memory.usage_in_bytes",
		    cg_class[idx]);
	mfd = open(buf, O_RDONLY);
	if (mfd < 0)
		pabort("open usage_in_bytes");

	i = sprintf(buf, "/sys/fs/cgroup/memory/%s/cgroup.event_control",
		    cg_class[idx]);
	cfd = open(buf, O_WRONLY);
	if (cfd < 0)
		pabort("open event_control");

//	efd = eventfd(0, EFD_NONBLOCK);
	efd = eventfd(0, 0);
	if (efd < 0)
		pabort("event fd");

	i = fcntl(efd, F_SETFL, O_NONBLOCK);
	if (i)
		pabort("fcntl fd");

	sz = asprintf(&ctl, "%d %d %lld", efd, mfd, thresb);
	if (sz < 0)
		pabort("asprintf ctl");
	sz += 1;

	ret = write(cfd, ctl, sz);
	if (ret != sz)
		pabort("write cfd");

	if (DEBUG)
		printf("registered event %s\n", ctl);

	thres->mfd = mfd;
	thres->cfd = cfd;
	thres->efd = efd;

	pollfds[idx].fd = efd;
	pollfds[idx].events = POLLIN;

	free(ctl);

	return 0;
}

/**
 *	cleanup_events - cleanup eventfd event
 *	@idx: task type index
 *
 *	Cleanups eventfd event setup by setup_events().
 */
void cleanup_events(int idx)
{
	struct mem_threshold *thres = &mem_thresholds[idx];

	if (close(thres->efd))
		pabort("close eventfd");

	close(thres->cfd);
	close(thres->mfd);
}

/**
 *	process_events - process eventfd event
 *	@idx: task type index
 *
 *	Processes eventfd event setup by setup_events().
 *	In practice it just reads mem_thresholds[idx].efd
 *	file descriptor.
 */
void process_event(int idx)
{
	struct mem_threshold *thres = &mem_thresholds[idx];
	uint64_t result;
	int ret;

	ret = read(thres->efd, &result, sizeof(result));
	if (ret < 0)
		pabort("read efd");

	if (DEBUG)
		printf("%s: res %lld B\n", cg_class[idx],
			thres->mem_limit);
}
