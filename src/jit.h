
/* This file is part of minemu
 *
 * Copyright 2010-2011 Erik Bosman <erik@minemu.org>
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

#ifndef JIT_H
#define JIT_H

#include "opcodes.h"
#include "codemap.h"

extern long jit_lock;

void jit_init(void);
void jit_resize(code_map_t *map, unsigned long cur_size);
char *jit(char *addr);
char *jit_lookup_addr(char *addr);
char *jit_rev_lookup_addr(char *jit_addr, char **jit_op_start, long *jit_op_len);

#endif /* JIT_H */
