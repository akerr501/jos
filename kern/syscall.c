/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	// checking first if this environment has permissions to print this memory
	user_mem_assert(curenv, s, len, PTE_U);

	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env *e = NULL;
	int r = env_alloc(&e, curenv ? curenv->env_id : 0); // allocate a new environment
	if(r < 0) return r; // if we failed to allocate, return the error

	e->env_status = ENV_NOT_RUNNABLE; // set new environment the status as not runnable
	e->env_tf = curenv->env_tf; // set the new environment trapframe to be the current trapframe
	e->env_tf.tf_regs.reg_eax = 0; // set the new environment trapframe eax register to be 0
	return e->env_id; // return the new environment id
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.

	if(status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) return -E_INVAL; // if the status is not being set to runnable or not runnable, return error
	struct Env *e = NULL;
	if(envid2env(envid, &e, 1) < 0) return -E_BAD_ENV; // check to see if the environment exists and we have permission to change it
	e->env_status = status; // if we do, set the status to the status argument
	return 0; // return zero for success
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
	struct Env *e = NULL;
	if(envid2env(envid, &e, 1) < 0) return -E_BAD_ENV; // check to see if the environment exists and we have permission to change it
	e->env_pgfault_upcall = func; // if we do, set the page fault upcall function to the argument func
	return 0; // return zero for success
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	if((perm & 0xfff) & (~PTE_SYSCALL)) return -E_INVAL; // check to see if the perm argument is appropriate, thanks yeongjin
	if((uintptr_t)va >= UTOP || (uintptr_t)va & 0xfff) return -E_INVAL; // check to see if the address is >= UTOP or not pag-aligned, thanks yeongjin

	struct Env *e = NULL;
	if(envid2env(envid, &e, 1) < 0) return -E_BAD_ENV; // check to see if the environment exists and we have permission to edit it

	struct PageInfo *pp = page_alloc(ALLOC_ZERO); // allocate a new page for the environment
	if(pp == NULL) return -E_NO_MEM; // check to see if we the page_alloc failed
	if(page_insert(e->env_pgdir, pp, va, perm) < 0){ // if we didn't fail, try to insert the page into the enviroment at va with permisions perm
		page_free(pp); // if we did fail, make to free the page before returning the error!
		return -E_NO_MEM;
	}
	return 0; // otherwise return zero for success
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	if((perm & 0xfff) & (~PTE_SYSCALL)) return -E_INVAL; // check to see if the perm is valid, thanks yeongjin
	if((uintptr_t)srcva >= UTOP || (uintptr_t)srcva & 0xfff) return -E_INVAL; // check to see if the srcva is >= UTOP or if it is not page aligned, thanks yeongjin
	if((uintptr_t)dstva >= UTOP || (uintptr_t)dstva & 0xfff) return -E_INVAL; // check to see if the dstva is >= UTOP or if it is not page aligned, thanks yeongjin


	struct Env *src_e = NULL, *dst_e = NULL;
	if(envid2env(srcenvid, &src_e, 1) < 0) return -E_BAD_ENV; // check to see if the src environment exists and we have permissions to change it
	if(envid2env(dstenvid, &dst_e, 1) < 0) return -E_BAD_ENV; // chekc to see if the dst environment exists and we have permissions to change it

	pte_t *p_pte;
	struct PageInfo *pp = page_lookup(src_e->env_pgdir, srcva, &p_pte); // lookup the page in the src environment at the given va, store permissions in p_ptes
	if(pp == NULL) return -E_INVAL; // check to see if the page was found

	if((perm & PTE_W) && !(*p_pte & PTE_W)){ // check to see if we have invalid permissions (defined above)
		return -E_INVAL;
	}

	if(page_insert(dst_e->env_pgdir, pp, dstva, perm) < 0){ // finally, try inserting the page into the dst enviornment at dstva with given permissions
		return -E_NO_MEM; // if we failed, return an error, no need to free the memory this time around.
	}
	return 0; // otherwise, return zero for success
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	if((uintptr_t)va >= UTOP || (uintptr_t)va & 0xfff) return -E_INVAL; // check to see if the address is >= than UTOP or not page aligned, thanks yeongjin

	struct Env *e;
	if(envid2env(envid, &e, 1) < 0) return -E_BAD_ENV; // check to see if the environment exists and we have permissions to change it

	page_remove(e->env_pgdir, va); // try to remove the page in the enviornment at va
	return 0; // return zero for success
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env *e = NULL;
	if(envid2env(envid, &e, 0) < 0) return -E_BAD_ENV; // check if env actually exists, don't need to check permissions this time
	if(e->env_ipc_recving == 0) return -E_IPC_NOT_RECV; // check if we they are blocking or not

	if((uintptr_t)srcva < UTOP){ // check to see if the srcva is < UTOP, if so, it means we need to insert a page into the child env
		if((uintptr_t)srcva & 0xfff) return -E_INVAL; // check if srcva is not page-aligned, thanks yeongjin
		if((perm & 0xfff) & (~PTE_SYSCALL)) return -E_INVAL; // check if perm is inappropriate, thanks yeongin

		pte_t *p_pte;
		struct PageInfo *pp = page_lookup(curenv->env_pgdir, srcva, &p_pte); // lookup the page in the src environment at the given va, store permissions in p_ptes
		if(pp == NULL) return -E_INVAL; // check to see if the page was found

		// check if srcva is readonly but perm is write (perm & PTE_W)
		if((perm & PTE_W) && !(*p_pte & PTE_W)) return -E_INVAL;

		// map into environment address space and check if it's successful
		if(page_insert(e->env_pgdir, pp, e->env_ipc_dstva, perm) < 0) return -E_NO_MEM;

		// set the permission of the environment
		e->env_ipc_perm = perm;
	}
	else{ // otherwise, we can just set the permissions to zero
		e->env_ipc_perm = 0;
	}

	// set necessary values
	e->env_ipc_recving = 0;
	e->env_ipc_value = value;
	e->env_ipc_from = curenv->env_id;
	e->env_status = ENV_RUNNABLE;
	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	if((uintptr_t)dstva < UTOP && (uintptr_t)dstva & 0xfff) return -E_INVAL; // check if dstva is < UTOP and not page-aligned, thanks yeongjin

	// otherwise, set necessary values
	curenv->env_ipc_recving = 1;
	curenv->env_status = ENV_NOT_RUNNABLE;
	curenv->env_ipc_dstva = dstva;
	curenv->env_tf.tf_regs.reg_eax = 0; // need to make sure this is set before yielding !
	sched_yield(); // have to yield

	return 0;
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	switch (syscallno) { // switch case statement for the different system calls available
		case SYS_yield:
		{
			sys_yield();
			return 0;
		}
		case SYS_exofork:
		{
			envid_t envid = sys_exofork(); // fork the current environment, returns the new environment id
			return envid;
		}
		case SYS_env_set_status:
		{
			int ret = sys_env_set_status((const envid_t) a1, a2); // set the environment's status, return success or not
			return ret;
		}
		case SYS_page_alloc:
		{
			int ret = sys_page_alloc((const envid_t)a1, (void *)a2, a3); // allocate a page into the environment, returns success or not
			return ret;
		}
		case SYS_page_map:
		{
			int ret = sys_page_map((const envid_t)a1, (void *)a2, (const envid_t)a3, (void *)a4, a5); // map an environment address to another, returns success or not
			return ret;
		}
		case SYS_page_unmap:
		{
			int ret = sys_page_unmap((const envid_t)a1, (void *)a2); // unmap a page at an environment address, returns success or not
			return ret;
		}
		case SYS_env_set_pgfault_upcall:
		{
			int ret = sys_env_set_pgfault_upcall((const envid_t)a1, (void *)a2); // set the pgfault upcall for an environment, returns success or not
			return ret;
		}
		case SYS_ipc_try_send:
		{
			int ret = sys_ipc_try_send((const envid_t)a1, a2, (void *)a3, (unsigned)a4); // try to send a value, returns success or not
			return ret;
		}
		case SYS_ipc_recv:
		{
			int ret = sys_ipc_recv((void *)a1); // receive a value, returns success or not
			return ret;
		}
		case SYS_cputs:
		{
			sys_cputs((const char *)a1, (size_t)a2); // printing, returns nothing
			return 0;
		}
		case SYS_cgetc:
		{
			int ret = sys_cgetc(); // getting input, need to return the int value
			return ret;
		}
		case SYS_getenvid:
		{
			envid_t envid = sys_getenvid(); // getting environment id, need to return the env id
			return envid;
		}
		case SYS_env_destroy:
		{
			int ret = sys_env_destroy((const envid_t)a1); // destorying the environment, need to return if successful?
			return ret;
		}
	default:
		return -E_INVAL;
	}
}
