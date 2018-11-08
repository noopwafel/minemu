
# This file is part of minemu
#
# Copyright 2010-2011 Erik Bosman <erik@minemu.org>
# Copyright 2011 Vrije Universiteit Amsterdam
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#include <asm/unistd.h>
#define SIGKILL      9
#include "asm_consts_gen.h"

.text
.global mutex_lock # ( long *lock_addr )
.type mutex_lock, @function
mutex_lock:
push %rbx
mutex_lock_retry:
movq $1, %rdx
xor %rcx, %rcx
movq %rdi, %rbx
spin:
movq $0, %rax
lock cmpxchg %rdx, (%rbx)
loopneq spin
jne 1f
pop %rbx
mfence
ret
1:
movq $(__NR_sched_yield), %rax
int $0x80
jmp mutex_lock_retry

.global mutex_init # ( long *lock_addr )
.type mutex_init, @function
mutex_init:

.global mutex_unlock # ( long *lock_addr )
.type mutex_unlock, @function
mutex_unlock:
mfence
movq $0, (%rdi)
ret

.global mutex_unlock_exit # ( long status, long *lock_addr )
.type mutex_unlock_exit, @function
mutex_unlock_exit:
movq 16(%rsp), %rdx
movq 8(%rsp), %rbx
movq $(__NR_exit), %rax
mfence
movq $0, (%rdx)
int $0x80
ud2

.global mutex_unlock_execve_or_die # ( char *filename, char *argv[], char *envp[], long *lock_addr )
.type mutex_unlock_execve_or_die, @function
mutex_unlock_execve_or_die:
movq 0x20(%esp), %rsi
movq 0x18(%esp), %rdx
movq 0x10(%esp), %rcx
movq 0x08(%esp), %rbx
movq $(__NR_execve), %rax
mfence
movq $0, (%rsi)
int $0x80
movq $(__NR_gettid), %rax
int $0x80
movq %rax, %rbx
movq %rax, %rcx
movq $(SIGKILL), %rdx
movq $(__NR_tgkill), %rax
int $0x80
ud2

.global atomic_clear_8bytes
.type atomic_clear_8bytes, @function
atomic_clear_8bytes:
push %rdi
movq 0x18(%rsp), %rdi
movq 0x10(%rsp), %rax
movq 0x8(%rax), %rdx
movq  (%rax), %rax
push %rbx
xor %rbx, %rbx
xor %rcx, %rcx
lock cmpxchg8b (%rdi)
pop %rbx
pop %rdi
ret

# This wil break HARD with -fomit-frame-pointers
#
#
patch_base_pointers: # ( difference )
test %rbp, %rbp
jz done
addq 8(%rsp), %rbp
movq %rbp, %rdx

1:
movq (%rdx), %rax
test %rax, %rax
jz done
addq 8(%rsp), %rax
movq %rax, (%rdx)
movq %rax, %rdx
jmp 1b

done:
ret


.global clone_relocate_stack # ( flags, sp, &parent_tid, dummy, &child_tid, stack_diff )
.type clone_relocate_stack, @function
clone_relocate_stack:
push %rbp
movq %rsp, %rbp
push %rbx
push %rsi
push %rdi

movq 0x38(%rbp), %rax
push %rax                  # stack_diff (for patch_base_pointers)

movq %rsp, %rbx            # src
addq %rsp, %rax            # dest ( = stack_diff + src )

movq %fs:CTX__MY_ADDR, %rcx
addq $CTX__SIZE, %rcx
subq %rsp, %rcx            # size

push %rcx
push %rbx
push %rax
call memcpy
lea 0x18(%rsp), %rsp

movq $(__NR_clone), %rax
movq 0x10(%rbp), %rbx                          # flags
movq 0x38(%rbp), %rcx                          # child_sp = stack_diff
addq      %rsp , %rcx                          # child_sp = stack_diff + sp
movq 0x20(%rbp), %rdx                          # &parent_tid
movq 0x28(%rbp), %rsi
movq 0x30(%rbp), %rdi                          # &child_tid
int $0x80
cmp %rcx, %rsp
jne 1f
mov %rax, %rbx
call patch_base_pointers # ( stack_diff )
mov %rbx, %rax

1:
lea 8(%rsp), %rsp          # discard stack_diff
pop %rdi
pop %rsi
pop %rbx
pop %rbp
ret
