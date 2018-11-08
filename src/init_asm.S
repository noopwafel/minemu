
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
.global start
.type start, @function
start:
pop  %rdx                   # argc -> %edx, argv -> %esp
lea  8(%rsp, %rdx, 8), %rbx # envp -> %ebx
xor  %rax,             %rax
xor  %rcx,             %rcx
dec  %rcx
cld
mov  %rbx,             %rdi
repne scas %es:(%rdi), %rax # auxv -> %edi
mov  %rsp,             %rax # argv -> %eax

mov %rdi, %rcx # auvx is arg 3
mov %rdx, %rdi # argc is arg 0
mov %rax, %rsi # argv is arg 1
mov %rbx, %rdx # envp is arg 2
call minemu_main
