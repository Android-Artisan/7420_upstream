/*
 * Project Name JPEG DRIVER IN Linux
 * Copyright  2007 Samsung Electronics Co, Ltd. All Rights Reserved. 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/wait.h>

#include <asm/io.h>
#include <plat/regs-lcd.h>

#include "JPGMisc.h"
#include "JPGMem.h"

static HANDLE hMutex	= NULL;
extern wait_queue_head_t	WaitQueue_JPEG;

/*----------------------------------------------------------------------------
*Function: CreateJPGmutex
*Implementation Notes: Create Mutex handle 
-----------------------------------------------------------------------------*/
HANDLE CreateJPGmutex(void)
{
	hMutex = (HANDLE)kmalloc(sizeof(struct mutex), GFP_KERNEL);
	if (hMutex == NULL)
		return NULL;
	
	mutex_init(hMutex);
	
	return hMutex;
}

/*----------------------------------------------------------------------------
*Function: LockJPGMutex
*Implementation Notes: lock mutex 
-----------------------------------------------------------------------------*/
DWORD LockJPGMutex(void)
{
    mutex_lock(hMutex);  
    return 1;
}

/*----------------------------------------------------------------------------
*Function: UnlockJPGMutex
*Implementation Notes: unlock mutex
-----------------------------------------------------------------------------*/
DWORD UnlockJPGMutex(void)
{
	mutex_unlock(hMutex);
	
    return 1;
}

/*----------------------------------------------------------------------------
*Function: DeleteJPGMutex
*Implementation Notes: delete mutex handle 
-----------------------------------------------------------------------------*/
void DeleteJPGMutex(void)
{
	if (hMutex == NULL)
		return;

	mutex_destroy(hMutex);
}

unsigned int get_fb0_addr(void)
{
	return readl(S3C_VIDW00ADD0B0);
}

void get_lcd_size(int *width, int *height)
{
	unsigned int	tmp;
	
	tmp		= readl(S3C_VIDTCON2);
	*height	= ((tmp >> 11) & 0x7FF) + 1;
	*width	= (tmp & 0x7FF) + 1;
}

void WaitForInterrupt(void)
{
	interruptible_sleep_on_timeout(&WaitQueue_JPEG, 100);
}

