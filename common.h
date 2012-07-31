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

#ifndef __TBULMK_COMMON_H
#define __TBULMK_COMMON_H

extern void pabort(const char *s);
extern void print_timestamp(void);

typedef unsigned long ulong;

struct task_info {
	char *name;
	time_t time;
	int activity;
	ulong rss;
	int tty_nr;
};

int get_task_info(pid_t pid, const char *dname, struct task_info *ti);
void put_task_info(struct task_info *ti);

#endif
