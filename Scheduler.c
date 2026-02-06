////////////////////////////////////////////////////////////////////
// CYBV 489
// SP 2026
// Dean Lewis
// Scheduler.c
////////////////////////////////////////////////////////////////////

#define _CRT_SECURE_NO_WARNINGS
// STANDARD LIBRARIES
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
// THREADS HELPER FILES
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

// DECLARATIONS ////////////////////////////////////////////////////
Process* runningProcess = NULL;
int debugFlag = 0;  // 0 = no debug output, 1 = debug output
#define TIME_SLICE_MS 80
// END DECLARATIONS ////////////////////////////////////////////////

// One join semaphore per process-table slot (pid % MAXPROC)
static ksem_t joinSem[MAXPROC];

// FUNCTION PROTOTYPES /////////////////////////////////////////////
static int watchdog(char*);
void dispatcher();
static int launch(void*);
static void check_deadlock();
static void DebugConsole(char* format, ...);
static inline void disableInterrupts();
static inline void enableInterrupts();
static void timer_interrupt_handler(char deviceId[32], uint8_t command, uint32_t status);
static void require_kernel_mode(void);
// Semaphore functions (Test15 Add)
static void ksem_wait(ksem_t* s);
static void ksem_broadcast(ksem_t* s);
static void ksem_init(ksem_t* s, int initialCount);
// END FUNCTION  PROTOTYPES ////////////////////////////////////////

///* DO NOT REMOVE FOLLOWING *//////////////////////////////////////
extern int SchedulerEntryPoint(void* pArgs);
int check_io_scheduler();
check_io_function check_io;
////////////////////////////////////////////////////////////////////


/*************************************************************************
   bootstrap()

   Purpose - This is the first function called by THREADS on startup.

             The function must setup the OS scheduler and primitive
             functionality and then spawn the first two processes.

             The first two process are the watchdog process
             and the startup process SchedulerEntryPoint.

             The statup process is used to initialize additional layers
             of the OS.  It is also used for testing the scheduler
             functions.

   Parameters - Arguments *pArgs - these arguments are unused at this time.

   Returns - The function does not return!

   Side Effects - The effects of this function is the launching of the kernel.

 *************************************************************************/
int bootstrap(void* pArgs)
{
    // value returned by call to spawn()
    int result;

    // Set this to the scheduler version of this function.
    check_io = check_io_scheduler;

    // Initialize ProcessTable. Moved to Processes.c
    processes_init();

    // Test15 Add
    // Initialize join semaphores (one per slot)
    for (int i = 0; i < MAXPROC; i++)
    {
        ksem_init(&joinSem[i], 0);
    }

    // Test05 Add
    //////////// INTERRUPT HANDLER ///////////
    // Invoke Interrupt Handler Timer
    interrupt_handler_t* handlers = get_interrupt_handlers();

    // Set all interrupt handlers to NULL
    for (int i = 0; i < THREADS_INTERRUPT_HANDLER_COUNT; i++)
    {
        handlers[i] = NULL;
    }

    // Set ONLY the timer interrupt handler
    handlers[THREADS_TIMER_INTERRUPT] = timer_interrupt_handler;
    //////////////////////////////////////////

    // SPAWN watchdog process
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Spawn test parent process, which is the main for each test program.
    // Executes SchedulerEntryPoint
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Performs next context switching
    dispatcher();

    /* This should never return since we are not a real process. */
    stop(-3);
    return 0;
}

