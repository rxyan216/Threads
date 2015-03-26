/*	User-level thread system
 *
 */

#include <setjmp.h>

#include "aux.h"
#include "umix.h"
#include "mythreads.h"
#include "string.h"

static int MyInitThreadsCalled = 0;	/* 1 if MyInitThreads called, else 0 */

static struct thread {			/* thread table */
	int valid;			/* 1 if entry is valid, else 0 */
	jmp_buf env;			/* current context */
        jmp_buf init;
        void (*f)();
        int p;
} thread[MAXTHREADS];

static struct queue {
        int valid;
        int tid;
        int next;
} queue[MAXTHREADS];

static int count;
static int head;
static int prevThread;
static int currThread;
static int find;

#define STACKSIZE	65536		/* maximum size of thread stack */

/*	MyInitThreads () initializes the thread package. Must be the first
 *	function called by any user program that uses the thread package.  
 */

void MyInitThreads ()
{
	int i;
        int record;

	if (MyInitThreadsCalled) {                /* run only once */
		Printf ("InitThreads: should be called only once\n");
		Exit ();
	}

	for (i = 0; i < MAXTHREADS; i++) {	/* initialize thread table */
		thread[i].valid = 0;
                queue[i].valid = 0;
                queue[i].next = -1;
	}

        if ((record = setjmp (thread[0].init)) != 0) {
                currThread = record - 1;
                (*(thread[currThread].f)) (thread[currThread].p);
                MyExitThread();
        }

        for (i = 1; i < MAXTHREADS; i++) {
                char s[i * STACKSIZE];

                if (((int) &s[i * STACKSIZE - 1]) - ((int) &s[0]) + 1 != i * STACKSIZE) {
                        Printf("Stack space reservation failed\n");
                        Exit();
                }

                if ((record = setjmp (thread[i].init)) != 0) {
                        currThread = record - 1;
                        (*(thread[currThread].f)) (thread[currThread].p);
                        MyExitThread ();
                }
        }

	thread[0].valid = 1;			/* initialize thread 0 */
        memcpy (thread[0].env, thread[0].init, sizeof (jmp_buf));

        count = 1;
        head = 0;
        currThread = 0;
        find = 0;

	MyInitThreadsCalled = 1;
}

/*	MySpawnThread (func, param) spawns a new thread to execute
 *	func (param), where func is a function with no return value and
 *	param is an integer parameter.  The new thread does not begin
 *	executing until another thread yields to it.  
 */

int MySpawnThread (func, param)
	void (*func)();		/* function to be executed */
	int param;		/* integer parameter */
{
        int i;
        int curr;

	if (! MyInitThreadsCalled) {
		Printf ("SpawnThread: Must call InitThreads first\n");
		Exit ();
	}

        if (count == MAXTHREADS) {
                return (-1);
        }

        for (i = 0; i < MAXTHREADS; i++) {
                find = (find + 1) % MAXTHREADS;
                if (! thread[find].valid) break;
        }

        thread[find].valid = 1;
        memcpy (thread[find].env, thread[find].init, sizeof (jmp_buf));
        thread[find].f = func;
        thread[find].p = param;

        if (count == 1) {
                queue[head].valid = 1;
                queue[head].tid = find;
        }
        else {
                curr = head;

                while (queue[curr].next != -1) {
                        curr = queue[curr].next;
                }

                for (i = 0; i < MAXTHREADS; i++) {
                        if (! queue[i].valid) break;
                }

                queue[i].valid = 1;
                queue[i].tid = find;

                queue[curr].next = i;
        }

        count++;

	return (find);		/* done spawning, return new thread ID */
}

/*	MyYieldThread (t) causes the running thread, call it T, to yield to
 *	thread t.  Returns the ID of the thread that yielded to the calling
 *	thread T, or -1 if t is an invalid ID.  Example: given two threads
 *	with IDs 1 and 2, if thread 1 calls MyYieldThread (2), then thread 2
 *	will resume, and if thread 2 then calls MyYieldThread (1), thread 1
 *	will resume by returning from its call to MyYieldThread (2), which
 *	will return the value 2.
 */

int MyYieldThread (t)
	int t;				/* thread being yielded to */
{
        int i;
        int curr;
        int record;

	if (! MyInitThreadsCalled) {
		Printf ("YieldThread: Must call InitThreads first\n");
		Exit ();
	}

	if (t < 0 || t >= MAXTHREADS) {
		Printf ("YieldThread: %d is not a valid thread ID\n", t);
		return (-1);
	}
	if (! thread[t].valid) {
		Printf ("YieldThread: Thread %d does not exist\n", t);
		return (-1);
	}

        if (thread[currThread].valid) {
                curr = head;

                while (queue[curr].next != -1) {
                        curr = queue[curr].next;
                }

                for (i = 0; i < MAXTHREADS; i++) {
                        if (! queue[i].valid) break;
                }

                queue[i].valid = 1;
                queue[i].tid = currThread;

                queue[curr].next = i;
        }

        if (! thread[currThread].valid && count == 1) {
                queue[head].valid = 0;
                queue[head].next = -1;

                head = 0;
        }
        else {
                for (i = 0; i < MAXTHREADS; i++) {
                        if (queue[i].valid && queue[i].tid == t) break;
                }

                curr = head;

                if (curr == i) {
                        head = queue[i].next;

                        queue[i].valid = 0;
                        queue[i].next = -1;
                }
                else {
                        while (queue[curr].next != i) {
                                curr = queue[curr].next;
                        }

                        queue[curr].next = queue[i].next;

                        queue[i].valid = 0;
                        queue[i].next = -1;
                }
        }

        prevThread = currThread;

        if ((record = setjmp (thread[prevThread].env)) == 0) {
                longjmp (thread[t].env, t + 1);
        }

        currThread = record - 1;

        return (prevThread);
}

/*	MyGetThread () returns ID of currently running thread. 
 */

int MyGetThread ()
{
	if (! MyInitThreadsCalled) {
		Printf ("GetThread: Must call InitThreads first\n");
		Exit ();
	}

        return (currThread);
}

/*	MySchedThread () causes the running thread to simply give up the
 *	CPU and allow another thread to be scheduled.  Selecting which
 *	thread to run is determined here. Note that the same thread may
 * 	be chosen (as will be the case if there are no other threads).  
 */

void MySchedThread ()
{
        int i;
        int num = 0;

	if (! MyInitThreadsCalled) {
		Printf ("SchedThread: Must call InitThreads first\n");
		Exit ();
	}

        for (i = 0; i < MAXTHREADS; i++) {
                if (queue[i].valid) num++;
        }

        if (num != 0) {
                MyYieldThread (queue[head].tid);
        }
}

/*	MyExitThread () causes the currently running thread to exit.  
 */

void MyExitThread ()
{
	if (! MyInitThreadsCalled) {
		Printf ("ExitThread: Must call InitThreads first\n");
		Exit ();
	}

        if (count == 1) {
                Exit();
        }

        thread[currThread].valid = 0;

        count--;

        MySchedThread();
}
