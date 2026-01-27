// Dean Lewis
// Schedule.c - Contains bootstrap()
////////////////////////////////////////////////////////////////////

#define _CRT_SECURE_NO_WARNINGS
// LIBRARIES
#include <stdio.h>
#include <string.h>		                                                    //** ADDED	
#include <stdarg.h>		                                                    //** ADDED
// HELPER FILES
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

// DECLARATIONS ////////////////////////////////////////////////////                           
Process* runningProcess = NULL;
int debugFlag = 0;                                                          // 0 = no debug output, 1 = debug output
// END DECLARATIONS ////////////////////////////////////////////////

// FUNCTION PROTOTYPES /////////////////////////////////////////////
static int watchdog(char*);
void dispatcher();
static int launch(void*);
static void check_deadlock();
static void DebugConsole(char* format, ...);
static inline void disableInterrupts();
static inline void enableInterrupts();		                                    //** ADDED
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
    int result; /* value returned by call to spawn() */

    /* set this to the scheduler version of this function.*/
    check_io = check_io_scheduler;

    // Initialize ProcessTable
    // init_process_table();
    processes_init();		               		 

    // SPAWN watchdog process
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Spawn test parent process, which is the main for each test program.  */
    // Execute SchedulerEntryPoint 
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag,"Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    // Performe next context switching
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

    // Comment following line to correct output
    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    /* Validate all of the parameters, starting with the name. */
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
//********************************** ADDED
    if (entryPoint == NULL)
    {
        console_output(debugFlag, "spawn(): entryPoint is NULL.\n");
        enableInterrupts();
        return -1;
    }
    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(debugFlag, "spawn(): invalid priority %d.\n", priority);
        enableInterrupts();
        return -1;
    }
    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
        console_output(debugFlag, "spawn(): stacksize too small.\n");
        enableInterrupts();
        return -1;
    }
//********************************** ADDED
    /* Find an empty slot in the process table */
    proc_slot = process_find_free_slot();		                    //** ADDED
//********************************** ADDED
    //if (proc_slot < 0)
    //{
    //    console_output(debugFlag, "spawn(): process table is full.\n");
    //    enableInterrupts();
    //    return -1;
    //}
//********************************** ADDED
    pNewProc = &processTable[proc_slot];

    /* Setup the entry in the process table. */
    memset(pNewProc, 0, sizeof(Process));		                //** ADDED							 
    strcpy(pNewProc->name, name);
//********************************** ADDED
    pNewProc->pid = nextPid++;
    pNewProc->priority = priority;
    pNewProc->entryPoint = entryPoint;
    pNewProc->status = PROCSTATE_READY;
    pNewProc->stacksize = (unsigned int)stacksize;	

