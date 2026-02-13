////////////////////////////////////////////////////////////////////
// CYBV 489
// SP 2026
// Dean Lewis
// Processes.c
////////////////////////////////////////////////////////////////////
// This file implements the process table and related helper functions for process management. 
// The process table is a fixed-size array of Process structs, 
// which represent individual processes in the system. Each Process struct contains 
// information about the process's state, context, parent-child relationships, 
// and other relevant data. The helper functions provide functionality for initializing 
// the process table, finding free slots for new processes, managing parent-child relationships, 
// and handling ready queues for scheduling.


#define _CRT_SECURE_NO_WARNINGS
#include <string.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

// DECLARATIONS
// Keep track of processes MAXPROC is set to 50
Process processTable[MAXPROC];                  
int nextPid = 1;

// Exit code storage indexed by process table slot
int exitCodeSlot[MAXPROC] = { 0 };

// Ready queues (indexed by priority) 
static Process* readyHeads[HIGHEST_PRIORITY + 1];
static Process* readyTails[HIGHEST_PRIORITY + 1];

///// PROCESS TABLE INITIALIZATION /////
// Instantiate each process in the procTable 1 - MAXPROC
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
    }

    for (int priority = 0; priority <= HIGHEST_PRIORITY; priority++)
    {
        readyHeads[priority] = NULL;
        readyTails[priority] = NULL;
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
Process* process_find_by_pid(int pid)
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
    Process* current = parent->pChildren;
    while (current->nextSiblingProcess != NULL)
    {
        current = current->nextSiblingProcess;
    }
    current->nextSiblingProcess = child;
}

// Searches a parent process's child list for a child that has terminated
Process* process_find_term_child(Process* parent, Process** pPrevOut)
{
    Process* previous = NULL;
    Process* current = parent->pChildren;

    while (current != NULL)
    {
        if (current->status == PROCSTATE_TERMINATE)
        {
            if (pPrevOut) *pPrevOut = previous;
            return current;
        }
        previous = current;
        current = current->nextSiblingProcess;
    }

    if (pPrevOut) *pPrevOut = NULL;
    return NULL;
}

// Removes a child process from its parent's list of children.
void process_remove_term_child(Process* parent, Process* child, Process* previous)
{
    if (previous == NULL) parent->pChildren = child->nextSiblingProcess;
    else previous->nextSiblingProcess = child->nextSiblingProcess;

    child->nextSiblingProcess = NULL;
	parent->numChildren--;
}

/* QUEUE HELPER FUNCTIONS */
// Adds a process to the ready queue based
void process_add_ready(Process* _proclocal)
{
    int addready = _proclocal->priority;
    _proclocal->nextReadyProcess = NULL;

    if (readyTails[addready] == NULL)
    {
        readyHeads[addready] = readyTails[addready] = _proclocal;
    }
    else
    {
        readyTails[addready]->nextReadyProcess = _proclocal;
        readyTails[addready] = _proclocal;
    }
}

// Selects and removes the highest priority READY process from the ready queues. 
// Priority is determined from highest to lowest.
Process* process_next_ready(void)
{
    // Test06 - Correct pop READY
    for (int priority = HIGHEST_PRIORITY; priority >= LOWEST_PRIORITY; priority--)
    {
        while (readyHeads[priority] != NULL)
        {
            Process* _proclocal = readyHeads[priority];
            readyHeads[priority] = _proclocal->nextReadyProcess;
            if (readyHeads[priority] == NULL)
                readyTails[priority] = NULL;

            _proclocal->nextReadyProcess = NULL;

            // Only schedule READY processes.
            if (_proclocal->status == PROCSTATE_READY)
                return _proclocal;
        }
    }
    return NULL;
}

// Test19 Add - Check if ready queue has process at priority to switch to
int process_in_ready_queue(int pri)
{
    return (pri >= LOWEST_PRIORITY && pri <= HIGHEST_PRIORITY && readyHeads[pri] != NULL);
}

// Test27 Check if a PID exists anywhere in the process table, regardless of current status
int process_check_pid_exists(int pid)
{
    for (int i = 0; i < MAXPROC; i++)
    {
        if (processTable[i].pid == pid)
            return 1;   // count it as existing even if status is EMPTY (reaped case)
    }
    return 0;
}