/*************************************************************************
   k_spawn()

   Purpose - spawns a new process.

             Finds an empty entry in the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.

   Parameters - the process's entry point function, the stack size, and
                the process's priority.

   Returns - The Process ID (pid) of the new child process
             The function must return if the process cannot be created.

************************************************************************ */
int k_spawn(char* name, int (*entryPoint)(void*), void* arg, int stacksize, int priority)
{
    // Test kernel mode
    // Test11 Add
    require_kernel_mode();

    Process* pNewProc = NULL;

    DebugConsole("spawn(): creating process %s\n", (name ? name : "(null)"));

    disableInterrupts();

    //////////// SAFETY CHECKS ////////////
    if (name == NULL)
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        enableInterrupts();
        return -1;
    }

    if (strlen(name) >= (MAXNAME - 1))
    {
        console_output(debugFlag, "spawn(): Process name is too long. Halting...\n");
        enableInterrupts();
        return -1;
    }

    if (entryPoint == NULL)
    {
        console_output(debugFlag, "spawn(): entryPoint is NULL.\n");
        enableInterrupts();
        return -1;
    }

    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(debugFlag, "spawn(): invalid priority value %d.\n", priority);
        enableInterrupts();
        return -1;
    }
    //////////// SAFETY CHECKS ////////////

    // Test 18 Add - Correct stack size error handling
    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
		console_output(FALSE, "spawn(): stacksize too small.\n"); // Set to FALSE to avoid debug flag Test18 Add
        enableInterrupts();
		return -2;  // Return -2 Test18 Add
    }

    //////////// PID/SLOT ALLOCATION (Professor rule) ////////////
    // Find a PID such that pid % MAXPROC refers to an EMPTY process table slot.
    int slot = -1;
    int attempts = 0;

    while (attempts < MAXPROC * 4)  // simple safety bound against infinite loops
    {
        int candidateSlot = nextPid % MAXPROC;

        // Slot is free?
        if (processTable[candidateSlot].status == PROCSTATE_EMPTY)
        {
            slot = candidateSlot;
            break;
        }

        nextPid++;
        attempts++;
    }

    // No slot available => process table full
    if (slot < 0)
    {
        enableInterrupts();
        return -1; // or a more specific "no more processes" code if your spec defines it
    }

    pNewProc = &processTable[slot];

    // Initialize the process table entry
    memset(pNewProc, 0, sizeof(Process));
    strcpy(pNewProc->name, name);

    // Assign PID that maps to the slot
    pNewProc->pid = nextPid;
    nextPid++;  // move to the next candidate for the next spawn

    // Test15 Add
    // Reset the join semaphore for this slot (fresh process)
    ksem_init(&joinSem[slot], 0);

    // Save process initial base values
    pNewProc->priority = priority;
    pNewProc->entryPoint = entryPoint;
    pNewProc->status = PROCSTATE_READY;
    pNewProc->stacksize = (unsigned int)stacksize;

    // Parent/child process management
    if (runningProcess != NULL)
    {
        process_add_child(runningProcess, pNewProc);
        runningProcess->numChildren++;
    }

    DebugConsole("k_spawn(): pid=%d slot=%d name='%s' argPtr=%p argStr='%s'\n",
        pNewProc->pid, slot, pNewProc->name, arg, (arg ? (char*)arg : "(null)"));

    // Initialize context
    pNewProc->context = context_initialize(launch, stacksize, arg);

    // Add to ready queue
    add_ready_process(pNewProc);

    // Preempt if new process is higher priority than currently running
    int preempt = (runningProcess != NULL) &&
        (runningProcess->status == PROCSTATE_RUNNING) &&
        (pNewProc->priority > runningProcess->priority);

    enableInterrupts();

    if (preempt)
    {
        dispatcher();
    }

    return pNewProc->pid;
}

/**************************************************************************
   Name - launch

   Purpose - Utility function that makes sure the environment is ready,
             such as enabling interrupts, for the new process.

   Parameters - none

   Returns - nothing
*************************************************************************/
static int launch(void* args)
{
    (void)args;  // ignore whatever the context library passed us

    DebugConsole("launch(): started: %s\n", runningProcess->name);

    enableInterrupts();

    int result = 0;
    // check on runningProcess entry
    if (runningProcess && runningProcess->entryPoint)
    {
        // pass process name as argument
        result = runningProcess->entryPoint(runningProcess->name);
    }

    k_exit(result);
    return 0;
}

