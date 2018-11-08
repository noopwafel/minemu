
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

#include "asm_consts_gen.h"
#include "segments.h"

.global jit_fragment_run
.type jit_fragment_run, @function
jit_fragment_run:
incl %fs:CTX__JIT_FRAGMENT_RUNNING
movq  4(%rsp), %rax                  # (struct sigcontext *)%rax
#pusha # FIXME
pushf
push %rax                            # (struct sigcontext *)%rax
# FIXME FIXME bad offsets (below too, much FIXME)
movq 0x38(%rax), %rdx                # (struct sigcontext *)%rax->eip
movq %rdx, %fs:CTX__JIT_FRAGMENT_ENTRY
movq 0x1C(%rax), %rbp
mov %rsp, %fs:CTX__JIT_FRAGMENT_SAVED_ESP

# %rax points to a struct sigcontext
# %esp from sigcontext is ignored, %esp is set to %ebp
# %eip from sigcontext is ignored, we'll jump to the value in jit_eip

push 0x40(%rax)
popf
movq %rbp, %rsp

movq 0x10(%rax), %rdi
movq 0x14(%rax), %rsi
movq 0x18(%rax), %rbp
#ignored:  movl 0x1C(%rax), %esp
movq 0x20(%rax), %rbx
movq 0x24(%rax), %rdx
movq 0x28(%rax), %rcx

# RESTORE SHIELD
mov 0x08(%rax), %es
mov 0x48(%rax), %ss
mov 0x0C(%rax), %ds

movq %cs:0x2C(%rax), %rax

jmp *%fs:CTX__JIT_FRAGMENT_ENTRY

.global jit_fragment_exit
.type jit_fragment_exit, @function
jit_fragment_exit:

SHIELDS_DOWN

xchg %fs:CTX__JIT_FRAGMENT_SAVED_ESP, %rsp

xchg %rax, (%rsp)                    # (struct sigcontext *)%rax / user %rax
pushf

mov %fs, 0x04(%rax)
mov %gs, 0x00(%rax)

# restore sigcontext
movq %rdi, 0x10(%rax)
movq %rsi, 0x14(%rax)
movq %rbp, 0x18(%rax)
movq %fs:CTX__JIT_FRAGMENT_SAVED_ESP, %rbp
movq %rbp, 0x1C(%rax)                # (struct sigcontext *)%rax->esp
movq %rbp, 0x44(%rax)                # (struct sigcontext *)%rax->esp@sig
movq %rbx, 0x20(%rax)
movq %rdx, 0x24(%rax)
movq %rcx, 0x28(%rax)
pop %rdx                             # flags
movq %rdx, 0x40(%rax)
pop %rdx                             # user %rax
movq %rdx, 0x2C(%rax)
movq %fs:CTX__JIT_EIP, %rbp          # %eip
movq %rbp, 0x38(%rax)                # (struct sigcontext *)%rax->eip

movabs $shield_segment, %rcx
movq %rcx, 0x08(%rax)      # %es
movq %rcx, 0x0C(%rax)      # %ds
movq %rcx, 0x48(%rax)      # %ss

popf
#popa # FIXME
decl %fs:CTX__JIT_FRAGMENT_RUNNING
ret

