////////////////////////////////////////////////////////////////////
// CYBV 489
// Dean Lewis
// Schedule.c
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
// END DECLARATIONS ////////////////////////////////////////////////

// FUNCTION PROTOTYPES /////////////////////////////////////////////
static int watchdog(char*);
void dispatcher();
static int launch(void*);
static void check_deadlock();
static void DebugConsole(char* format, ...);
static inline void disableInterrupts();
static inline void enableInterrupts();		                                   
//**** NEED FUNCTION TO TEST IF IN KERNEL MODE ****//
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
int bootstrap(void *pArgs)
{
    // value returned by call to spawn() 
    int result; 

    // Set this to the scheduler version of this function.
    check_io = check_io_scheduler;

    // Initialize ProcessTable. Moved to Processes.c
    processes_init();		               		 

    // SPAWN watchdog process
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Spawn test parent process, which is the main for each test program.
    // Executes SchedulerEntryPoint in SchedulerTest00.c
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag,"Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
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
int k_spawn(char* name, int (*entryPoint)(void *), void* arg, int stacksize, int priority)
{
    int proc_slot;
    struct _process* pNewProc;

    // Debug output
    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    // Initial safety checks
    if (name == NULL)
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        enableInterrupts();		                                //** ADDED
        return -1;
    }
    if (strlen(name) >= (MAXNAME - 1))
    {
        console_output(debugFlag, "spawn(): Process name is too long.  Halting...\n");
        stop( 1);
    }

    //////// Additional safety checks /////////
    // NULL entry point
    if (entryPoint == NULL)
    {
        console_output(debugFlag, "spawn(): entryPoint is NULL.\n");
        enableInterrupts();
        return -1;
    }
    // out of bounds pririty value
    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(debugFlag, "spawn(): invalid priority %d.\n", priority);
        enableInterrupts();
        return -1;
    }
    // Stack size minimum
    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
        console_output(debugFlag, "spawn(): stacksize too small.\n");
        enableInterrupts();
        return -1;
    }

    // Find an empty slot in the process table 
    proc_slot = process_find_free_slot();		

    // Set new process slot
    pNewProc = &processTable[proc_slot];

    /* Setup the entry in the process table. */
    memset(pNewProc, 0, sizeof(Process));		             						 
    strcpy(pNewProc->name, name);

    // Save process initial base values
    pNewProc->pid = nextPid++;
    pNewProc->priority = priority;
    pNewProc->entryPoint = entryPoint;
    pNewProc->status = PROCSTATE_READY;
    pNewProc->stacksize = (unsigned int)stacksize;	

    // If there is a parent process, add this to the list of children.
    if (runningProcess != NULL)
    {
        process_add_child(runningProcess, pNewProc);
    }

    // Debug testing args value
    DebugConsole( "k_spawn(): pid=%d name='%s' argPtr=%p argStr='%s'\n", pNewProc->pid, pNewProc->name, arg, (arg ? (char*)arg : "(null)"));

    // Initialize context for this process, but use launch function pointer for
    // the initial value of the process's program counter (PC) 
    pNewProc->context = context_initialize(launch, stacksize, arg);           

    // Add the process to the ready list.
    add_ready_process(pNewProc);		                     

    enableInterrupts();		 

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
    disableInterrupts();

    // check if current process is null and return -4
    if (runningProcess == NULL)
    {
        enableInterrupts();
        return -4;
    }
    if (runningProcess->pChildren == NULL)
    {
        enableInterrupts();
        return -4;
    }

    while (1)
    {
        Process* prev = NULL;
        // Check for terminated child
        Process* termchild = process_find_term_child(runningProcess, &prev); 

		// If dead child found, clean up and return
        if (termchild != NULL)
        {
            int pid = termchild->pid;
            int slot = (int)(termchild - processTable);

            if (code) *code = exitCodeSlot[slot];

            /* remove from parent's child list */
            remove_term_child(runningProcess, termchild, prev);       

            /* reclaim the process table entry */
            // ********Need to ajust to clean out all process table elements
            processTable[slot].status = PROCSTATE_EMPTY;
            processTable[slot].pid = 0;
            processTable[slot].context = NULL;
            exitCodeSlot[slot] = 0;

            enableInterrupts();
            return pid;
        }

        /* block until a child quits */
        runningProcess->status = PROCSTATE_BLOCKED;
        enableInterrupts();
        dispatcher();
        disableInterrupts();
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
    disableInterrupts();

    Process* _proclocal = runningProcess;

    if (_proclocal == NULL)
    {
        enableInterrupts();
        stop(exitcode);
        return;
    }

    int mySlot = (int)(_proclocal - processTable);
    exitCodeSlot[mySlot] = exitcode;

    _proclocal->status = PROCSTATE_TERMINATE;

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
    int result = 0;

    disableInterrupts();

    Process* _proclocal = find_process_by_pid(pid);                 

    if (_proclocal == NULL)
    {
        enableInterrupts();				   
		return 0;		                             
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
        enableInterrupts();
        return;
    }
	// ****************************************
	// if nextProcess not equal to runningProcess, do context switch and set current process status
	// ****************************************

    /* If the currently running process is still runnable, put it back on ready */
    if (runningProcess != NULL && runningProcess->status == PROCSTATE_RUNNING)
    {
        runningProcess->status = PROCSTATE_READY;
        add_ready_process(runningProcess);
    }

	// Switch to the next process
    runningProcess = nextProcess;
    runningProcess->status = PROCSTATE_RUNNING;

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
    console_output(FALSE, "PID  Parent  Priority    Status  # Kids  CPUtime Name\n");

    // Cycle through process table printing off each row
    for (int i = 1; i < MAXPROC; i++)
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
        case PROCSTATE_BLOCKED:   stateStr = "BLOCKED";   break;
        case PROCSTATE_TERMINATE: stateStr = "TERMINATE"; break;
        }

        int ppid = (_proclocal->pParent != NULL) ? _proclocal->pParent->pid : -1;

        console_output(FALSE, "%-5d %-5d %-5d %-10s %-5d %s\n",
            i,
            _proclocal->pid,
            ppid,
            stateStr,
            _proclocal->priority,
            _proclocal->name
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
    // Currently not in use
    // return 0;
    // FUTURE TESTS
    (void)pid;
    (void)pChildExitCode;
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
   Name - readtime
*************************************************************************/
int read_time()
{
    // Currently not in use
    return 0;
}

/*************************************************************************
   Name - readClock
*************************************************************************/
DWORD read_clock()
{
    return system_clock();
}

/*************************************************************************
   Name - check_deadlock
*************************************************************************/
// check to determine if deadlock has occurred
static void check_deadlock()
{
    // Currently not in use
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

/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}
///////////////////////////////////////////////////////////////////////////