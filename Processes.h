////////////////////////////////////////////////////////////////////
// CYBV 489
// SP 2026
// Dean Lewis
// Processes.h
////////////////////////////////////////////////////////////////////
// This file defines the Process struct and related helper functions for process management. 
// The Process struct represents an individual process in the system and contains information about the process's state
// context, parent-child relationships, and other relevant data. The helper functions provide functionality for initializing
// the process table, finding free slots for new processes, managing parent-child relationships, and handling ready queues for scheduling.

#pragma once
#include "THREADSLib.h"
#include "Scheduler.h" 

// DEFINE STD PROCESS STATE MACROS
#define PROCSTATE_EMPTY       0
#define PROCSTATE_READY       1
#define PROCSTATE_RUNNING     2
#define PROCSTATE_BLOCKED     3
#define PROCSTATE_TERMINATE   4

// Adding typedef to handle multiple block types
typedef enum
{
	BLOCK_NONE = 0,
	BLOCK_WAIT,
	BLOCK_JOIN
} block_reason_t;

/*
 * Forward declare Process so ksem_t can reference Process* before
 * the full struct _process is defined.
 */
typedef struct _process Process;

// Adding race logic to utilize a semaphore Test16 Add
typedef struct
{
	int count;                        // for an event, 0/1
	Process* waiters;                 // head of blocked queue
	Process* waitersTail;             // tail of blocked queue  // Test17 Add
} ksem_t;

typedef struct _process
{
	struct			_process* nextReadyProcess;
	struct			_process* nextSiblingProcess;
	struct			_process* pParent;
	struct			_process* pChildren;
	struct			_process* nextBlocked;			// Semaphore wait queue Test 15 Add
	char			name[MAXNAME];					// Process name 
	char			startArgs[MAXARG];				// Process arguments
	void*			context;						// Process's current context 
	short			pid;							// Process id (pid) 
	int				priority;
	int				(*entryPoint) (void*);			// The entry point that is called from launch 
	unsigned int	stacksize;						// likely will not use
	int				status;							// READY, QUIT, BLOCKED, etc.
	DWORD 			cpuTime;                        // Total CPU time used by process	
	DWORD			lastStartTime;					// last process start time	
	int 			numChildren;                    // Number of child processes	
	int				receivedSignal;					// 0 none, 1 was signaled
	block_reason_t  blockReason;					// Handles BLOCK TYPE
	int				blockedPid;						// pid this process is blocked (JOIN)
} Process;

// EXTERNAL FUNCTION DECLARATIONS
extern Process processTable[MAXPROC];
extern int nextPid;
extern int exitCodeSlot[MAXPROC];

// FUNCTION PROTOTYPES //
// SEE FUNCTION DEFINITIONS FOR DESCRIPTIONS //
Process* process_find_by_pid(int pid);
Process* process_find_term_child(Process* parent, Process** pPrevOut);
Process* process_next_ready(void);
void processes_init(void);
int process_find_free_slot(void);
void process_add_child(Process* parent, Process* child);
void process_remove_term_child(Process* parent, Process* child, Process* prev);
void process_add_ready(Process* p);
int process_in_ready_queue(int pri);
int process_check_pid_exists(int pid);

