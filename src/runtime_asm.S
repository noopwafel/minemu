
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

.text
#include "asm_consts_gen.h"
#include "segments.h"
#include "opcodes.h"
#include "taint.h"

.global runtime_cache_resolution_start
runtime_cache_resolution_start:

.global taint_fault
.type taint_fault, @function
taint_fault:
SHIELDS_DOWN
movq %rcx, %fs:CTX__IJMP_TAINT
movq %rax, %fs:CTX__USER_EIP
movq %rsp, %fs:CTX__USER_ESP
movq %fs:CTX__SCRATCH_STACK_TOP, %rsp
pushf
test $TAINT_RET_TRAP_INVMASK, %rcx
pextrq $0, %xmm4, %rcx
pextrq $0, %xmm3, %rax
pextrq $0, %xmm5, %rdx
push %rdi
push %rsi
push %rbp
push %fs:CTX__USER_ESP
push %rbx
push %rdx
push %rcx
push %rax
push %rsp           # *(long)regs
jz return_hook_taint
call block_signals
call do_taint_dump
ud2
return_hook_taint:
call block_signals
call return_hook
push %rax
call unblock_signals
pop %rax
test %rax,%rax
lea 4(%rsp), %rsp
pop %rax
pop %rcx
pop %rdx
pop %rbx
lea 4(%rsp), %rsp
pop %rbp
pop %rsi
pop %rdi
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rax, %xmm3
pinsrq $0, %rdx, %xmm5
movq %fs:CTX__USER_EIP, %rax
movq $0, %rcx
jnz hook_fault
popf
mov %fs:CTX__USER_ESP, %rsp
SHIELDS_UP
jmp runtime_ijmp

hook_fault:
popf
mov %fs:CTX__USER_ESP, %rsp
SHIELDS_UP
jmp taint_fault

taint_fault_short:
jmp taint_fault
#
# RET / ICALL / IJMP FAST PATH:
#
# runtime_ret()/runtime_ijmp() are called from the jit code
#
.balign 64
.global runtime_ret
.type runtime_ret, @function

runtime_ret:
pinsrq $0, %rcx, %xmm4
pinsrq $0, %rax, %xmm3
mov taint_offset(%rsp), %rcx
pop %rax

#
# Status from here: original %eax:         %xmm3[0:31]
#                   original %ecx:         %xmm4[0:31]
#                   taint:                 %ecx
#                   original jump target:  %eax
#
.global runtime_ijmp
.type runtime_ijmp, @function
runtime_ijmp:
pinsrq $0, %rdx, %xmm5
movzwq %ax, %rdx
jecxz,pt taint_ok
jmp taint_fault_short
taint_ok:
movq %fs: (, %rdx, 8), %rcx         # load mangled cached address
movq %fs:4(, %rdx, 8), %rdx
lea (%rcx,%rax,1), %rcx             # %ecx = addr + CACHE_MANGLE(cached_addr)
                                    # %ecx is 1 if there is a cache hit
movq %rdx, %fs:CTX__JIT_EIP
loop cache_lookup                   # branch taken on cache miss

.global jit_return
.type jit_return, @function
jit_return: # thread_ctx->jit_eip contains jump address
pextrq $0, %xmm4, %rcx
pextrq $0, %xmm3, %rax
pextrq $0, %xmm5, %rdx
jmp *%fs:offset__jit_eip_HACK   # The Makefile changes this into %fs:offset__jit_eip
                                # in the normal code. But the code in this file is
                                # included twice in the binary. The second copy is
                                # for finishing an emulated instruction when a signal
                                # comes in. In this copy, this symbol gets changed
                                # into %fs:offset__jit_fragment_exit_addr. If the
                                # program counter is within this object file when a
                                # signal comes in, the program counter gets
                                # 'transplanted' into the other code and instead of
                                # jumping back to jit code, we jump back into the
                                # signal handler wrapper.
                                    
#
# END FAST PATH
#
#

cache_lookup:
mov %rax, %rdx
lahf
mov %rax, %fs:CTX__FLAGS_TMP
movzwq %dx, %rax                  # HASH_INDEX[addr]
cache_lookup_loop:
inc %ax
mov %fs:(, %rax, 8), %rcx
lea -1(%rcx,%rdx,1), %rcx    # %ecx = addr + CACHE_MANGLE(cached_addr)-1
jecxz,pt cache_hit
testl $-1, %fs:(, %rax, 8)   # jmp_cache.addr[HASH_INDEX[addr]] == 0
jz cache_miss
cmp %dx, %ax
jne cache_lookup_loop
jmp cache_miss
cache_hit:
mov %fs:4(, %rax, 8), %rdx
mov %fs:CTX__FLAGS_TMP, %rax
sahf
movq %rdx, %fs:CTX__JIT_EIP
jmp jit_return

.global cpuid_emu
.type cpuid_emu, @function
cpuid_emu:
xchg %rax, %fs:CTX__FLAGS_TMP
lahf
xchg %rax, %fs:CTX__FLAGS_TMP
cmpl $1, %eax
cpuid
jne 1f
# mask SSE registers in feature set
# FIXME: this won't fly on 64-bit :)
andl $(CPUID_FEATURE_INFO_ECX_MASK), %ecx
andl $(CPUID_FEATURE_INFO_EDX_MASK), %edx
1:
xchg %rax, %fs:CTX__FLAGS_TMP
sahf
xchg %rax, %fs:CTX__FLAGS_TMP
jmp *%fs:offset__jit_eip_HACK                 # see comment above :-)

.global runtime_cache_resolution_end
runtime_cache_resolution_end:
nop

# address not found in jmp_cache
#
# Processor state:
#
# %eax:    address
# %e[cd]x: clobbered
# FIXME: for now alyssa just pushed r8-r11 below
#
cache_miss:
mov %fs:CTX__FLAGS_TMP, %rax
sahf
SHIELDS_DOWN
mov %rsp, %fs:CTX__USER_ESP
mov %fs:CTX__SCRATCH_STACK_TOP, %rsp
push %rdi
pushf
push %r8
push %r9
push %r10
push %r11
mov %rdx, %rdi
push %rdi
call jit_lookup_addr # (char *addr);
test %rax, %rax      # jit_addr or NULL
jnz lookup_hit

# the code we're looking for has not been translated yet
pop %rdi
push %rdi
call runtime_jit     # (char *addr);

lookup_hit:
movq %rax, %fs:CTX__JIT_EIP
pop %rdi
pop %r11
pop %r10
pop %r9
pop %r8
popf
pop %rsp
SHIELDS_UP
jmp *%fs:CTX__JIT_RETURN_ADDR

#
# total miss, we have to translate some code
#
runtime_jit:

movabs $jit_lock, %rdi
call mutex_lock

movq 8(%rsp), %rax
movq %rsp, %rdx                     # switch to jit stack
movabs $minemu_stack_bottom, %rsp      #
mov (%rsp), %rsp
push %rdx # save old rsp
mov %rax, %rdi                      # address to be translated
call jit
pop %rsp                            # revert to scratch stack
push %rax # save jit return value

movabs $jit_lock, %rdi
call mutex_unlock

pop %rax # restore jit return value
ret

