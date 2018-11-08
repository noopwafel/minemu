
/* This file is part of minemu
 *
 * Copyright 2010-2011 Erik Bosman <erik@minemu.org>
 * Copyright 2011 Vrije Universiteit Amsterdam
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <asm/ldt.h>
#include <asm/prctl.h>

#include "segments.h"
#include "syscalls.h"
#include "error.h"

unsigned int shield_segment, data_segment, code_segment;

static void set_fs_segment(int number)
{
	__asm__ __volatile__ ("mov %0, %%fs":: "r" (number));
}

static unsigned int create_segment(int entry_number, void *base_addr, unsigned long size)
{
	struct user_desc seg =
	{
		.entry_number = entry_number,
#ifndef __x86_64__
		.base_addr = (int)base_addr,
		.limit = 1 + ( (size-1) & ~0xfff ) / 0x1000,
#else
		// TODO: no segmentation on 64-bit
#endif
		.seg_32bit = 1,
		.limit_in_pages = 1,
	};

	if (sys_set_thread_area(&seg) < 0)
	{
		if (entry_number != -1)
			return create_segment(-1, base_addr, size);
		else
			die("cannot allocate segment");
	}

	return 3 + 8*seg.entry_number;
}

void init_tls(void *base_addr, unsigned long size)
{
	//set_fs_segment(create_segment(PREF_TLS_GDT_ENTRY, base_addr, size));
	sys_arch_prctl(ARCH_SET_FS, base_addr);
}

/* The segment shield that makes emulated code unable to touch emulator memory
 */
void init_shield(unsigned long size)
{
	return; // no segments on amd64
	shield_segment = create_segment(PREF_SHIELD_GDT_ENTRY, 0x00000000, size);
	data_segment = code_segment = 0;
	__asm__ __volatile__ ("movabs $data_segment, %%rax\nmov %%ds, (%%rax)":::"rax"); /* save original data segment */
	__asm__ __volatile__ ("movabs $code_segment, %%rax\nmov %%cs, (%%rax)":::"rax"); /* save original code segment */
}
