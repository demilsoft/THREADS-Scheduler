
#include <stdio.h>
#include "THREADSLib.h"
#include "SchedulerTesting.h"
#include "Scheduler.h"

/*********************************************************************************
*
* SchedulerTest02
*
* Spawns one child process at priority 3.  The child process spawns two
* children at a higher priority (4).  Those children should preempt their parent
* at spawn().
*
* SchedulerTest02: started
* SchedulerTest02: after spawn of child with pid 3
* SchedulerTest02: waiting for child process
* SchedulerTest02-Child1: started
* SchedulerTest02-Child1: performing spawn of first child
* SchedulerTest02-Child1-Child1: started
* SchedulerTest02-Child1-Child1: quitting
* SchedulerTest02-Child1: spawn of first child returned pid = 4
* SchedulerTest02-Child1: performing spawn of second child
* SchedulerTest02-Child1-Child2: started
* SchedulerTest02-Child1-Child2: quitting
* SchedulerTest02-Child1: spawn of second child returned pid = 5
* SchedulerTest02-Child1: performing first join.
* SchedulerTest02-Child1: exit status for child 4 is -3
* SchedulerTest02-Child1: performing second join.
* SchedulerTest02-Child1: exit status for child 5 is -3
* SchedulerTest02: exit status for child 3 is -3
* All processes completed.
*********************************************************************************/
int SchedulerEntryPoint(void* pArgs)
{
    int status = -1, pid1, kidpid = -1;
    char* testName = "SchedulerTest02";
    char nameBuffer[512];

    console_output(FALSE, "\n%s: started\n", testName);

    /* Use the -Child naming convention for the child process name. */
    snprintf(nameBuffer, sizeof(nameBuffer), "%s-Child1", testName);

    pid1 = k_spawn(nameBuffer, SpawnTwoPriorityFour, nameBuffer, THREADS_MIN_STACK_SIZE, 3);
    console_output(FALSE, "%s: after spawn of child with pid %d\n", testName, pid1);

    console_output(FALSE, "%s: waiting for child process\n", testName);
    kidpid = k_wait(&status);

    console_output(FALSE, "%s: exit status for child %d is %d\n", testName, kidpid, status);

    return 0;
}

