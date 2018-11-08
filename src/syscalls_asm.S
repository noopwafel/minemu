
# This file is part of minemu
#
# Copyright 2010-2011 Erik Bosman <erik@minemu.org>
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

#include "asm_consts_gen.h"

#define DO_SYSCALL_SYSCALL \
push %rcx ;\
push %r11 ;\
syscall ;\
pop %r11 ;\
pop %rcx

# FIXME: these

#define DO_SYSCALL_INTR_PRE_SYSCALL \
syscall

#define DO_SYSCALL_INTR_SYSCALL \
syscall

#define DO_SYSCALL_INTR_RESTARTSYS_SYSCALL \
syscall

.text

# Sysenter did not do much for performance.
# It works, as long as the vdso is intact.
# I decided not to use it because of its dependence on linux kernel tricks.

#define DO_SYSCALL_SYSENTER \
push $1f ;\
push %rcx ;\
push %rdx ;\
push %rbp ;\
movq %rsp, %rbp ;\
sysenter ;\
1:

#define DO_SYSCALL_INTR_PRE_SYSENTER \
push $1f ;\
push %rcx ;\
push %rdx ;\
push %rbp ;\
movq %rsp, %rbp

#define DO_SYSCALL_INTR_SYSENTER \
sysenter

#define DO_SYSCALL_INTR_RESTARTSYS_SYSENTER \
movq %rbp, %rsp ;\
pop %rbp ;\
pop %rdx ;\
pop %rcx ;\
lea 8(%rsp), %rsp ;\
1:

# # define DO_SYSCALL_INT80 int $0x80
# #define DO_SYSCALL_INTR_INT80 int $0x80
# #define DO_SYSCALL_INTR_PRE_INT80 ;
# #define DO_SYSCALL_INTR_RESTARTSYS_INT80 ;

# #define DO_SYSCALL                  DO_SYSCALL_INT80
# #define DO_SYSCALL_INTR             DO_SYSCALL_INTR_INT80
# #define DO_SYSCALL_INTR_PRE         DO_SYSCALL_INTR_PRE_INT80
# #define DO_SYSCALL_INTR_RESTARTSYS  DO_SYSCALL_INTR_RESTARTSYS_INT80

#define DO_SYSCALL DO_SYSCALL_SYSCALL
#define DO_SYSCALL_INTR DO_SYSCALL_INTR_SYSCALL
#define DO_SYSCALL_INTR_PRE DO_SYSCALL_INTR_PRE_SYSCALL
#define DO_SYSCALL_INTR_RESTARTSYS DO_SYSCALL_INTR_RESTARTSYS_SYSCALL

.global syscall0
.type syscall0, @function
syscall0:
movq %rdi, %rax
DO_SYSCALL
ret

.global syscall1
.type syscall1, @function
syscall1:
push %rdi
movq %rdi, %rax
movq %rsi, %rdi
DO_SYSCALL
pop %rdi
ret

.global syscall2
.type syscall2, @function
syscall2:
push %rdi
push %rsi
movq %rdi, %rax
movq %rsi, %rdi
movq %rdx, %rsi
DO_SYSCALL
pop %rsi
pop %rdi
ret

.global syscall3
.type syscall3, @function
syscall3:
push %rdi
push %rsi
push %rdx
movq %rdi, %rax
movq %rsi, %rdi
movq %rdx, %rsi
movq %rcx, %rdx
DO_SYSCALL
pop %rdx
pop %rsi
pop %rdi
ret

.global syscall4
.type syscall4, @function
syscall4:
push %rdi
push %rsi
push %rdx
push %r10
movq %rdi, %rax
movq %rsi, %rdi
movq %rdx, %rsi
movq %rcx, %rdx
movq %r8, %r10
DO_SYSCALL
pop %r10
pop %rdx
pop %rsi
pop %rdi
ret

.global syscall5
.type syscall5, @function
syscall5:
push %rdi
push %rsi
push %rdx
push %r10
push %r8
movq %rdi, %rax
movq %rsi, %rdi
movq %rdx, %rsi
movq %rcx, %rdx
movq %r8, %r10
movq %r9, %r8
DO_SYSCALL
pop %r8
pop %r10
pop %rdx
pop %rsi
pop %rdi
ret

.global syscall6
.type syscall6, @function
syscall6:
push %rdi
push %rsi
push %rdx
push %r10
push %r8
push %r9
movq %rdi, %rax
movq %rsi, %rdi
movq %rdx, %rsi
movq %rcx, %rdx
movq %r8, %r10
movq %r9, %r8
movq 0x38(%rsp), %r9
DO_SYSCALL
pop %r9
pop %r8
pop %r10
pop %rdx
pop %rsi
pop %rdi
ret

# FIXME
.global syscall_intr
.type syscall_intr, @function
syscall_intr:
push %rbp
movq %rsp, %rbp
push %rbx
push %rsi
push %rdi
movq 0x10(%rbp),%rax
movq 0x18(%rbp),%rbx
movq 0x20(%rbp),%rcx
movq 0x28(%rbp),%rdx
movq 0x30(%rbp),%rsi
movq 0x38(%rbp),%rdi
movq 0x40(%rbp),%rbp
DO_SYSCALL_INTR_PRE
.global syscall_intr_critical_start
syscall_intr_critical_start:
movb $1, %fs:CTX__JIT_FRAGMENT_RESTARTSYS
testb $1, %fs:CTX__JIT_FRAGMENT_RUNNING
jnz return
.global syscall_intr_critical_end
syscall_intr_critical_end:
DO_SYSCALL_INTR
return:
DO_SYSCALL_INTR_RESTARTSYS
pop %rdi
pop %rsi
pop %rbx
pop %rbp
ret

