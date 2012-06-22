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
#include <sys/mman.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "common.h"
#include "shm.h"

#define PFX "tbulkmd: "

int timeout = 60; /* timeout in seconds */

static void print_usage(char *argv0)
{
	printf("Usage: %s [OPTION]...\n"
	       "\n"
	       "-t, --timeout	set timeout (in seconds)\n"
	       "-h, --help	display this help message\n"
	       "\n",
	       argv0);
}

static void parse_args(int argc, char *argv[])
{
	struct option opts[] = {
		{ "timeout",	1, NULL, 't' },
		{ "help",	0, NULL, 'h' },
	};
	int c;

	c = getopt_long(argc, argv, "t:h", opts, NULL);
	if (c < 0)
		return;

	switch (c) {
	case 't':
		timeout = atoi(optarg);
		break;
	case 'h':
		print_usage(argv[0]);
		exit(1);
		break;
	}
}

static int tasklist_fd;
static struct tasklist_mem *tasklist_mem;

void init_tasklist(void)
{
	int ret;

	tasklist_fd = shm_open("/tbulmkd_tasklist", O_RDWR, 0600);
	if (tasklist_fd < 0)
		pabort("shm_open tasklist");

	ret = ftruncate(tasklist_fd, sizeof(*tasklist_mem));
	if (ret)
		pabort("ftruncate");

	tasklist_mem = mmap(NULL, sizeof(*tasklist_mem),
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED,
			tasklist_fd, 0);
	if (tasklist_mem == MAP_FAILED)
		pabort("mmap tasklist");
}

void free_tasklist(void)
{
	munmap(tasklist_mem, sizeof(*tasklist_mem));
	close(tasklist_fd);
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

int main(int argc, char *argv[])
{
	int ret;

	ret = mlockall(MCL_FUTURE);
	if (ret)
		pabort("mlockall");

	parse_args(argc, argv);

	init_tasklist();

	while (1) {
		int i;

		sem_wait(&tasklist_mem->sem);
		for (i = 0; i < MAX_NR_TASKS; i++) {
			struct task_info_shm *tis = &tasklist_mem->tasks[i];
			struct task_info ti;
			pid_t pid;
			time_t t;

			pid = tis->pid;
			if (!pid)
				break;

			if (tis->activity)
				continue;

			t = time(NULL);
			if (t == -1)
				pabort("time");

			if (t - tis->time > timeout) {
				get_task_info(pid, NULL, &ti);
				print_timestamp();
				printf("killing %d timeout %d secs (%lu %s)\n",
				       pid, (unsigned)(t - tis->time),
				       ti.rss / 1024 / 1024, ti.name);
				put_task_info(&ti);
				kill(pid, SIGKILL);
			}
		}
		sem_post(&tasklist_mem->sem);

		sleep(1);
	};

	free_tasklist();
}