/**************************************************************************
   Name - k_wait

   Purpose - Wait for a child process to quit.  Return right away if
             a child has already quit.

   Parameters - Output parameter for the child's exit code.

   Returns - the pid of the quitting child, or
        -4 if the process has no children
        -5 if the process was signaled in the join

************************************************************************ */
int k_wait(int* code)
{
    // Test kernel mode
    // Test11 Add
    require_kernel_mode();

    disableInterrupts();

    // check if current process is null and return -4
    if (runningProcess == NULL)
    {
        enableInterrupts();
        //return -4;
		return -1;   // Test19 Change
    }
    if (runningProcess->pChildren == NULL)
    {
        enableInterrupts();
        //return -4;
        return -1;   // Test19 Change
    }

    /* block until a child quits  Test13 Add */
    runningProcess->status = PROCSTATE_BLOCKED;
    runningProcess->blockReason = BLOCK_WAIT;

    while (1)
    {
        Process* prev = NULL;

        // See if any child has already terminated
        Process* termchild = process_find_term_child(runningProcess, &prev);

        if (termchild != NULL)
        {
            int pid = termchild->pid;
            int slot = (int)(termchild - processTable);

            if (code)
                *code = exitCodeSlot[slot];

            // Remove from parent's child list
            remove_term_child(runningProcess, termchild, prev);

            // Reclaim the process table entry (so old PIDs don't linger into later dumps)
            // memset(&processTable[slot], 0, sizeof(Process));
            // Changed for Test06 and Test15
            processTable[slot].status = PROCSTATE_EMPTY;
            processTable[slot].context = NULL;    
            processTable[slot].pChildren = NULL;  
            processTable[slot].pParent = NULL;     
            // exitCodeSlot[slot] = 0;

            enableInterrupts();
            return pid;
        }

        // Set block status and WAIT type Test13 Add
        runningProcess->status = PROCSTATE_BLOCKED;
        runningProcess->blockReason = BLOCK_WAIT;

        enableInterrupts();
        dispatcher();
        disableInterrupts();

        // When we resume here, we are running again
        if (runningProcess != NULL)
            runningProcess->status = PROCSTATE_RUNNING;
    }
}

/**************************************************************************
   Name - k_exit

   Purpose - Exits a process and coordinates with the parent for cleanup
             and return of the exit code.

   Parameters - the code to return to the grieving parent

   Returns - nothing

*************************************************************************/
void k_exit(int exitcode)
{
    // Test kernel mode
    // Test11 Add
    require_kernel_mode();

    disableInterrupts();

    Process* _proclocal = runningProcess;

    if (_proclocal == NULL)
    {
        enableInterrupts();
        stop(exitcode);
        return;
    }

    // Test09 add - if active children - disallow exit call and HALT
    // parent spawns a child
    // parent prints “after spawn…”
    // parent calls quit() / k_exit()
    // Halt
    if (_proclocal->pChildren != NULL)
    {
        Process* _proclocalchild = _proclocal->pChildren;
        while (_proclocalchild != NULL)
        {
            if (_proclocalchild->status != PROCSTATE_TERMINATE && _proclocalchild->status != PROCSTATE_EMPTY)
            {
                // reenable to normal bevavior
                enableInterrupts();

                // Print expected output
                console_output(FALSE, "quit(): Process with active children attempting to quit\n");
                stop(1);    // prints "THREADS Halted: Error code 1."
                return;
            }
            // Check next child
            _proclocalchild = _proclocalchild->nextSiblingProcess;
        }
    }

    int mySlot = (int)(_proclocal - processTable);

    // Test03 Add
    if (_proclocal->receivedSignal)
    {
        exitCodeSlot[mySlot] = -5;  // Kill exit code
    }
    else
    {
        exitCodeSlot[mySlot] = exitcode;
    }

    // Update cpu time before terminating
    DWORD now = read_clock();
    _proclocal->cpuTime += (now - _proclocal->lastStartTime);

    _proclocal->status = PROCSTATE_TERMINATE;

    // Test15 Add
    // Wake any processes blocked on JOIN (replaces blockedPid scan loop logic)
    ksem_broadcast(&joinSem[mySlot]);

    /* Wake parent if it is waiting */
    if (_proclocal->pParent != NULL && _proclocal->pParent->status == PROCSTATE_BLOCKED)
    {
        _proclocal->pParent->status = PROCSTATE_READY;
        // Add to ready list
        add_ready_process(_proclocal->pParent);
    }

    /* If no parent, end the system */
    if (_proclocal->pParent == NULL)
    {
        enableInterrupts();

        // Print final complete statement
        console_output(FALSE, "All processes completed.\n");

        stop(exitcode);
        return;
    }

    enableInterrupts();
    dispatcher();
}

/**************************************************************************
   Name - k_kill

   Purpose - Signals a process with the specified signal

   Parameters - Signal to send

   Returns -
*************************************************************************/
int k_kill(int pid, int signal)
{
    // Test kernel mode
    // Test11 Add
    require_kernel_mode();

    int result = 0;

    disableInterrupts();

    Process* _proclocal = find_process_by_pid(pid);

    if (_proclocal == NULL)
    {
        enableInterrupts();
        return 0;
    }

    // Check is signal received
    if (signal == SIG_TERM)
    {
        // Set process table signal to true
        _proclocal->receivedSignal = 1;

        // Test06 Add
        // Check blocked and move to ready
        if (_proclocal->status == PROCSTATE_BLOCKED)
        {
            _proclocal->status = PROCSTATE_READY;
            add_ready_process(_proclocal);
        }
    }

    /* Minimal for now; scheduler tests later will define behavior. */
    (void)signal;

    enableInterrupts();

    return result;
}

