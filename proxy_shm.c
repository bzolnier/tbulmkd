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
#include <sys/types.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include "common.h"
#include "shm.h"

struct tasklist_mem *tasklist_mem;

/**
 *	update_tasks - update tasklist_mem task list
 *
 *	Fill tasklist_mem task list for all tasks in the system using
 *	information from /proc/$pid/stat and /proc/$pid/activity[_time].
 *
 *	tasklist_mem list is terminated by using task entry with PID == 0.
 *
 *	This function needs to take tasklist_sem->sem semaphore to protect
 *	access to tasklist_mem task list.
 */
static void update_tasks(void)
{
	DIR *dir;
	struct dirent *de;
	int i = 0;

	dir = opendir("/proc");
	if (!dir)
		pabort("opendir proc");

	while ((de = readdir(dir))) {
		struct task_info ti;
		const char *dname = de->d_name;

		if (!strcmp(dname, "1") || !strcmp(dname, "self") ||
		    !strcmp(dname, "."))
			continue;

		if (get_task_info(0, dname, &ti))
//		if (get_task_info_stat(0, dname, &ti))
			continue;

		printf("%s %d %u\n", dname, ti.activity, (unsigned)ti.time);
//		printf("%s %d %lu\n", dname, ti.tty_nr, ti.rss / 1024 / 1024);

		sem_wait(&tasklist_mem->sem);
		tasklist_mem->tasks[i].pid = atoi(dname);
		tasklist_mem->tasks[i].activity = ti.activity;
		tasklist_mem->tasks[i].time = ti.time;
//		tasklist_mem->tasks[i].activity = 1;
//		tasklist_mem->tasks[i].time = time(NULL);
		tasklist_mem->tasks[i].tty_nr = ti.tty_nr;
		i++;
		if (i < MAX_NR_TASKS)
			tasklist_mem->tasks[i].pid = 0;
		sem_post(&tasklist_mem->sem);
	}

	closedir(dir);
}

/*
 * Creates and mmap()s shared memory area containing list of tasks.
 * Then updates tasklist_mem task list once for every second.
 */
int main(void)
{
	int tasklist_fd;
	int ret;

	shm_unlink("/tbulmkd_tasklist");

	tasklist_fd = shm_open("/tbulmkd_tasklist", O_RDWR | O_CREAT, 0600);
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

	sem_init(&tasklist_mem->sem, 1, 1);

	while (1) {
		update_tasks();
		sleep(1);
	}
	return 0;
}
