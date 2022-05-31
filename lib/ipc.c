// User-level IPC library routines

#include <inc/lib.h>

// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'from_env_store' is nonnull, then store the IPC sender's envid in
//	*from_env_store.
// If 'perm_store' is nonnull, then store the IPC sender's page permission
//	in *perm_store (this is nonzero iff a page was successfully
//	transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
// Otherwise, return the value sent by the sender
//
// Hint:
//   Use 'thisenv' to discover the value and who sent it.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value, since that's
//   a perfectly valid place to map a page.)
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	// LAB 4: Your code here.
	if(pg == NULL) pg = (void *)KERNBASE; // if the page is null, lets set the address to KERNBASE so we know not to try to map a page
	int result = sys_ipc_recv(pg); // get the result of receive
	if(result == 0){ // if it is zero it means success
		if(from_env_store != NULL) *from_env_store = thisenv->env_ipc_from; // if from_env_store is not NULL, means we sent a page. Get from who
		if(perm_store != NULL) *perm_store = thisenv->env_ipc_perm; // if perm_store is not NULL, means we sent a page. Get the permissions of it
		return thisenv->env_ipc_value; // return the value we received, regardless if there was a page sent or not
	}
	else {
		if(from_env_store != NULL) *from_env_store = 0; // if from_env_store isn't null, set to zero (described above)
		if(perm_store != NULL) *perm_store = 0; // if perm_store isn't null, set to zero (described above)
		return result; // return the error result
	}
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
//
// Hint:
//   Use sys_yield() to be CPU-friendly.
//   If 'pg' is null, pass sys_ipc_try_send a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	// LAB 4: Your code here.
	if(pg == NULL) pg = (void *)KERNBASE; // if the page is null, lets set the address to KERNBASE so we know not to try to map a page
	int succ = 0, result;
	while(succ == 0){ // while loop to keep trying to send
		result = sys_ipc_try_send(to_env, val, pg, perm); // get the result of send attempt
		if(result == 0) succ = 1; // if it is zero, let's break from the loop
		else if(result == -E_IPC_NOT_RECV) sys_yield(); // if they are not waiting to receive, yield are time
		else panic("Failed to send!"); // otherwise, error means we should panic attack time
	}
	return; // no return value necessary
}

// Find the first environment of the given type.  We'll use this to
// find special environments.
// Returns 0 if no such environment exists.
envid_t
ipc_find_env(enum EnvType type)
{
	int i;
	for (i = 0; i < NENV; i++)
		if (envs[i].env_type == type)
			return envs[i].env_id;
	return 0;
}