//********************************** ADDED		  
    // Added debug output to find error with new process name
    DebugConsole("k_spawn(): slot=%d oldStatus=%d oldPid=%d oldName='%s'\n", proc_slot, pNewProc->status, pNewProc->pid, pNewProc->name);

    /* If there is a parent process,add this to the list of children. */
    if (runningProcess != NULL)
    {
        // add_child(runningProcess, pNewProc);		            //** ADDED	
        process_add_child(runningProcess, pNewProc);		    //** ADDED	
    }

    // Testing args value
    DebugConsole( "k_spawn(): pid=%d name='%s' argPtr=%p argStr='%s'\n", pNewProc->pid, pNewProc->name, arg, (arg ? (char*)arg : "(null)"));

    /* Initialize context for this process, but use launch function pointer for
     * the initial value of the process's program counter (PC) */
    pNewProc->context = context_initialize(launch, stacksize, arg);           

    /* Add the process to the ready list. */
    ready_enqueue(pNewProc);		                            //** ADDED	

    enableInterrupts();		                                    //** ADDED	
    return pNewProc->pid;

} /* spawn */

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

    int rc = 0;
    if (runningProcess && runningProcess->entryPoint)
    {
        rc = runningProcess->entryPoint(runningProcess->name);
    }

    k_exit(rc);
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
//********************************** ADDED	
    disableInterrupts();

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
        Process* dead = process_find_quit_child(runningProcess, &prev);          //** ADDED

		// If dead child found, clean up and return
        if (dead != NULL)
        {
            int pid = dead->pid;
            int slot = (int)(dead - processTable);

            if (code) *code = exitCodeSlot[slot];

            /* remove from parent's child list */
            process_remove_child_link(runningProcess, dead, prev);              //** ADDED

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
//********************************** ADDED	

/**************************************************************************
   Name - k_exit

   Purpose - Exits a process and coordinates with the parent for cleanup 
             and return of the exit code.

   Parameters - the code to return to the grieving parent

   Returns - nothing
   
*************************************************************************/
void k_exit(int code)
{
//********************************** ADDED	
    disableInterrupts();

    Process* me = runningProcess;

    if (me == NULL)
    {
        enableInterrupts();
        stop(code);
        return;
    }

    int mySlot = (int)(me - processTable);
    exitCodeSlot[mySlot] = code;

    me->status = PROCSTATE_TERMINATE;

    /* Wake parent if it is waiting */
    if (me->pParent != NULL && me->pParent->status == PROCSTATE_BLOCKED)
    {
        me->pParent->status = PROCSTATE_READY;
        ready_enqueue(me->pParent);
    }

    /* If no parent, end the system */
    if (me->pParent == NULL)
    {
        enableInterrupts();

        // Print final complete statement
        console_output(FALSE, "All processes completed.\n");

        stop(code);
        return;
    }

    enableInterrupts();

    dispatcher();
//********************************** ADDED	
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
//********************************** ADDED	
    disableInterrupts();

    // Process* p = find_process_by_pid(pid);
    Process* p = process_find_by_pid(pid);                  //** ADDED

    if (p == NULL)
    {
        enableInterrupts();				   
		return 0;		                                    //** ADDED ALTERED POSITION
    }

    /* Minimal for now; scheduler tests later will define behavior. */
    (void)signal;

    enableInterrupts();

    return result;
//********************************** ADDED			  
}

/**************************************************************************
   Name - k_getpid
*************************************************************************/
int k_getpid()
{
    //return 0;
    return (runningProcess ? runningProcess->pid : -1);		//** ADDED ALTERED CODE
}

/**************************************************************************
   Name - k_join
***************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    (void)pid;		//** ADDED
    (void)pChildExitCode;			                        //** ADDED	 
    return 0;
}

/**************************************************************************
   Name - unblock
*************************************************************************/
int unblock(int pid)
{
    (void)pid;		                                        //** ADDED
    return 0;
}

/*************************************************************************
   Name - block
*************************************************************************/
int block(int newStatus)
{
    (void)newStatus;		                                //** ADDED			
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

void display_process_table()
{
    //typedef struct _process
    //{
    //    struct			_process* nextReadyProcess;
    //    struct			_process* nextSiblingProcess;
    //    struct			_process* pParent;
    //    struct			_process* pChildren;

    //    char			    name[MAXNAME];					// Process name 
    //    char			    startArgs[MAXARG];				// Process arguments
    //    void* context;						            // Process's current context 
    //    short			    pid;							// Process id (pid) 
    //    int				priority;
    //    int				(*entryPoint) (void*);			// The entry point that is called from launch 
    //    //char*			stack;							// WILL NOT USE THIS - CAN BE REMOVED
    //    unsigned int	    stacksize;
    //    int				status;							// READY, QUIT, BLOCKED, etc.

    //    /* WHAT ELSE WILL WE NEED TO TRACK? ADD BELOW */

    //} Process;

    // Print out header pro process table
    console_output(FALSE, "PID  Parent  Priority    Status  # Kids  CPUtime Name\n");

    // Cycle through process table printing off each row
    for (int i = 1; i < MAXPROC; i++)
    {
        Process* p = &processTable[i];

        // If process state == empty, continue to next row
        if (p->status == PROCSTATE_EMPTY)
            continue;

        const char* stateStr = "UNKNOWN";
        switch (p->status)
        {
        case PROCSTATE_READY:     stateStr = "READY";     break;
        case PROCSTATE_RUNNING:   stateStr = "RUNNING";   break;
        case PROCSTATE_BLOCKED:   stateStr = "BLOCKED";   break;
        case PROCSTATE_TERMINATE: stateStr = "TERMINATE"; break;
        }

        int ppid = (p->pParent != NULL) ? p->pParent->pid : -1;

        console_output(FALSE,"%-5d %-5d %-5d %-10s %-5d %s\n",
            i,
            p->pid,
            ppid,
            stateStr,
            p->priority,
            p->name
        );
    }
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

    //Process *nextProcess = NULL;
    nextProcess = ready_dequeue_highest();		            //** ADDED ALTERED CODE

    if (nextProcess == NULL)
    {
        enableInterrupts();
        return;
    }

    /* If the currently running process is still runnable, put it back on ready */
    if (runningProcess != NULL && runningProcess->status == PROCSTATE_RUNNING)
    {
        runningProcess->status = PROCSTATE_READY;
        ready_enqueue(runningProcess);
    }

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

/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
    // Currently not in use
}

// Disables the interrupts
static inline void disableInterrupts()
{
    /* We ARE in kernel mode */

    int psr = get_psr();
    psr = psr & ~PSR_INTERRUPTS;
    set_psr( psr);

}

// Enables the interrupts
static inline void enableInterrupts()
{
    /* We ARE in kernel mode */

    int psr = get_psr();
    psr = psr | PSR_INTERRUPTS;
    set_psr(psr);
} /* enableInterrupts */

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