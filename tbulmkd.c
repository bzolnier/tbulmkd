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
#include <errno.h>
#include <sys/mount.h>
#include "common.h"
#include "shm.h"

#define PFX "tbulkmd: "

static int timeout = 60; /* timeout in seconds */
static int use_cgroups = 0;

static void print_usage(char *argv0)
{
	printf("Usage: %s [OPTION]...\n"
	       "\n"
	       "-c, --cgroups	use control groups memory controller\n"
	       "-t, --timeout	set timeout (in seconds)\n"
	       "-h, --help	display this help message\n"
	       "\n",
	       argv0);
}

static void parse_args(int argc, char *argv[])
{
	struct option opts[] = {
		{ "cgroups",	0, NULL, 'c' },
		{ "timeout",	1, NULL, 't' },
		{ "help",	0, NULL, 'h' },
	};
	int c;

	c = getopt_long(argc, argv, "t:hc", opts, NULL);
	if (c < 0)
		return;

	switch (c) {
	case 'c':
		use_cgroups = 1;
		printf("using control groups memory controller\n");
		break;
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

static char *config_file = "tbulmkd.cfg";

static char *exemption_list[MAX_NR_TASKS];
static int exemption_list_len;

#define MAX_TASK_NAME 100

static void init_config_file(void)
{
	FILE *f;
	char buf[4096];
	int i = 0;

	f = fopen(config_file, "r");
	if (!f) {
		int errsv = errno;
		printf("fopen(%s) errno=%d\n", config_file, errsv);
		return;
	}

	while (fgets(buf, sizeof(buf), f)) {
		char *s = buf, es[MAX_TASK_NAME];
		int j;

		if (*s == '#')
			continue;

		j = sscanf(s, "exemption %s", es);
		if (j != 1)
			continue;

		exemption_list[i++] = strdup(es);
	}

	exemption_list_len = i;

	fclose(f);
}

static void print_exemption_list(void)
{
	int i;

	printf("Exemption list:\n");

	for (i = 0; i < exemption_list_len; i++) {
		printf("%s\n", exemption_list[i]);
	}

	printf("\n");
}

static void free_config_file(void)
{
	int i;

	for (i = 0; i < exemption_list_len; i++)
		free(exemption_list[i]);

	exemption_list_len = 0;
}

static void free_cgroup(void)
{
	rmdir("/sys/fs/cgroup/memory/apps");
	rmdir("/sys/fs/cgroup/memory/daemons");
	umount("/sys/fs/cgroup/memory");
	rmdir("/sys/fs/cgroup/memory");
	umount("/sys/fs/cgroup");
}

static void init_cgroups(void)
{
	FILE *f;
	char buf[4096];
	unsigned long int memtotal;
	int i;

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

	i = sprintf(buf, "%lu", (unsigned long int)(0.8 * memtotal));
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

	i = sprintf(buf, "%lu", (unsigned long int)(0.8 * memtotal));
	if (DEBUG)
		printf("apps limit: %s\n\n", buf);
	if (fwrite(buf, i, 1, f) != 1)
		pabort("fwrite apps\n");

	fclose(f);
}

static void add_pid_to_daemons_cgroup(pid_t pid)
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

static void add_pid_to_apps_cgroup(pid_t pid)
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

#define MAX_LIVE_BG_TASKS 6

struct bg_task {
	pid_t pid;
	time_t time;
};

static struct bg_task live_bg_tasks[MAX_LIVE_BG_TASKS];

static void print_bg_tasks(void)
{
	int i;

	printf("Live background tasks:\n");

	for (i = 0; i < MAX_LIVE_BG_TASKS; i++) {
		struct bg_task *bt = &live_bg_tasks[i];

		printf("%d pid %d time %u\n", i, bt->pid, (unsigned)bt->time);
	}

	printf("\n");
}

int main(int argc, char *argv[])
{
	int ret;

	init_config_file();
	if (DEBUG)
		print_exemption_list();

	if (use_cgroups)
		init_cgroups();

	ret = mlockall(MCL_FUTURE);
	if (ret)
		pabort("mlockall");

	parse_args(argc, argv);

	init_tasklist();

	while (1) {
		int i, j;

		sem_wait(&tasklist_mem->sem);

		memset(live_bg_tasks, 0, sizeof(struct bg_task) * MAX_LIVE_BG_TASKS);

		for (i = 0; i < MAX_NR_TASKS; i++) {
			struct task_info_shm *tis = &tasklist_mem->tasks[i];
			pid_t pid;

			pid = tis->pid;
			if (!pid)
				break;

			for (j = 0; j < MAX_LIVE_BG_TASKS; j++) {
				struct bg_task *bt = &live_bg_tasks[j];
				int k;

				if (tis->activity)
					continue;

				if (tis->time <= bt->time)
					continue;

				for (k = MAX_LIVE_BG_TASKS - 1; k > j; k--) {
					live_bg_tasks[k].time =
						live_bg_tasks[k - 1].time;
					live_bg_tasks[k].pid =
						live_bg_tasks[k - 1].pid;
				}

				bt->time = tis->time;
				bt->pid = tis->pid;
				break;
			}
		}

		if (DEBUG)
			print_bg_tasks();

		for (i = 0; i < MAX_NR_TASKS; i++) {
			struct task_info_shm *tis;
			struct task_info ti;
			pid_t pid;
			time_t t;
next_task:
			tis = &tasklist_mem->tasks[i];
			pid = tis->pid;
			if (!pid)
				break;

			if (use_cgroups) {
				/*
				 * TODO: this is just an approximation and should
				 *       be accompanied by a list of exemptions..
				 */
				if (tis->tty_nr)
					add_pid_to_apps_cgroup(pid);
				else
					add_pid_to_daemons_cgroup(pid);
			}

			if (tis->activity)
				continue;

			for (j = 0; j < MAX_LIVE_BG_TASKS; j++) {
				struct bg_task *bt = &live_bg_tasks[j];

				if (pid == bt->pid) {
					if (DEBUG) {
						print_timestamp();
						printf("skipping live pid %d\n",
						       pid);
					}
					i++;
					goto next_task;
				}
			}

			t = time(NULL);
			if (t == -1)
				pabort("time");

			if (t - tis->time <= timeout)
				continue;

			get_task_info(pid, NULL, &ti);

			for (j = 0; j < exemption_list_len; j++) {
				if (!strcmp(exemption_list[j], ti.name)) {
					if (DEBUG) {
						print_timestamp();
						printf("skipping exempted pid "
						       "%d (%s)\n", pid, ti.name);
					}
					put_task_info(&ti);
					i++;
					goto next_task;
				}
			}

			print_timestamp();
			printf("killing %d timeout %d secs rss %luMiB (%s)\n",
			       pid, (unsigned)(t - tis->time),
			       ti.rss / 1024 / 1024, ti.name);
			put_task_info(&ti);
			kill(pid, SIGKILL);
		}
		sem_post(&tasklist_mem->sem);

		sleep(1);
	};

	free_tasklist();

	free_config_file();

	if (use_cgroups)
		free_cgroups();
}
