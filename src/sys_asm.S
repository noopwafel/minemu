
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

.text
#include "asm_consts_gen.h"
#include "segments.h"

#
# syscall wrapper
#
runtime_syscall:
pushf
push %rcx
push %rdx
push %rbp
xor %rbp, %rbp
push %rdi
push %rsi
push %rdx
push %rcx
push %rbx
push %rax
call syscall_emu
lea 48(%rsp), %rsp
pop %rbp
pop %rdx
pop %rcx
popf
ret

#
# emu_start(): Protect minemu memory, load registers, jump to address
#
.global emu_start
.type emu_start, @function
emu_start: # (long eip, long esp)
movabs $minemu_stack_bottom, %rax
movq %rsp, (%rax)   # save orig stack address for runtime_jit()
movq %rdi, %rax               # load user %eip from arg1
movq %rsi, %rsp               # load user's stack pointer from arg2
xor %rcx, %rcx                   # clear registers and flags
xor %rdx, %rdx
xor %rbx, %rbx
xor %rbp, %rbp
xor %rsi, %rsi
xor %rdi, %rdi
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rcx, %xmm3 # cleared %eax
SHIELDS_UP
jmp *%fs:CTX__RUNTIME_IJMP_ADDR

.global int80_emu
.type int80_emu, @function
int80_emu:
SHIELDS_DOWN
mov %rsp, %fs:CTX__USER_ESP
mov %fs:CTX__SCRATCH_STACK_TOP, %rsp
call runtime_syscall
pop %rsp
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rax, %xmm3
mov $0x0,%rcx
movq %fs:CTX__USER_EIP, %rax
SHIELDS_UP
jmp *%fs:CTX__RUNTIME_IJMP_ADDR

.global linux_sysenter_emu
.type linux_sysenter_emu, @function
linux_sysenter_emu:
SHIELDS_DOWN
movq (%rsp), %rbp      # restore %ebp, (is used to store %esp during sysenter.)
mov %rsp, %fs:CTX__USER_ESP
mov %fs:CTX__SCRATCH_STACK_TOP, %rsp
call runtime_syscall
pop %rsp
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rax, %xmm3
mov $0x0,%rcx
movabs sysenter_reentry, %rax
SHIELDS_UP
jmp *%fs:CTX__RUNTIME_IJMP_ADDR

.global state_restore
.type state_restore, @function
state_restore:
SHIELDS_DOWN
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rax, %xmm3
mov $0x0,%rcx
movq %fs:CTX__USER_EIP, %rax
SHIELDS_UP
jmp *%fs:CTX__RUNTIME_IJMP_ADDR

.global hook_stub
.type hook_stub, @function
hook_stub:
SHIELDS_DOWN
mov %rsp, %fs:CTX__USER_ESP
mov %fs:CTX__SCRATCH_STACK_TOP, %rsp
pushf
push %rdi
push %rsi
push %rbp
push %fs:CTX__USER_ESP
push %rbx
push %rdx
push %rcx
push %rax
push %rsp           # *(long)regs
call *%fs:CTX__HOOK_FUNC
test %rax, %rax     # non-zero -> abort
lea 8(%rsp), %rsp
pop %rax
pop %rcx
pop %rdx
pop %rbx
lea 8(%rsp), %rsp
pop %rbp
pop %rsi
pop %rdi
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rax, %xmm3
pinsrq $0, %rdx, %xmm5
jnz hook_fault
popf
mov %fs:CTX__USER_ESP, %rsp
SHIELDS_UP
jmp *%fs:CTX__JIT_RETURN_ADDR

hook_fault:
movq %fs:CTX__USER_EIP, %rax
movq $0, %rcx
popf
mov %fs:CTX__USER_ESP, %rsp
SHIELDS_UP
jmp taint_fault

