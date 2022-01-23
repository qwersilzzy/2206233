// File: TwoTasks.c

#include <stdio.h>
#include "includes.h"
#include <string.h>

#include "system.h"
#include <time.h>
#include "alt_types.h"
#include <altera_avalon_performance_counter.h>


#define DEBUG 1
#define CONTEXT_SWITCH_MEASUREMENTS 10


/* Definition of Task Stacks */
/* Stack grows from HIGH to LOW memory */
#define   TASK_STACKSIZE       2048
OS_STK    task0_stk[TASK_STACKSIZE];
OS_STK    task1_stk[TASK_STACKSIZE];
OS_STK    stat_stk[TASK_STACKSIZE];

/* Definition of Task Priorities */
#define TASK0_PRIORITY      8  // highest priority
#define TASK1_PRIORITY      7
#define TASK_STAT_PRIORITY 12  // lowest priority

INT8U task0state = 0;
INT8U task1state = 0;
INT8U task0oldstate = 0;
INT8U task1oldstate = 0;

INT32U Task1toTask0_ContextSwitchTimes[CONTEXT_SWITCH_MEASUREMENTS];
INT32U Task0toTask1_ContextSwitchTimes[CONTEXT_SWITCH_MEASUREMENTS];

INT32U  Task1toTask0_ContextSwitchCounter = 0;

INT32U  Task0toTask1_ContextSwitchCounter = 0;

OS_EVENT *Task0Printing;
OS_EVENT *Task1Printing;

alt_u32 ticks;
alt_u64 ticks_u64;
alt_u32 time_1;
alt_u32 time_2;
alt_u32 timer_overhead;

alt_u32 Task0toTask1_ContextSwitchTicks;
alt_u32 Task1toTask0_ContextSwitchTicks;

float microseconds(int ticks)
{
  return (float) 1000000 * (float) ticks / (float) 50000000;
}


void printStackSize(char* name, INT8U prio)
{
  INT8U err;
  OS_STK_DATA stk_data;

  err = OSTaskStkChk(prio, &stk_data);
  if (err == OS_NO_ERR) {
    if (DEBUG == 1)
      printf("%s (priority %d) - Used: %d; Free: %d\n",
         name, prio, stk_data.OSUsed, stk_data.OSFree);
  }
  else
    {
      if (DEBUG == 1)
    printf("Stack Check Error!\n");
    }
}

/* Prints a message and sleeps for given time interval */
void task0(void* pdata)
{
  INT8U err,i;
  INT32S c_sum,avg;
  while (1)
  {

     task0oldstate = task0state;

     if (task1oldstate == 1)
     {
       OSSemPend(Task0Printing,0,&err);
       PERF_END(PERFORMANCE_COUNTER_BASE,1);
       Task1toTask0_ContextSwitchTicks = (alt_u32)perf_get_section_time((void*)PERFORMANCE_COUNTER_BASE,1);
       if (Task1toTask0_ContextSwitchCounter < CONTEXT_SWITCH_MEASUREMENTS)
       {
         Task1toTask0_ContextSwitchTimes[Task1toTask0_ContextSwitchCounter++] = Task1toTask0_ContextSwitchTicks;
       } else
       {
         avg = 0;
         c_sum = 0;
         for (i=0;i<CONTEXT_SWITCH_MEASUREMENTS;i++)
         {
           c_sum = c_sum + Task1toTask0_ContextSwitchTimes[i];
         }
         printf("Task 1 --> Task 0: %5.2f us\n",/*CONTEXT_SWITCH_MEASUREMENTS,*/microseconds(c_sum/CONTEXT_SWITCH_MEASUREMENTS - timer_overhead));
         Task1toTask0_ContextSwitchCounter = 0;
       }

     }

     if (task0state == 0)
     {
       PERF_RESET(PERFORMANCE_COUNTER_BASE);
       /* Start global measurement */
       PERF_START_MEASURING(PERFORMANCE_COUNTER_BASE);
       /* Start measurement in "section" 1 */
       PERF_BEGIN(PERFORMANCE_COUNTER_BASE,1);
       OSSemPost(Task1Printing);
     }

     if (task0state == 0)
     {
       task0state = 1;
     } else if (task0state == 1)
     {
       task0state = 0;
     }

     OSTimeDlyHMSM(0, 0, 0, 4);
                                   /* Context Switch to next task
                   * Task will go to the ready state
                   * after the specified delay
                   */
    }
}

