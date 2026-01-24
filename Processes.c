#define _CRT_SECURE_NO_WARNINGS
#include <string.h>

#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

// DECLARATIONS
Process processTable[MAXPROC];                  // Keep track of processes in OS (MAXPROC is set to 50)
int nextPid = 1;

/* Exit code storage (indexed by process table slot) */
int exitCodeSlot[MAXPROC] = { 0 };

/* Ready queues (indexed by priority) */
static Process* readyHeads[HIGHEST_PRIORITY + 1];
static Process* readyTails[HIGHEST_PRIORITY + 1];

/* PROCESS TABLE INITIALIZATION */
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

        processTable[i].name[0] = '\0';
        processTable[i].startArgs[0] = '\0';
        processTable[i].context = NULL;

        processTable[i].pid = 0;
        processTable[i].priority = 0;
        processTable[i].entryPoint = NULL;
        processTable[i].stacksize = 0;
        processTable[i].status = PROCSTATE_EMPTY;

        exitCodeSlot[i] = 0;
    }

    for (int p = 0; p <= HIGHEST_PRIORITY; p++)
    {
        readyHeads[p] = NULL;
        readyTails[p] = NULL;
    }
}

/* PROCESS TABLE HELPERS  */
// Finds an unused entry in the process table to allocate for a new process.
int process_find_free_slot(void)
{
    for (int i = 1; i < MAXPROC; i++)   /* leave slot 0 unused */
    {
        if (processTable[i].status == PROCSTATE_EMPTY)
            return i;
    }
    return -1;
}

// Searches the process table for a process by pid
Process* process_find_by_pid(int pid)
{
    for (int i = 1; i < MAXPROC; i++)
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
    child->pParent = parent;
    child->nextSiblingProcess = parent->pChildren;
    parent->pChildren = child;
}

// Searches a parent process's child list for a child that has terminated
Process* process_find_quit_child(Process* parent, Process** pPrevOut)
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
void process_remove_child_link(Process* parent, Process* child, Process* prev)
{
    if (prev == NULL) parent->pChildren = child->nextSiblingProcess;
    else prev->nextSiblingProcess = child->nextSiblingProcess;

    child->nextSiblingProcess = NULL;
}

/* QUEUE HELPER FUNCTIONS */
// Adds a process to the ready queue based on its priority.
void ready_enqueue(Process* p)
{
    int pr = p->priority;
    p->nextReadyProcess = NULL;

    if (readyTails[pr] == NULL)
    {
        readyHeads[pr] = readyTails[pr] = p;
    }
    else
    {
        readyTails[pr]->nextReadyProcess = p;
        readyTails[pr] = p;
    }
}

// Selects and removes the highest priority READY process from the ready queues. 
// Priority is determined from highest to lowest.
Process* ready_dequeue_highest(void)
{
    for (int pr = HIGHEST_PRIORITY; pr >= LOWEST_PRIORITY; pr--)
    {
        if (readyHeads[pr] != NULL)
        {
            Process* p = readyHeads[pr];
            readyHeads[pr] = p->nextReadyProcess;
            if (readyHeads[pr] == NULL) readyTails[pr] = NULL;
            p->nextReadyProcess = NULL;
            return p;
        }
    }
    return NULL;
}
