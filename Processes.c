////////////////////////////////////////////////////////////////////
// CYBV 489
// SP 2026
// Dean Lewis
// Processes.c
////////////////////////////////////////////////////////////////////

#define _CRT_SECURE_NO_WARNINGS
#include <string.h>

#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

// DECLARATIONS
// Keep track of processes in OS (MAXPROC is set to 50)
Process processTable[MAXPROC];                  
int nextPid = 1;

// Exit code storage (indexed by process table slot) 
int exitCodeSlot[MAXPROC] = { 0 };

// Ready queues (indexed by priority) 
static Process* readyHeads[HIGHEST_PRIORITY + 1];
static Process* readyTails[HIGHEST_PRIORITY + 1];

///// PROCESS TABLE INITIALIZATION /////
// Instantiate each process in the procTable (1 - MAXPROC)
// These could be done on demand but easier to make sure we have a clean set when a new process is created.
void processes_init(void)
{
    for (int i = 0; i < MAXPROC; i++)
    {
        processTable[i].nextReadyProcess = NULL;
        processTable[i].nextSiblingProcess = NULL;
        processTable[i].pParent = NULL;
        processTable[i].pChildren = NULL;

        processTable[i].name[0] = '\0';                 // TERMINATE EMPTY
        processTable[i].startArgs[0] = '\0';            // TERMINATE EMPTY
        processTable[i].context = NULL;
        processTable[i].pid = 0;
        processTable[i].priority = 0;
        processTable[i].entryPoint = NULL;
        processTable[i].stacksize = 0;

        processTable[i].status = PROCSTATE_EMPTY;       // O - SEE DECLARATION
        processTable[i].cpuTime = 0;                       		
        processTable[i].numChildren = 0;
        processTable[i].lastStartTime = 0;
        processTable[i].receivedSignal = 0;
        processTable[i].blockReason = BLOCK_NONE;
        processTable[i].blockedPid = 0;

        exitCodeSlot[i] = 0;

		//processTable[i].joinSem.count = 0;              // Added for semaphore logic in Test15
  //      processTable[i].joinSem.waiters = NULL;
  //      processTable[i].exited = 0;
        processTable[i].nextBlocked = NULL;
    }

    for (int p = 0; p <= HIGHEST_PRIORITY; p++)
    {
        readyHeads[p] = NULL;
        readyTails[p] = NULL;
    }
}

///// PROCESS TABLE HELPER FUNCTIONS  /////
// Finds an unused entry in the process table to allocate for a new process.
int process_find_free_slot(void)
{

    for (int i = 0; i < MAXPROC; i++) // CHANGING FROM 1 to 0!!  /////????????/* leave slot 0 unused */?????????/////
    {
        if (processTable[i].status == PROCSTATE_EMPTY)
            return i;
    }
    return -1;
}

// Searches the process table for a process by pid
Process* find_process_by_pid(int pid)
{
    //for (int i = 1; i < MAXPROC; i++)
	for (int i = 0; i < MAXPROC; i++)  // CHANGING FROM 1 to 0!! Test19 Alter
    {
        if (processTable[i].status != PROCSTATE_EMPTY && processTable[i].pid == pid)
            return &processTable[i];
    }
    return NULL;
}

/* PARENT-CHILD HELPERS  */
// Adds a child process to a parent process's list of children
void process_add_child(Process* parent, Process* child)
{
	// Test17 Change ordering logic to FIFO - append to tail instead of head
    child->pParent = parent;
    child->nextSiblingProcess = NULL;

    // If no children yet, child becomes head
    if (parent->pChildren == NULL)
    {
        parent->pChildren = child;
        return;
    }

    // Otherwise, append to tail to preserve spawn order (FIFO)
    Process* cur = parent->pChildren;
    while (cur->nextSiblingProcess != NULL)
    {
        cur = cur->nextSiblingProcess;
    }
    cur->nextSiblingProcess = child;
}

// Searches a parent process's child list for a child that has terminated
Process* process_find_term_child(Process* parent, Process** pPrevOut)
{
    Process* prev = NULL;
    Process* cur = parent->pChildren;

    while (cur != NULL)
    {
        if (cur->status == PROCSTATE_TERMINATE)
        {
            if (pPrevOut) *pPrevOut = prev;
            return cur;
        }
        prev = cur;
        cur = cur->nextSiblingProcess;
    }

    if (pPrevOut) *pPrevOut = NULL;
    return NULL;
}

// Removes a child process from its parent's list of children.
void remove_term_child(Process* parent, Process* child, Process* prev)
{
    if (prev == NULL) parent->pChildren = child->nextSiblingProcess;
    else prev->nextSiblingProcess = child->nextSiblingProcess;

    child->nextSiblingProcess = NULL;
	parent->numChildren--;
}

/* QUEUE HELPER FUNCTIONS */
// Adds a process to the ready queue based
void add_ready_process(Process* _proc)
{
    int addready = _proc->priority;
    _proc->nextReadyProcess = NULL;

    if (readyTails[addready] == NULL)
    {
        readyHeads[addready] = readyTails[addready] = _proc;
    }
    else
    {
        readyTails[addready]->nextReadyProcess = _proc;
        readyTails[addready] = _proc;
    }
}

// Selects and removes the highest priority READY process from the ready queues. 
// Priority is determined from highest to lowest.
Process* next_ready_process(void)
{
    // Test06 - Correct pop READY
    for (int pri = HIGHEST_PRIORITY; pri >= LOWEST_PRIORITY; pri--)
    {
        while (readyHeads[pri] != NULL)
        {
            Process* p = readyHeads[pri];
            readyHeads[pri] = p->nextReadyProcess;
            if (readyHeads[pri] == NULL)
                readyTails[pri] = NULL;

            p->nextReadyProcess = NULL;

            // Only schedule READY processes.
            if (p->status == PROCSTATE_READY)
                return p;
        }
    }
    return NULL;
}

// Searches for a parents child by PID in process table
// Added in Test06 to locate the exact child being joined.
Process* find_child(Process* parent, int pid, Process** pPrevOut)
{
    Process* prev = NULL;
    Process* cur = parent->pChildren;

    while (cur != NULL)
    {
        if (cur->pid == pid)
        {
            if (pPrevOut) *pPrevOut = prev;
            return cur;
        }
        prev = cur;
        cur = cur->nextSiblingProcess;
    }

    if (pPrevOut) *pPrevOut = NULL;
    return NULL;
}

// Test19 Add - Check if ready queue has process at priority to switch to
int ready_queue_has_process(int pri)
{
    return (pri >= LOWEST_PRIORITY && pri <= HIGHEST_PRIORITY && readyHeads[pri] != NULL);
}