/**************************************************************************
   Name - dispatcher

   Purpose - This is where context changes to the next process to run.

   Parameters - none

   Returns - nothing

*************************************************************************/
void dispatcher()
{
    Process* nextProcess = NULL;

    disableInterrupts();

    // YOU CAN SEE THE FOLLOWING AT ~1:25 IN THE VIDEO LECTURE
    // Validate logic: Needs to be highest priority process in ready list
    // ******At or higher than current process priority

    // Check the ready list for next priority ready process
    nextProcess = next_ready_process();

    // No new ready process found continue running current process
    if (nextProcess == NULL)
    {
        // Nothing else ready, keep current runningProcess running Test19 Add
        if (runningProcess != NULL)
        {
            runningProcess->status = PROCSTATE_RUNNING;
            runningProcess->blockReason = BLOCK_NONE;
        }
        enableInterrupts();
        return;
    }

	// If dispatcher selected the current running process do nothing - Test19 Add
    if (nextProcess == runningProcess)
    {
        runningProcess->status = PROCSTATE_RUNNING;
        runningProcess->blockReason = BLOCK_NONE;
        enableInterrupts();
        return;
    }

    /* If the currently running process is still runnable, put it back on ready */
    if (runningProcess != NULL && runningProcess->status == PROCSTATE_RUNNING)
    {
        runningProcess->status = PROCSTATE_READY;
        add_ready_process(runningProcess);
    }

    // If the current running process is not null and a new process switch is coming update its cpu time
    if (runningProcess != NULL && nextProcess != NULL) {
        // Added to calculate cpu time when process is switched out
        DWORD now = read_clock();
        runningProcess->cpuTime += (now - runningProcess->lastStartTime);
    }

    // Switch to the next process and block reason
    runningProcess = nextProcess;
    runningProcess->status = PROCSTATE_RUNNING;
    runningProcess->blockReason = BLOCK_NONE;

    // start cpu running clock time
    runningProcess->lastStartTime = read_clock();

    enableInterrupts();

    /* IMPORTANT: context switch enables interrupts. */
    context_switch(nextProcess->context);

}

/**************************************************************************
   Name - watchdog

   Purpose - The watchdoog keeps the system going when all other
         processes are blocked.  It can be used to detect when the system
         is shutting down as well as when a deadlock condition arises.

   Parameters - none

   Returns - nothing
   *************************************************************************/
static int watchdog(char* dummy)
{
    // Comment following line to correct output
    DebugConsole("watchdog(): called\n");

    while (1)
    {
        check_deadlock();
        dispatcher();		//POSSIBLY NOT NEEDED HERE
    }
    return 0;
}

// Enables the interrupts
static inline void enableInterrupts()
{
    /* We ARE in kernel mode */

    int psr = get_psr();
    psr = psr | PSR_INTERRUPTS;         // bitwise OR to set the interrupt bit
    set_psr(psr);
}

// Disables the interrupts
static inline void disableInterrupts()
{
    /* We ARE in kernel mode */

    int psr = get_psr();
    psr = psr & ~PSR_INTERRUPTS;        // bitwise AND with NOT to clear the interrupt bit
    set_psr(psr);

}

