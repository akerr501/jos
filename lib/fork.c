// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va; // get the address of the the fault
	addr = (void *)ROUNDDOWN(addr, PGSIZE); // round down to the closest page
	uint32_t err = utf->utf_err; // get the error
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	// check if the attempt was not writing and COW page
	if((err & FEC_WR) == 0 || (uvpt[PGNUM(addr)] & (PTE_COW | PTE_P)) == 0 || (uvpd[PDX(addr)] & PTE_P) == 0) { // if so, panic attack time
		panic("Not a proper copy on write attempt!");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	// allocate a new page at PFTEMP, with write permissions for the user
	r = sys_page_alloc(0, (void *)PFTEMP, (PTE_U | PTE_P | PTE_W)); // first system call
	if(r < 0) panic("Failed to allocate new page!"); // check to see if that failed, panic attack time
	// copy the data at the address to PFTEMP
	memcpy((void *)PFTEMP, addr, PGSIZE);
	// map the data from addr to PFTEMP
	r = sys_page_map(0, (void *)PFTEMP, 0, addr, (PTE_U | PTE_P | PTE_W)); // second system call
	if(r < 0) panic("Failed to map page!"); // check to see if that failed, panic attack time
	// unmap page at PFTEMP
	r = sys_page_unmap(0, (void *)PFTEMP); // third system call
	if(r < 0) panic("Failed to unmap page!"); // check to see if that failed, panic attack time
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	envid_t p_envid = 0;
	void *p_addr = (void *)(pn*PGSIZE);
	// LAB 4: Your code here.
	// check first if COW or writable is set
	if((uvpt[pn] & (PTE_COW | PTE_W)) == 0){ // if it isn't, map as read only
		r = sys_page_map(p_envid, p_addr, envid, p_addr, (PTE_P | PTE_U)); // map child to be only readable
		if(r < 0) return -1; // check if that failed
	}
	else{ // otherwise let's set it to be COW for both child and parent
		// map the child first
		r = sys_page_map(p_envid, p_addr, envid, p_addr, (PTE_COW | PTE_P | PTE_U));
		if(r < 0) return -1; // check if that failed
		// then map the parent
		r = sys_page_map(p_envid, p_addr, p_envid, p_addr, (PTE_COW | PTE_P | PTE_U));
		if(r < 0) return -1; // check if that failed
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	// for child page fault handling
	extern void _pgfault_upcall(void);
	envid_t envid;
	uint32_t addr;
	int r;

	// step 1, let's make sure the page fault handler is setup
	set_pgfault_handler(pgfault);

	// step 2, let's fork using sys_exofork. Just copied from dumb fork
	// Allocate a new child environment.
	// The kernel will initialize it with a copy of our register state,
	// so that the child will appear to have called sys_exofork() too -
	// except that in the child, this "fake" call to sys_exofork()
	// will return 0 instead of the envid of the child.
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// step three, time to remap things
	for (addr = UTEXT; addr < UTOP; addr += PGSIZE){ // for each page from UTEXT to UTOP
		// if the address is valid (not at UXSTACKTOP - PGSIZE) and we have appropriate permissions, map time
		if(addr != UXSTACKTOP - PGSIZE && (uvpd[PDX(addr)] & PTE_P) != 0 && (uvpt[PGNUM(addr)] & (PTE_P | PTE_U)) != 0){
			r = duppage(envid, addr / PGSIZE); // use the duppage function we've implemented
			if(r < 0) panic("Failed to duppage!"); // check if that failed, panic attack time
		}
		else{
			// handle something here ? do nothing ? it doesns't blow up so I guess this is fine???
		}
	}

	// step three and a half, need to alloc a new page for the child's exception stack
	// address needs to be at UXSTACKTOP - PGSIZE
	r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), (PTE_W | PTE_U | PTE_P));
	if(r < 0) panic("Failed to allocate a page for UXSTACKTOP!"); // check if the page_alloc failed, panic attack time

	// step four, parent sets the user page fault entrypoint for the child to look like its own
	r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if(r < 0) panic("Failed to setup environment page fault upcall"); // check if the set function failed, panic attack time

	// step five, everything above is setup, let's mark the child as runnable. Copied from dumbfork
	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
	return envid; // if we have made it this far, fork as succeded! return the envid
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
