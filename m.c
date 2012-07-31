/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Author: Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define ALLOC_NR_PAGES 256

int main(void)
{
	void *alloc_app_pages[ALLOC_NR_PAGES];
	int i;

	for (i = 0; i < ALLOC_NR_PAGES; i++) {
		alloc_app_pages[i] = malloc(4096);
		if (alloc_app_pages[i])
			memset(alloc_app_pages[i], 'z', 4096);
	}

	getchar();

	return 0;
}
