////////////////////////////////////////////////////////////////////
// CYBV 489
// SP 2026
// Dean Lewis
// Scheduler.c
////////////////////////////////////////////////////////////////////
// This file implemetns the OS Scheduler functions.  The scheduler is responsible for managing the process table,
// handling process creation and termination, and performing context switches between processes. 
// The scheduler maintains a process table, which is an array of Process structs that represent 
// individual processes in the system. Each Process struct contains information about the process's state, 
// context, parent-child relationships, and other relevant data. The scheduler provides functions for spawning 
// new processes, waiting for child processes to terminate, joining with child processes, killing processes, 
// and exiting processes. It also includes a dispatcher function that performs context switching to the next 
// ready process based on priority scheduling.

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
int debugFlag = 0;          //  0 = no debug output, 1 = debug output
#define TIME_SLICE_MS 250   //  BUMPING UP TO EVEN OUT ORDERING
/////////  CPU TIME OUTPUT FIX  //////////
#define NOW_MS()   ((DWORD)(read_clock() / 1000))  // ADDING TO CONVERT OUTPUT TO milliseconds Fixes Test05, 08
// END DECLARATIONS ////////////////////////////////////////////////

// One join semaphore per process-table slot (pid % MAXPROC)
static ksem_t joinSem[MAXPROC];  // sem_t local

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
// Semaphore functions Test15 Add
static void semaphore_wait(ksem_t* s);
static void semaphore_broadcast(ksem_t* s);
static void semaphore_init(ksem_t* s, int initialCount);
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
    // Value returned by call to spawn()
    int result;

    // Set this to the scheduler version of this function.
    check_io = check_io_scheduler;

    // Initialize ProcessTable. Moved to Processes.c
    processes_init();

    // Test15 Add
    // Initialize join semaphores (one per slot)
    for (int i = 0; i < MAXPROC; i++)
    {
        semaphore_init(&joinSem[i], 0);
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
    // Executes watchdog Entry point
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Spawn test parent process, which is the main for each test program.
    // Executes SchedulerEntryPoint Entry point
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

    // Debug print out
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
        console_output(debugFlag, "spawn(): Priority out of range.\n"); // Test21 Alter
        enableInterrupts();
        return -2;
    }
    //////////// SAFETY CHECKS ////////////

    // Test 18 Add - Correct stack size error handling
    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
		console_output(FALSE, "spawn(): Stack size too small.\n"); // Set to FALSE to avoid debug flag Test18 Add
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
        return -4;      // or a more specific "no more processes" code if your spec defines it
    }

    pNewProc = &processTable[slot];

    // Initialize the process table entry
    memset(pNewProc, 0, sizeof(Process));
    strcpy(pNewProc->name, name);

    // Assign PID that maps to the slot
    pNewProc->pid = nextPid;
    nextPid++;   // move to the next candidate for the next spawn

    // Test15 Add
    // Reset the join semaphore for this slot 
    semaphore_init(&joinSem[slot], 0);

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

    // Debug Print
    DebugConsole("k_spawn(): pid=%d slot=%d name='%s' argPtr=%p argStr='%s'\n", pNewProc->pid, slot, pNewProc->name, arg, (arg ? (char*)arg : "(null)"));

    // Initialize context
    pNewProc->context = context_initialize(launch, stacksize, arg);

    // Add to ready queue
    process_add_ready(pNewProc);

    // If new process is higher priority than currently running
    int isHigher = (runningProcess != NULL) && (runningProcess->status == PROCSTATE_RUNNING) && (pNewProc->priority > runningProcess->priority);

    enableInterrupts();

	// If the new process has higher priority than the currently running process, dispatch to it immediately.
    if (isHigher)
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

    // Expanded for test 04, 05, 19 to fix ordering issue
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
            process_remove_term_child(runningProcess, termchild, prev);

            // reset entry
            // Changed for Test06 and Test15
            processTable[slot].status = PROCSTATE_EMPTY;
            processTable[slot].context = NULL;
            processTable[slot].pChildren = NULL;
            processTable[slot].pParent = NULL;
            processTable[slot].pid = 0;   // Test26 fix - late join fatal

            // We are returning to user code; make sure our state reflects that.
            runningProcess->status = PROCSTATE_RUNNING;
            runningProcess->blockReason = BLOCK_NONE;

            enableInterrupts();
            return pid;
        }

        /* block until a child quits  Test13 Add */
        runningProcess->status = PROCSTATE_BLOCKED;
        runningProcess->blockReason = BLOCK_WAIT;

        enableInterrupts();
        dispatcher();
        disableInterrupts();

        // When we resume here, we are running again
        if (runningProcess != NULL)
        {
            runningProcess->status = PROCSTATE_RUNNING;
            runningProcess->blockReason = BLOCK_NONE;  // important: don't keep WAIT reason while RUNNING
        }
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
            if (_proclocalchild->status != PROCSTATE_TERMINATE &&
                _proclocalchild->status != PROCSTATE_EMPTY)
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
    DWORD now = NOW_MS();
    _proclocal->cpuTime += (now - _proclocal->lastStartTime);

    _proclocal->status = PROCSTATE_TERMINATE;

    // Test15 Add
    // Wake any processes blocked on JOIN (replaces blockedPid scan loop logic)
    semaphore_broadcast(&joinSem[mySlot]);

    /* Wake parent if it is waiting - expanded all secenarios to fix order bug */
    if (_proclocal->pParent != NULL && _proclocal->pParent->status == PROCSTATE_BLOCKED)
    {
        _proclocal->pParent->status = PROCSTATE_READY;

        // Test19 fix if parent was waiting, run it now. Also fixes earlier test cases
        if (_proclocal->pParent->blockReason == BLOCK_WAIT)
        {
            // Put parent back on its ready queue and immediately schedule
            _proclocal->pParent->blockReason = BLOCK_NONE;
            process_add_ready(_proclocal->pParent);

            enableInterrupts();
            dispatcher();
            return; // important: stop k_exit here (we already switched away)
        }

        // Add to ready list
        process_add_ready(_proclocal->pParent);
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
    // Test11 Add
    require_kernel_mode();

    int result = 0;

    disableInterrupts();

    Process* _proclocal = process_find_by_pid(pid);

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
            process_add_ready(_proclocal);
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
    nextProcess = process_next_ready();

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
        process_add_ready(runningProcess);
    }

    // If the current running process is not null and a new process switch is coming update its cpu time
    if (runningProcess != NULL && nextProcess != NULL) {
        // Added to calculate cpu time when process is switched out
        DWORD now = NOW_MS(); // Fixed calculations
        runningProcess->cpuTime += (now - runningProcess->lastStartTime);
    }

    // Switch to the next process and block reason
    runningProcess = nextProcess;
    runningProcess->status = PROCSTATE_RUNNING;
    runningProcess->blockReason = BLOCK_NONE;

    // start cpu running clock time
    runningProcess->lastStartTime = NOW_MS();  // Fixed calculations

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
        dispatcher();		            // POSSIBLY NOT NEEDED HERE
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
	char stateBuffer[32]; //Test22 Add for unknown block reason

    // Print out header pro process table
    console_output(FALSE, "PID\tParent\tPriority\tStatus\t\t# Kids\tCPUtime\tName\n");

    // Cycle through process table printing off each row
    for (int i = 0; i < MAXPROC; i++)   // CHANGING to 0 to MAXPROC for testing
    {
        Process* _proclocal = &processTable[i];

        // If process state == empty, continue to next row
        if (_proclocal->status == PROCSTATE_EMPTY)
            continue;

        // Const char* stateStr = "UNKNOWN";
        switch (_proclocal->status)
        {
        case PROCSTATE_READY:     strcpy(stateBuffer, "READY");     break;
        case PROCSTATE_RUNNING:   strcpy(stateBuffer, "RUNNING");   break;
            // Case PROCSTATE_BLOCKED:   stateStr = "BLOCKED";   break;
            // Alter output display for blocked status showing reason - Test13 Add
        case PROCSTATE_BLOCKED:
            if (_proclocal->blockReason == BLOCK_WAIT) strcpy(stateBuffer, "WAIT BLOCK");
            else if (_proclocal->blockReason == BLOCK_JOIN) strcpy(stateBuffer, "JOIN BLOCK");
            else snprintf(stateBuffer, sizeof(stateBuffer), "%d", _proclocal->blockReason); // Test22 Add - unknown block reason
            break;
        case PROCSTATE_TERMINATE: strcpy(stateBuffer, "TERMINATE"); break;
        default: strcpy(stateBuffer, "UNKNOWN"); break;
        }

        int ppid = (_proclocal->pParent != NULL) ? _proclocal->pParent->pid : -1;

        console_output(FALSE, "%-5d\t%-5d\t%-5d\t\t%-10s\t%-5d\t%-5d\t%s\n",
            _proclocal->pid,            // PID
            ppid,                       // Parent PID
            _proclocal->priority,       // Priority
            stateBuffer,                // Status
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
// Helper: determine whether a PID exists anywhere in the process table.
// (Used to satisfy both: "halt on truly non-existent pid" AND "allow late join after reap".)
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

    // Track whether we actually blocked waiting on this join.
    // If we blocked once, we must allow success even if the parent reaps the slot later (Test17).
    int blockedOnce = 0;

    while (1)
    {
        // If the pid is not currently in the slot, then it does not exist (late join) => HALT (Test26)
        // NOTE: If we already blocked and got woken, the parent might have reaped/cleared pid;
        //       in that case we still succeed using exitCodeSlot[slot] (Test17).
        if (processTable[slot].pid != pid)
        {
            if (!blockedOnce)
            {
                enableInterrupts();
                console_output(FALSE, "join: attempting to join a process that does not exist.\n");
                stop(1); // Print HALT
            }

            // We were already waiting and got woken; slot was reaped after exit.
            if (pChildExitCode != NULL)
                *pChildExitCode = exitCodeSlot[slot];

            enableInterrupts();
            return 0;
        }

        // If target has terminated, return exit code immediately
        if (processTable[slot].status == PROCSTATE_TERMINATE)
        {
            if (pChildExitCode != NULL)
                *pChildExitCode = exitCodeSlot[slot];

            enableInterrupts();
            return 0;
        }

        // If it got reaped while still matching pid (rare with your cleanup order), also succeed
        // only if we were already waiting; otherwise it would have been caught above.
        if (processTable[slot].status == PROCSTATE_EMPTY)
        {
            if (pChildExitCode != NULL)
                *pChildExitCode = exitCodeSlot[slot];

            enableInterrupts();
            return 0;
        }

        // Otherwise block on JOIN (Semaphore-based)
        // Test15 Add: Wait on the target's join semaphore (wakes ALL joiners on exit)
        blockedOnce = 1;
        semaphore_wait(&joinSem[slot]);

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
        // TESTING 19 COMMENT
        if (strcmp(runningProcess->name, "watchdog") != 0)
        {
            DWORD now = NOW_MS();
            if ((now - runningProcess->lastStartTime) >= TIME_SLICE_MS)
            {
                // Only preempt if there is someone else ready at my priority - Test19 Add
                if (process_in_ready_queue(runningProcess->priority))
                {
                    runningProcess->status = PROCSTATE_READY;
                    process_add_ready(runningProcess);
                    dispatcher();
                    return;
                }

                // Else nobody to rotate with keep running
                // Reset slice accounting
                runningProcess->lastStartTime = now; 
            }
        }
    }

    enableInterrupts();
}

/**************************************************************************
   Name - unblock - TEST22 ADD
*************************************************************************/
int unblock(int pid)
{
    require_kernel_mode();
    disableInterrupts();

    Process* _proclocal = process_find_by_pid(pid);
    if (_proclocal == NULL)
    {
        enableInterrupts();
        return -1;
    }

    if (_proclocal->status != PROCSTATE_BLOCKED)
    {
        enableInterrupts();
        return -1;
    }

    _proclocal->status = PROCSTATE_READY;
    _proclocal->blockReason = BLOCK_NONE;
    process_add_ready(_proclocal);

    enableInterrupts();
    return 0;
}

/*************************************************************************
   Name - block - TEST22 Add - Updated Test31
*************************************************************************/
int block(int code)
{
    require_kernel_mode();
    disableInterrupts();

    if (runningProcess == NULL)
    {
        enableInterrupts();
        return -1;
    }

    // Test31 - disallowing codes 0-10 as reserved - arbitrary to me but i do use 14 in Test22
    // Test 31 uses 6?
    if (code >= 0  && code <= 10)
    {
        console_output(FALSE, "block: function called with a reserved status value.\n");
        stop(1);        // prints "THREADS Halted: Error code 1."
    }

    // Block the CURRENT process with the provided code
    runningProcess->status = PROCSTATE_BLOCKED;
    runningProcess->blockReason = code;

    enableInterrupts();
    dispatcher();              
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
    // using system_clock NOT get_start_time()
    int readtime = read_clock();  // Test30 Add
    return readtime;
    //return (int)system_clock();
}

/*************************************************************************
   Name - readClock - read_clock from documentation
*************************************************************************/
DWORD read_clock()
{
    //DWORD nowtime = system_clock();
    // Change to convert to milliseconds Added for Test04, 05, 08, 19
    DWORD nowtime = system_clock();
    return nowtime;
    //return (DWORD)system_clock();
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

/////////////////////// SEMAPHORE IMPLEMENTATION Test15 Add //////////////////////////
// Initialize a semaphore
static void semaphore_init(ksem_t* sem_t, int initialCount)
{
    sem_t->count = initialCount;
    sem_t->waiters = NULL;
    sem_t->waitersTail = NULL;  // Test17 Add FIFO
}

// Wait semaphore Test15 Add
static void semaphore_wait(ksem_t* sem_t)
{
    // interrupts already disabled by caller

    if (sem_t->count > 0)
    {
        sem_t->count--;
        return;
    }

    runningProcess->status = PROCSTATE_BLOCKED;
    runningProcess->blockReason = BLOCK_JOIN;
    runningProcess->nextBlocked = NULL;

    // FIFO enqueue Test17 Add
    if (sem_t->waitersTail == NULL)
    {
        sem_t->waiters = sem_t->waitersTail = runningProcess;
    }
    else
    {
        sem_t->waitersTail->nextBlocked = runningProcess;
        sem_t->waitersTail = runningProcess;
    }

    enableInterrupts();
    dispatcher();
    disableInterrupts();
}

// Broadcast (wake ALL waiters Test15 Add
static void semaphore_broadcast(ksem_t* sem_t)
{
    // Interrupts already disabled by caller
    Process* _proclocal = sem_t->waiters;
    sem_t->waiters = NULL;
    sem_t->waitersTail = NULL;

    while (_proclocal != NULL)
    {
        Process* next = _proclocal->nextBlocked;
        _proclocal->nextBlocked = NULL;

        if (_proclocal->status == PROCSTATE_BLOCKED)
        {
            _proclocal->status = PROCSTATE_READY;
            _proclocal->blockReason = BLOCK_NONE;
            process_add_ready(_proclocal);
        }

        _proclocal = next;
    }

    // Keep it signaled for late joiners 
    sem_t->count = 1;
}

// Checks to see if pid is current use
static int process_check_pid_exists(int pid)
{
    for (int i = 0; i < MAXPROC; i++)
    {
        // Count as "exists" if we still remember the PID in any slot (even if EMPTY due to reap).
        if (processTable[i].status != PROCSTATE_EMPTY && processTable[i].pid == pid)
        {
            return 1;
        }
    }
    return 0;
}
///////////////////////////////////////////////////////////////////////////
///////////////////////  DEBUG CONSOLE FUNCTIONS //////////////////////////
/**************************************************************************
   Name - DebugConsole
   Purpose - Prints  the message to the console_output if in debug mode
   Parameters - format string and va args
   Returns - nothing
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