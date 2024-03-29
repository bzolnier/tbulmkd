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

#ifndef __TBULMKD_H
#define __TBULMKD_H

struct mem_threshold {
	long long mem_limit;
	int mfd;
	int cfd;
	int efd;
};

extern struct mem_threshold mem_thresholds[2];

struct pollfd;

int apps_mem_percent;
int daemons_mem_percent;

void free_cgroups(void);
void init_cgroups(void);
void add_pid_to_daemons_cgroup(pid_t pid);
void add_pid_to_apps_cgroup(pid_t pid);

int setup_events(struct pollfd *pollfds, int idx);
void cleanup_events(int idx);
void process_event(int idx);
int check_pid_in_cgroup(pid_t pid, int idx);
long long get_mem_usage(int idx);

#endif
