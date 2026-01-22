// Dean Lewis
// Processes.h Helper File
///////////////////////////////////////////////////////////////////////////////
#pragma once

// DEFINE STD PROCESS STATES
//#define PROC_EMPTY   0
//#define PROC_READY   1
//#define PROC_RUNNING 2
//#define PROC_BLOCKED 3
//#define PROC_QUIT    4

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

//static void init_process_table(void)
//{
//    for (int i = 0; i < MAXPROC; i++)
//    {
//        processTable[i].nextReadyProcess = NULL;
//        processTable[i].nextSiblingProcess = NULL;
//        processTable[i].pParent = NULL;
//        processTable[i].pChildren = NULL;
//
//        processTable[i].name[0] = '\0';
//        processTable[i].startArgs[0] = '\0';
//        processTable[i].context = NULL;
//
//        processTable[i].pid = 0;
//        processTable[i].priority = 0;
//        processTable[i].entryPoint = NULL;
//        //processTable[i].stack = NULL;  // NOT UN USE
//        processTable[i].stacksize = 0;
//        processTable[i].status = PROC_EMPTY;
//
//        exitCodeSlot[i] = 0;
//    }
//
//    for (int p = 0; p <= HIGHEST_PRIORITY; p++)
//    {
//        readyHeads[p] = NULL;
//        readyTails[p] = NULL;
//    }
//}