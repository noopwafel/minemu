
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

#include <asm/unistd.h>

.global load_sigframe
.type load_sigframe, @function

.global do_sigreturn
.type do_sigreturn, @function

load_sigframe:
movq 8(%rsp), %rsp
pop %rax
do_sigreturn:
pop %rax
#movq $(__NR_sigreturn), %rax # FIXME FIXME
int $0x80
ud2

.global load_rt_sigframe
.type load_rt_sigframe, @function

.global do_rt_sigreturn
.type do_rt_sigreturn, @function

load_rt_sigframe:
movq 8(%rsp), %rsp
pop %rax
do_rt_sigreturn:
movq $(__NR_rt_sigreturn), %rax
int $0x80
ud2