/* Prints a message and sleeps for given time interval */
void task1(void* pdata)
{
  INT8U err,i;
  INT32S c_sum,avg;
  while (1)
    {

       task1oldstate = task1state;

       if (task1oldstate == 0)
       {
         OSSemPend(Task1Printing,0,&err);
         PERF_END(PERFORMANCE_COUNTER_BASE,1);
         Task0toTask1_ContextSwitchTicks = (alt_u32)perf_get_section_time((void*)PERFORMANCE_COUNTER_BASE, 1);

         if (Task0toTask1_ContextSwitchCounter < CONTEXT_SWITCH_MEASUREMENTS)
         {
           Task0toTask1_ContextSwitchTimes[Task0toTask1_ContextSwitchCounter++] = Task0toTask1_ContextSwitchTicks;
         } else
         {
           avg = 0;
           c_sum = 0;
           for (i=0;i<CONTEXT_SWITCH_MEASUREMENTS;i++)
           {
             c_sum = c_sum + Task0toTask1_ContextSwitchTimes[i];
           }
           printf("Task 0 --> Task 1: %5.2f us\n",microseconds(c_sum/CONTEXT_SWITCH_MEASUREMENTS - timer_overhead));
           Task0toTask1_ContextSwitchCounter = 0;
         }
       }


       //printf("Task 1 - State %d\n",task1state);
       if (task0state == 1)
       {
         PERF_RESET(PERFORMANCE_COUNTER_BASE);
         /* Start global measurement */
         PERF_START_MEASURING(PERFORMANCE_COUNTER_BASE);
         /* Start measurement in "section" 1 */
         PERF_BEGIN(PERFORMANCE_COUNTER_BASE,1);
         OSSemPost(Task0Printing);
       }

       if (task1state == 0)
       {
         task1state = 1;
       } else if (task1state == 1)
       {
         task1state = 0;
       }

      OSTimeDlyHMSM(0, 0, 0,4);
    }
}

/* Printing Statistics */
void statisticTask(void* pdata)
{
  while(1)
    {
      //printStackSize("Task1", TASK1_PRIORITY);
      //printStackSize("Task2", TASK2_PRIORITY);
      //printStackSize("StatisticTask", TASK_STAT_PRIORITY);
    }
}

/* The main function creates two task and starts multi-tasking */
int main(void)
{
  printf("Lab 3 - Two Tasks\n");

  OSTaskCreateExt
    ( task0,                        // Pointer to task code
      NULL,                         // Pointer to argument passed to task
      &task0_stk[TASK_STACKSIZE-1], // Pointer to top of task stack
      TASK0_PRIORITY,               // Desired Task priority
      TASK0_PRIORITY,               // Task ID
      &task0_stk[0],                // Pointer to bottom of task stack
      TASK_STACKSIZE,               // Stacksize
      NULL,                         // Pointer to user supplied memory (not needed)
      OS_TASK_OPT_STK_CHK |         // Stack Checking enabled
      OS_TASK_OPT_STK_CLR           // Stack Cleared
      );

  OSTaskCreateExt
    ( task1,                        // Pointer to task code
      NULL,                         // Pointer to argument passed to task
      &task1_stk[TASK_STACKSIZE-1], // Pointer to top of task stack
      TASK1_PRIORITY,               // Desired Task priority
      TASK1_PRIORITY,               // Task ID
      &task1_stk[0],                // Pointer to bottom of task stack
      TASK_STACKSIZE,               // Stacksize
      NULL,                         // Pointer to user supplied memory (not needed)
      OS_TASK_OPT_STK_CHK |         // Stack Checking enabled
      OS_TASK_OPT_STK_CLR           // Stack Cleared
      );

  if (DEBUG == 1)
    {
      OSTaskCreateExt
    ( statisticTask,                // Pointer to task code
      NULL,                         // Pointer to argument passed to task
      &stat_stk[TASK_STACKSIZE-1],  // Pointer to top of task stack
      TASK_STAT_PRIORITY,           // Desired Task priority
      TASK_STAT_PRIORITY,           // Task ID
      &stat_stk[0],                 // Pointer to bottom of task stack
      TASK_STACKSIZE,               // Stacksize
      NULL,                         // Pointer to user supplied memory (not needed)
      OS_TASK_OPT_STK_CHK |         // Stack Checking enabled
      OS_TASK_OPT_STK_CLR           // Stack Cleared
      );
    }
  Task0Printing = OSSemCreate(1);
  Task1Printing = OSSemCreate(1);

  /* Calculate Timer Overhead */
  // Average of 10 measurements */
  int i;
  timer_overhead = 0;
  for (i = 0; i < 100; i++)
  {
    /* Initialize Performance counter core */
    PERF_RESET(PERFORMANCE_COUNTER_BASE);

    /* Start global measurement */
    PERF_START_MEASURING(PERFORMANCE_COUNTER_BASE);

    /* Start measurement in "section" 1 */
    PERF_BEGIN(PERFORMANCE_COUNTER_BASE,1);

    /* Stop measurement */
    PERF_END(PERFORMANCE_COUNTER_BASE,1);

    /* Retrieve number of ticks elapsed for measurement in section 1 */
    ticks = (alt_u32)perf_get_section_time((void*)PERFORMANCE_COUNTER_BASE, 1);

    /* Sum timer overhead */
    timer_overhead = timer_overhead + ticks;
  }

  timer_overhead = timer_overhead / 100;



  OSStart();
  return 0;
}
