/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	A thread waiting for the private rw_lock whose wait is interrupted must
	still get the lock when it is released. It once spun in rw_lock_wait()
	and kept the lock. haiku-pi 2bf2f773ba.
*/


#include <signal.h>
#include <stdio.h>

#include <OS.h>

#include <locks.h>


static rw_lock sLock;
static int32 sAcquired;


static status_t
writer(void*)
{
	status_t status = rw_lock_write_lock(&sLock);
	atomic_set(&sAcquired, status == B_OK ? 1 : -1);
	if (status == B_OK)
		rw_lock_write_unlock(&sLock);
	return status;
}


static status_t
exit_at_once(void*)
{
	return B_OK;
}


static void
ignore_signal(int)
{
}


static bool
test(const char* name, bool signalWhileBlocked)
{
	rw_lock_init(&sLock, name);
	atomic_set(&sAcquired, 0);
	rw_lock_write_lock(&sLock);

	thread_id thread = spawn_thread(writer, "rw lock writer",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(thread);
	snooze(100000);
	if (signalWhileBlocked)
		send_signal(thread, SIGUSR1);
	snooze(100000);
	rw_lock_write_unlock(&sLock);

	for (int i = 0; i < 200 && atomic_get(&sAcquired) == 0; i++)
		snooze(10000);

	bool passed = atomic_get(&sAcquired) == 1;
	printf("%s: %s\n", name,
		passed ? "PASS" : "FAIL (the writer never got the lock)");
	if (passed) {
		status_t status;
		wait_for_thread(thread, &status);
	} else
		kill_thread(thread);
	return passed;
}


int
main()
{
	signal(SIGUSR1, ignore_signal);

	thread_id thread = spawn_thread(exit_at_once, "exits at once",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(thread);
	status_t status;
	wait_for_thread(thread, &status);

	bool passed = test("interrupted by the thread's start", false);
	passed &= test("interrupted by a signal", true);
	return passed ? 0 : 1;
}