// Displays current process struct values
void display_process_table()
{
    // IN DEVELOMENT TEST04 - NOT COMPELTE

    // Print out header pro process table
    console_output(FALSE, "PID\tParent\tPriority\tStatus\t\t# Kids\tCPUtime\tName\n");

    // Cycle through process table printing off each row
    for (int i = 0; i < MAXPROC; i++)   // CHANGING to 0 to MAXPROC for testing
    {
        Process* _proclocal = &processTable[i];

        // If process state == empty, continue to next row
        if (_proclocal->status == PROCSTATE_EMPTY)
            continue;

        const char* stateStr = "UNKNOWN";
        switch (_proclocal->status)
        {
        case PROCSTATE_READY:     stateStr = "READY";     break;
        case PROCSTATE_RUNNING:   stateStr = "RUNNING";   break;
            //case PROCSTATE_BLOCKED:   stateStr = "BLOCKED";   break;
            // Alter output display for blocked status showing reason Test13 Add
        case PROCSTATE_BLOCKED:
            if (_proclocal->blockReason == BLOCK_WAIT) stateStr = "WAIT BLOCK";
            else if (_proclocal->blockReason == BLOCK_JOIN) stateStr = "JOIN BLOCK";
            else stateStr = "BLOCKED";
            break;
        case PROCSTATE_TERMINATE: stateStr = "TERMINATE"; break;
        }

        int ppid = (_proclocal->pParent != NULL) ? _proclocal->pParent->pid : -1;

        console_output(FALSE, "%-5d\t%-5d\t%-5d\t\t%-10s\t%-5d\t%-5d\t%s\n",
            _proclocal->pid,            // PID
            ppid,                       // Parent PID
            _proclocal->priority,       // Priority
            stateStr,                   // Status
            _proclocal->numChildren,    // # Kids
            _proclocal->cpuTime,        // CPU Time
            _proclocal->name            // Name
        );
    }
}

/**************************************************************************
   Name - k_getpid
*************************************************************************/
int k_getpid()
{
    // Currently not in use
    // return 0;
    // FUTURE TESTS
    return (runningProcess ? runningProcess->pid : -1);
}

/**************************************************************************
   Name - k_join
***************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    // Kernel mode validation Test11 Add
    require_kernel_mode();

    disableInterrupts();

    if (runningProcess == NULL)
    {
        enableInterrupts();
        return -1;
    }

    // Joining yourself is invalid safety check
    // Test20 Alter joining yourself is fatal
    if (pid == runningProcess->pid)
    {
        enableInterrupts();
        console_output(FALSE, "join: process attempted to join itself.\n");
        stop(1); // does not return
    }

    // joining parent is fatal Test10 Add
    if (runningProcess->pParent != NULL && runningProcess->pParent->pid == pid)
    {
        enableInterrupts();
        console_output(FALSE, "join: process attempted to join parent.\n");
        stop(2); // does not return
    }

    // Enforce PID->slot mapping rule (Professor requirement, Test04/Test15 impact)
    // pid % MAXPROC must refer to the process's position in the process table.
    int slot = pid % MAXPROC;

    while (1)
    {
        // join is by PID Test13 Add
        // IMPORTANT: we validate the mapping using the pid stored in the slot,
        // because the target may already be reaped by k_wait() (Test15 case).
        if (processTable[slot].pid != pid)
        {
            // If pid doesn't exist fail safety check
            enableInterrupts();
            return -1;
        }

        // If target has terminated OR has been reaped (EMPTY), return exit code immediately
        // Test15 Add: join must still succeed even if another process already waited/reaped it
        if (processTable[slot].status == PROCSTATE_TERMINATE ||
            processTable[slot].status == PROCSTATE_EMPTY)
        {
            if (pChildExitCode != NULL)
                *pChildExitCode = exitCodeSlot[slot];

            // clear block join and tag Test15 Add
            // NOTE: blockedPid no longer needed with semaphore solution
            //runningProcess->blockReason = BLOCK_NONE;

            enableInterrupts();
            return pid;
        }

        // Otherwise block on JOIN (Semaphore-based)
        // Test13 Add (status + reason preserved for process table)
        //runningProcess->status = PROCSTATE_BLOCKED;
        //runningProcess->blockReason = BLOCK_JOIN;

        // Test15 Add
        // Wait on the target's join semaphore (wakes ALL joiners on exit)
        ksem_wait(&joinSem[slot]);

        // loop and re-check target
    }
}

/**************************************************************************
    Name - timer_interrupt_handler
    Interrupt Timer Handler Function - Added Test05
***************************************************************************/
static void timer_interrupt_handler(char deviceId[32], uint8_t command, uint32_t status)
{
    (void)deviceId;
    (void)command;
    (void)status;

    disableInterrupts();

    // Only preempt a real running process
    if (runningProcess != NULL && runningProcess->status == PROCSTATE_RUNNING)
    {
        // If signaled exit handler
        if (runningProcess != NULL && runningProcess->status == PROCSTATE_RUNNING && runningProcess->receivedSignal)
        {
            enableInterrupts();
            k_exit(-5);
            return;
        }

        // Altered Test19 Add
        //  TESTING 19 COMMENT
        if (strcmp(runningProcess->name, "watchdog") != 0)
        {
            DWORD now = read_clock();

            if ((now - runningProcess->lastStartTime) >= TIME_SLICE_MS)
            {
                // Only preempt if there is someone else ready at my priority - Test19 Add
                if (ready_queue_has_process(runningProcess->priority))
                {
                    runningProcess->status = PROCSTATE_READY;
                    add_ready_process(runningProcess);
                    dispatcher();
                    return;
                }

                // else: nobody to rotate with; keep running
                runningProcess->lastStartTime = now; // optional: reset slice accounting
            }
        }
    }

    enableInterrupts();
}

