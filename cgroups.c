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
#include <poll.h>
#include "tbulmkd.h"
#include "common.h"

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

void cleanup_events(int idx)
{
	struct mem_threshold *thres = &mem_thresholds[idx];

	if (close(thres->efd))
		pabort("close eventfd");

	close(thres->cfd);
	close(thres->mfd);
}

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
