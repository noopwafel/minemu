
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

#ifndef SEGMENTS_H
#define SEGMENTS_H

#define PREF_TLS_GDT_ENTRY (7)
#define PREF_SHIELD_GDT_ENTRY (8)

#ifndef __x86_64__
#define SHIELDS_UP \
	mov %cs:shield_segment, %ds ; \
	mov %cs:shield_segment, %es ; \
	mov %cs:shield_segment, %ss ;

#define SHIELDS_DOWN \
	mov %cs:data_segment, %ds ; \
	mov %cs:data_segment, %es ; \
	mov %cs:data_segment, %ss ;
#else
#define SHIELDS_UP
#define SHIELDS_DOWN
#endif

#ifndef __ASSEMBLER__

extern unsigned int shield_segment, data_segment, code_segment;

void init_tls(void *base_addr, unsigned long size);
void init_shield(unsigned long size);

inline long get_tls_long(long offset)
{
	long ret;
	__asm__ __volatile__ ("mov %%fs:(%1), %0":"=r"(ret): "r" (offset));
	return ret;
}

#endif

#endif /* SEGMENTS_H */
