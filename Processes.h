// Dean Lewis
// Processes.h Helper File
///////////////////////////////////////////////////////////////////////////////
#pragma once

// DEFINE STD PROCESS STATE MACROS
#define PROCSTATE_EMPTY       0
#define PROCSTATE_READY       1
#define PROCSTATE_RUNNING     2
#define PROCSTATE_BLOCKED     3
#define PROCSTATE_TERMINATED  4

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