/**************************************************************************
   Name - unblock
*************************************************************************/
int unblock(int pid)
{
    // Currently not in use
    // return 0;
    // FUTURE TESTS
    (void)pid;
    return 0;
}

/*************************************************************************
   Name - block
*************************************************************************/
int block(int newStatus)
{
    //(void)newStatus;
    return 0;
}

/*************************************************************************
   Name - signaled
*************************************************************************/
int signaled()
{
    // Currently not in use
    return 0;
}

/*************************************************************************
   Name - readtime - get_start_time from documentation
*************************************************************************/
int read_time()
{
    // Currently not in use
    return 0;
}

/*************************************************************************
   Name - readClock - read_clock from documentation
*************************************************************************/
DWORD read_clock()
{
    DWORD nowtime = system_clock();
    return nowtime;
}

/*************************************************************************
   Name - check_deadlock
*************************************************************************/
// check to determine if deadlock has occurred
static void check_deadlock()
{
    // Currently not in use
}

/*************************************************************************
   Name - require_kernel_mode
*************************************************************************/
static void require_kernel_mode(void)
{
    // Test kernel mode prior to action. If fail stop(1)
    uint32_t psr = get_psr();
    if ((psr & PSR_KERNEL_MODE) == 0)
    {
        console_output(FALSE, "Kernel mode expected, but function called in user mode.\n");
        stop(1); // prints "THREADS Halted: Error code 1."
    }
}

/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}

/////////////////////// SEMAPHORE IMPLEMENTATION (Test15) //////////////////////////
// Initialize a semaphore
static void ksem_init(ksem_t* s, int initialCount)
{
    s->count = initialCount;
    s->waiters = NULL;
    s->waitersTail = NULL;  // Test17 Add FIFO
}

//Wait semaphore Test15 Add
static void ksem_wait(ksem_t* s)
{
    // interrupts already disabled by caller

    if (s->count > 0)
    {
        s->count--;
        return;
    }

    runningProcess->status = PROCSTATE_BLOCKED;
    runningProcess->blockReason = BLOCK_JOIN;
    runningProcess->nextBlocked = NULL;

    // FIFO enqueue Test17 Add
    if (s->waitersTail == NULL)
    {
        s->waiters = s->waitersTail = runningProcess;
    }
    else
    {
        s->waitersTail->nextBlocked = runningProcess;
        s->waitersTail = runningProcess;
    }

    enableInterrupts();
    dispatcher();
    disableInterrupts();
}

// Broadcast (wake ALL waiters Test15 Add
static void ksem_broadcast(ksem_t* s)
{
    // interrupts already disabled by caller

    Process* p = s->waiters;
    s->waiters = NULL;
    s->waitersTail = NULL;

    while (p != NULL)
    {
        Process* next = p->nextBlocked;
        p->nextBlocked = NULL;

        if (p->status == PROCSTATE_BLOCKED)
        {
            p->status = PROCSTATE_READY;
            p->blockReason = BLOCK_NONE;
            add_ready_process(p);
        }

        p = next;
    }

    // keep it "signaled" for late joiners (join after exit/reap)
    s->count = 1;
}
///////////////////////////////////////////////////////////////////////////
///////////////////////  DEBUG CONSOLE FUNCTIONS //////////////////////////
/**************************************************************************
   Name - DebugConsole
   Purpose - Prints  the message to the console_output if in debug mode
   Parameters - format string and va args
   Returns - nothing
   Side Effects -
*************************************************************************/
static void DebugConsole(char* format, ...)
{
    char buffer[2048];
    va_list argptr;

    if (debugFlag)
    {
        va_start(argptr, format);
        vsprintf(buffer, format, argptr);
        console_output(TRUE, buffer);
        va_end(argptr);

    }
}

///////////////////////////////////////////////////////////////////////////