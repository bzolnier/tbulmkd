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

#ifndef __TBULMKD_SHM_H
#define __TBULMKD_SHM_H

#include <semaphore.h>
#include <time.h>

#define MAX_NR_TASKS 1000

struct task_info_shm {
	pid_t pid;
	time_t time; /* last update to activity */
	int activity; /* 1 == foreground, 0 == background */
	int tty_nr;
};

struct tasklist_mem {
	sem_t sem;
	struct task_info_shm tasks[MAX_NR_TASKS];
};

#endif
