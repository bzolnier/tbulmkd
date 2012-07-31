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
#include <poll.h>
#include "common.h"
#include "shm.h"
#include "tbulmkd.h"

#define PFX "tbulkmd: "

static struct tasklist_mem *tasklist_mem;

#define THRES_NR 2
struct mem_threshold mem_thresholds[THRES_NR];

enum {
	THRES_DAEMONS_IDX	= 0,
	THRES_APPS_IDX		= 1,
};

static char *exemption_list[MAX_NR_TASKS];
static int exemption_list_len;

#define POLL_TIMEOUT 1000

static pid_t select_pid_rss(int idx, ulong *max_rss)
{
	pid_t last_pid = 0;
	int i;

	sem_wait(&tasklist_mem->sem);

	for (i = 0; i < MAX_NR_TASKS; i++) {
		struct task_info_shm *tis;
		struct task_info ti;
		pid_t pid;
		int j;

		tis = &tasklist_mem->tasks[i];
		pid = tis->pid;
		if (!pid)
			break;

		if ((idx == THRES_DAEMONS_IDX && tis->tty_nr) ||
		    (idx == THRES_APPS_IDX && !tis->tty_nr))
			continue;

		if (get_task_info_stat(pid, NULL, &ti))
			continue;

		// debug
//		if (strcmp("m", ti.name)) {
//			put_task_info(&ti);
//			continue;
//		}

		if (ti.rss > *max_rss) {
			*max_rss = ti.rss;
			last_pid = pid;
		}

		put_task_info(&ti);
	}

	sem_post(&tasklist_mem->sem);

	return last_pid;
}

static void poll_lowmem(void)
{
	struct pollfd pollfds[THRES_NR];

	setup_events(pollfds, THRES_DAEMONS_IDX);
	setup_events(pollfds, THRES_APPS_IDX);

	while (poll(pollfds, THRES_NR, POLL_TIMEOUT) > 0) {
		int i;

		if (DEBUG) {
			print_timestamp();
			puts("got lowmem event");
		}

		for (i = 0; i < THRES_NR; i++) {
			struct mem_threshold *thres = &mem_thresholds[i];

			if (pollfds[i].revents & POLLIN) {
				process_event(i);

				while (get_mem_usage(i) >= thres->mem_limit) {
					struct task_info ti;
					ulong rss = 0;
					pid_t pid = select_pid_rss(i, &rss);

					if (!pid)
						continue;

					if (get_task_info_stat(pid, NULL, &ti))
						continue;

					print_timestamp();
					printf("[cgroups] killing %d rss %luMiB"
					       " (%s)\n", pid, rss / 1024 / 1024,
					       ti.name);
					put_task_info(&ti);
					kill(pid, SIGKILL);

					sleep(1);
				}
			}
		}
	}

	cleanup_events(THRES_APPS_IDX);
	cleanup_events(THRES_DAEMONS_IDX);
}


static int timeout = 60; /* timeout in seconds */
static int use_cgroups = 0;
static int apps_mem_percent = 90;
static int daemons_mem_percent = 10;

static void print_usage(char *argv0)
{
	printf("Usage: %s [OPTION]...\n"
	       "\n"
	       "-a, --apps	set memory percent for apps cgmem\n"
	       "-d, --daemons	set memory percent for daemons cgmem\n"
	       "-c, --cgroups	use control groups memory controller\n"
	       "-t, --timeout	set timeout (in seconds)\n"
	       "-h, --help	display this help message\n"
	       "\n",
	       argv0);
}

static void parse_args(int argc, char *argv[])
{
	struct option opts[] = {
		{ "apps",	1, NULL, 'a' },
		{ "daemons",	1, NULL, 'd' },
		{ "cgroups",	0, NULL, 'c' },
		{ "timeout",	1, NULL, 't' },
		{ "help",	0, NULL, 'h' },
	};
	int c;

	while (1) {
		c = getopt_long(argc, argv, "a:d:t:hc", opts, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'a':
			apps_mem_percent = atoi(optarg);
			print_timestamp();
			printf("using %d%% of memory for apps cgmem\n",
			       apps_mem_percent);
			break;
		case 'd':
			daemons_mem_percent = atoi(optarg);
			print_timestamp();
			printf("using %d%% of memory for daemons cgmem\n",
			       daemons_mem_percent);
			break;
		case 'c':
			use_cgroups = 1;
			print_timestamp();
			printf("using control groups memory controller\n");
			break;
		case 't':
			timeout = atoi(optarg);
			print_timestamp();
			printf("using %d seconds timeout\n", timeout);
			break;
		case 'h':
			print_usage(argv[0]);
			exit(1);
			break;
		}
	}
}

static int tasklist_fd;

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
		printf("\t%s\n", exemption_list[i]);
	}
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

		printf("\t%d pid %d time %u\n", i, bt->pid, (unsigned)bt->time);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	init_config_file();
	if (DEBUG)
		print_exemption_list();

	ret = mlockall(MCL_FUTURE);
	if (ret)
		pabort("mlockall");

	parse_args(argc, argv);

	if (use_cgroups)
		init_cgroups();

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

			if (get_task_info_stat(pid, NULL, &ti))
				continue;

			/* skip kernel threads */
			if (!ti.rss) {
				if (DEBUG) {
					print_timestamp();
					printf("skipping pid (rss = 0)"
					       "%d (%s)\n", pid, ti.name);
				}
				put_task_info(&ti);
				continue;
			}

			for (j = 0; j < exemption_list_len; j++) {
				if (!strcmp(exemption_list[j], ti.name)) {
					if (DEBUG) {
						print_timestamp();
						printf("[timeout] skipping "
						       "exempted pid %d (%s)\n",
						       pid, ti.name);
					}
					put_task_info(&ti);
					i++;
					goto next_task;
				}
			}

			print_timestamp();
			printf("[timeout] killing %d timeout %d secs rss %luMiB"
			       " (%s)\n", pid, (unsigned)(t - tis->time),
			       ti.rss / 1024 / 1024, ti.name);
			put_task_info(&ti);
			kill(pid, SIGKILL);
		}
		sem_post(&tasklist_mem->sem);

		if (use_cgroups)
			poll_lowmem();
		else
			sleep(1);
	};

	free_tasklist();

	free_config_file();

	if (use_cgroups)
		free_cgroups();
}
