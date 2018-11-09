
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


#include <linux/unistd.h>
#include <string.h>
#include <sched.h>

#include "runtime.h"
#include "jit.h"
#include "mm.h"
#include "error.h"
#include "syscalls.h"
#include "sigwrap.h"
#include "exec.h"
#include "taint.h"
#include "debug.h"
#include "taint_dump.h"
#include "threads.h"

long syscall_emu(long call, long arg1, long arg2, long arg3,
                            long arg4, long arg5, long arg6)
{
	long ret;
	switch (call)
	{
 		case __NR_brk:
#ifndef __x86_64__
 		case __NR_mmap2:
#endif
 		case __NR_mmap:
 		case __NR_mremap:
 		case __NR_mprotect:
 		case __NR_madvise:

 		case __NR_sigaltstack:
#ifndef __x86_64__
 		case __NR_signal:
 		case __NR_sigaction:
		case __NR_sigreturn:
#endif
 		case __NR_rt_sigaction:
		case __NR_rt_sigreturn:

		case __NR_fork:
		case __NR_vfork:
		case __NR_clone:
		case __NR_exit:

		case __NR_execve:
		case __NR_exit_group:
			break;

		case __NR_read:
		case __NR_readv:
		case __NR_open:
		case __NR_creat:
		case __NR_dup:
		case __NR_dup2:
		case __NR_openat:
		case __NR_pipe:
#ifndef __x86_64__
		case __NR_socketcall:
#endif
			ret = syscall_intr(call,arg1,arg2,arg3,arg4,arg5,arg6);

			if ( taint_flag == TAINT_ON )
				do_taint(ret,call,arg1,arg2,arg3,arg4,arg5,arg6);

			return ret;

#ifndef __x86_64__
 		case __NR_ipc:
			if ( arg1 == SHMAT )
				break;
			/* fall through */
#endif
		default:
			return syscall_intr(call,arg1,arg2,arg3,arg4,arg5,arg6);
	}

	ret = call;
	if (!try_block_signals())
		return ret; /* we have a signal in progress, revert to pre-syscall state */

	switch (call)
	{
		/* these calls are all non-blocking right?
		 * blocked signals during blocking calls is a bad thing
		 */
 		case __NR_brk:
			ret = user_brk(arg1);
			break;
#ifndef __x86_64__
 		case __NR_mmap2:
			ret = user_mmap2(arg1,arg2,arg3,arg4,arg5,arg6);
			break;
 		case __NR_mmap:
			ret = user_old_mmap((struct kernel_mmap_args *)arg1);
			break;
#else
 		case __NR_mmap:
			ret = user_mmap(arg1,arg2,arg3,arg4,arg5,arg6);
			break;
#endif
 		case __NR_mremap:
			ret = user_mremap(arg1,arg2,arg3,arg4,arg5);
			break;
 		case __NR_mprotect:
			ret = user_mprotect(arg1,arg2,arg3);
			break;
 		case __NR_madvise:
			ret = user_madvise(arg1,arg2,arg3);
			break;
#ifndef __x86_64__
 		case __NR_ipc:
			if (arg1 == SHMAT)
				ret = user_shmat(arg2,(char *)arg5,arg3,(unsigned long *)arg4);
			else
				die("should not have caught IPC call: %d", arg1);
			break;
#endif
 		case __NR_sigaltstack:
			ret = user_sigaltstack((stack_t *)arg1, (stack_t *)arg2);
			break;
#ifndef __x86_64__
 		case __NR_signal:
		{
			ret = (long)user_signal(arg1, (kernel_sighandler_t)arg2);
			break;
		}
 		case __NR_sigaction:
			ret = user_sigaction(arg1, (struct kernel_old_sigaction *)arg2,
			                           (struct kernel_old_sigaction *)arg3);
			break;
#endif
 		case __NR_rt_sigaction:
			ret = user_rt_sigaction(arg1, (struct kernel_sigaction *)arg2,
			                              (struct kernel_sigaction *)arg3, arg4);
			break;
#ifndef __x86_64__
 		case __NR_sigreturn:
			user_sigreturn();
			break;
#endif
 		case __NR_rt_sigreturn:
			user_rt_sigreturn();
			break;

		case __NR_vfork:
			ret = user_clone(SIGCHLD, 0, NULL, NULL, NULL);
//			ret = user_clone(CLONE_VFORK | CLONE_VM | SIGCHLD, 0, NULL, NULL, NULL);
			break;
		case __NR_fork:
			ret = user_clone(SIGCHLD, 0, NULL, NULL, NULL);
			break;
		case __NR_clone:
			ret = user_clone(arg1, arg2, (void *)arg3, (void *)arg4, (void*)arg5);
			break;
		case __NR_exit:
			user_exit(arg1);
			break;

		case __NR_execve:
			ret = user_execve((char *)arg1, (char **)arg2, (char **)arg3);
			break;
		case __NR_exit_group:
			if (dump_on_exit)
			{
				long regs[] = { call, arg2, arg3, arg1, get_thread_ctx()->user_rsp, arg6, arg4, arg5 };
				do_taint_dump(regs);
			}
			sys_exit_group(arg1);
			break;
		default:
			die("unimplemented syscall");
			break;
	}
	unblock_signals();
	return ret;
}

