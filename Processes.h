// Dean Lewis
// Processes.h Helper File
///////////////////////////////////////////////////////////////////////////////
#pragma once
#include "THREADSLib.h"
#include "Scheduler.h" 

// DEFINE STD PROCESS STATE MACROS
#define PROCSTATE_EMPTY       0
#define PROCSTATE_READY       1
#define PROCSTATE_RUNNING     2
#define PROCSTATE_BLOCKED     3
#define PROCSTATE_TERMINATE   4

typedef struct _process
{
	struct			_process* nextReadyProcess;
	struct			_process* nextSiblingProcess;
	struct			_process* pParent;
	struct			_process* pChildren;

	char			name[MAXNAME];					// Process name 
	char			startArgs[MAXARG];				// Process arguments
	void*			context;						// Process's current context 
	short			pid;							// Process id (pid) 
	int				priority;
	int				(*entryPoint) (void*);			// The entry point that is called from launch 
	//char*			stack;							// WILL NOT USE THIS - CAN BE REMOVED
	unsigned int	stacksize;
	int				status;							// READY, QUIT, BLOCKED, etc.

	/* WHAT ELSE WILL WE NEED TO TRACK? ADD BELOW */

} Process;

extern Process processTable[MAXPROC];
extern int nextPid;
extern int exitCodeSlot[MAXPROC];

void processes_init(void);

int process_find_free_slot(void);
Process* process_find_by_pid(int pid);

void process_add_child(Process* parent, Process* child);
Process* process_find_quit_child(Process* parent, Process** pPrevOut);
void process_remove_child_link(Process* parent, Process* child, Process* prev);

void ready_enqueue(Process* p);
Process* ready_dequeue_highest(void);