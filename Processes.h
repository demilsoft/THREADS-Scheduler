////////////////////////////////////////////////////////////////////
// CYBV 489
// Dean Lewis
// Processes.h
////////////////////////////////////////////////////////////////////
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

	// struct			_process* pActiveChildren;			// Likely wont need this...addressing in k_wait
	// struct			_process* pChildrenThatExited;		// Likely wont need this...addressing in k_wait

	char			name[MAXNAME];					// Process name 
	char			startArgs[MAXARG];				// Process arguments
	void*			context;						// Process's current context 
	short			pid;							// Process id (pid) 
	int				priority;
	int				(*entryPoint) (void*);			// The entry point that is called from launch 
	//char*			stack;							// NOT IN USE
	unsigned int	stacksize;						// likely will not use
	int				status;							// READY, QUIT, BLOCKED, etc.
	/* WHAT ELSE WILL WE NEED TO TRACK? ADD BELOW */
	// int 			cpuTime;                        // Total CPU time used by process		
	// int 			myExitCode;                     // Exit code when process terminates	

} Process;

extern Process processTable[MAXPROC];
extern int nextPid;
extern int exitCodeSlot[MAXPROC];

// FUNCTION PROTOTYPES //
Process* find_process_by_pid(int pid);
Process* process_find_term_child(Process* parent, Process** pPrevOut);
Process* next_ready_process(void);
void processes_init(void);
int process_find_free_slot(void);
void process_add_child(Process* parent, Process* child);
void remove_term_child(Process* parent, Process* child, Process* prev);
void add_ready_process(Process* p);