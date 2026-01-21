///////////////////////////////////////////////////////////////////////////////
// Changes to this file should be reflected in Processes.c
///////////////////////////////////////////////////////////////////////////////
#pragma once

typedef struct _process
{
	struct			_process* nextReadyProcess;
	struct			_process* nextSiblingProcess;

	struct			_process* pParent;
	struct			_process* pChildren;

	char			name[MAXNAME];			// Process name 
	char			startArgs[MAXARG];		// Process arguments
	void*			context;				// Process's current context 
	short			pid;					// Process id (pid) 
	int				priority;
	int				(*entryPoint) (void*);  // The entry point that is called from launch 
	//char*			stack;					// WILL NOT USE THIS - CAN BE REMOVED
	unsigned int	stacksize;
	int				status;					// READY, QUIT, BLOCKED, etc.

	/* WHAT ELSE WILL WE NEED TO TRACK? ADD BELOW */

} Process;

// ADD CODE TO MANAGE PROCESS STRUCTURE AS NEEDED
// FOR EXAMPLE, FUNCTIONS TO CREATE, DELETE, AND MANIPULATE PROCESS STRUCTURES
