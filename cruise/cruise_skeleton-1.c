#include <stdio.h>
#include "system.h"
#include "includes.h"
#include "altera_avalon_pio_regs.h"
#include "sys/alt_irq.h"
#include "sys/alt_alarm.h"
#include "os_cfg.h"

extern void delay_asm (int millisec);

#define DEBUG 1

#define HW_TIMER_PERIOD 100 /* 100ms */

/* Button Patterns */

#define GAS_PEDAL_FLAG      0x08
#define BRAKE_PEDAL_FLAG    0x04
#define CRUISE_CONTROL_FLAG 0x02
/* Switch Patterns */

#define TOP_GEAR_FLAG       0x00000002
#define ENGINE_FLAG         0x00000001

/* LED Patterns */

#define LED_RED_0 0x00000001 // Engine
#define LED_RED_1 0x00000002 // Top Gear

#define LED_GREEN_0 0x0001 // Cruise Control activated
#define LED_GREEN_2 0x0002 // Cruise Control Button
#define LED_GREEN_4 0x0010 // Brake Pedal
#define LED_GREEN_6 0x0040 // Gas Pedal

/*
 * Definition of Tasks
 */

#define TASK_STACKSIZE 2048

OS_STK StartTask_Stack[TASK_STACKSIZE];
OS_STK ControlTask_Stack[TASK_STACKSIZE];
OS_STK VehicleTask_Stack[TASK_STACKSIZE];


OS_STK ButtonIOTask_Stack[TASK_STACKSIZE];
OS_STK SwitchIOTask_Stack[TASK_STACKSIZE];

OS_STK ExtraLoadTask_Stack[TASK_STACKSIZE];
OS_STK OverloadDetectionTask_Stack[TASK_STACKSIZE];
OS_STK WatchDogTask_Stack[TASK_STACKSIZE];


// Task Priorities

#define STARTTASK_PRIO     5
#define VEHICLETASK_PRIO  10
#define CONTROLTASK_PRIO  12

#define ButtonIOTASK_PRIO  3
#define SwitchIOTASK_PRIO  2


#define ExtraLoadTASK_PRIO 6
#define OverloadDetectionTASK_PRIO 7
#define WatchDogTASK_PRIO 1


// Task Periods

#define CONTROL_PERIOD  300
#define VEHICLE_PERIOD  300

/*
 * Definition of Kernel Objects
 */

// Mailboxes
OS_EVENT *Mbox_Throttle;
OS_EVENT *Mbox_Velocity;



OS_EVENT *Mbox_CruiseControl;
OS_EVENT *Mbox_GasPedal;

OS_EVENT *Mbox_Engine;
OS_EVENT *Mbox_Gear;

OS_EVENT *Mbox_BrakePedal;


// Semaphores

OS_EVENT *VehicleTaskSem;
OS_EVENT *ControlTaskSem;
OS_EVENT *ButtonIOTaskSem;
OS_EVENT *SwitchIOTaskSem;


OS_EVENT *ExtraLoadTaskSem;
OS_EVENT *OverloadDetectionTaskSem;
OS_EVENT *WatchDogTaskSem;



// SW-Timer

OS_TMR *VehicleTaskTmr;
OS_TMR *ControlTaskTmr;
OS_TMR *ButtonIOTaskTmr;
OS_TMR *SwitchIOTaskTmr;


OS_TMR *ExtraLoadTaskTmr;
OS_TMR *OverloadDetectionTaskTmr;
OS_TMR *WatchDogTaskTmr;


INT8U CC_CurrentState  = 0;
INT8U CC_PreviousState = 0;
INT8U CC_Flag = 0;

INT8U Engine_CurrentState  = 0;
INT8U Engine_PreviousState = 0;
INT8U Engine_Flag = 0;

INT16S Target_Velocity=0;
INT16S Utilization = 0;


INT32S PositionLEDPattern = 0;
INT32S OverloadDetectionFlag = 1;


void VehicleTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(VehicleTaskSem);
  //printf("VehicleTaskTimerCallback, ska anropa OSSemPost\n");
  //printf("OSSemPost(VehicleTaskSem) returned;\n");
}

void ControlTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(ControlTaskSem);
  //printf("OSSemPost(ControlTaskSem) returned;\n");
}

void ButtonIOTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(ButtonIOTaskSem);
  //printf("OSSemPost(ButtonIOTaskSem) returned;\n");
}

void SwitchIOTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(SwitchIOTaskSem);
  //printf("OSSemPost(SwitchIOTaskSem) returned;\n");
}



void ExtraLoadTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(ExtraLoadTaskSem);
}


void OverloadDetectionTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(OverloadDetectionTaskSem);
}

void WatchDogTaskTmrCallback (void *ptmr, void *callback_arg){
  OSSemPost(WatchDogTaskSem);
}




/*
 * Types
 */
enum active {on, off};

enum active gas_pedal = off;
enum active brake_pedal = off;
enum active top_gear = off;
enum active engine = off;
enum active engine_active = off;

enum active cruise_control = off;

enum active cruise_control_active = off;
/*
 * Global variables
 */
int delay; // Delay of HW-timer
INT16U led_green = 0; // Green LEDs
INT32U led_red = 0;   // Red LEDs

int buttons_pressed(void)
{
  return ~IORD_ALTERA_AVALON_PIO_DATA(D2_PIO_KEYS4_BASE);
}

int switches_pressed(void)
{
  return IORD_ALTERA_AVALON_PIO_DATA(DE2_PIO_TOGGLES18_BASE);
}

/*
 * ISR for HW Timer
 */
alt_u32 alarm_handler(void* context)
{
  OSTmrSignal(); /* Signals a 'tick' to the SW timers */

  return delay;
}

static int b2sLUT[] = {0x40, //0
               0x79, //1
               0x24, //2
               0x30, //3
               0x19, //4
               0x12, //5
               0x02, //6
               0x78, //7
               0x00, //8
               0x18, //9
               0x3F, //-
};

/*
 * convert int to seven segment display format
 */
int int2seven(int inval){
  return b2sLUT[inval];
}

/*
 * output current velocity on the seven segement display
 */
void show_velocity_on_sevenseg(INT8S velocity){
  int tmp = velocity;
  int out;
  INT8U out_high = 0;
  INT8U out_low = 0;
  INT8U out_sign = 0;

  if(velocity < 0){
    out_sign = int2seven(10);
    tmp *= -1;
  }else{
    out_sign = int2seven(0);
  }

  out_high = int2seven(tmp / 10);
  out_low = int2seven(tmp - (tmp/10) * 10);

  out = int2seven(0) << 21 |
    out_sign << 14 |
    out_high << 7  |
    out_low;
  IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_LOW28_BASE,out);
}

/*
 * shows the target velocity on the seven segment display (HEX5, HEX4)
 * when the cruise control is activated (0 otherwise)
 */
void show_target_velocity(INT8S target_vel)
{
  int tmp = target_vel;
  int out;
  INT8U out_high = 0;
  INT8U out_low = 0;
  INT8U out_sign = 0;

  if(target_vel < 0){
    out_sign = int2seven(10);
    tmp *= -1;
  }else{
    out_sign = int2seven(0);
  }

  out_high = int2seven(tmp / 10);
  out_low = int2seven(tmp - (tmp/10) * 10);

  out = int2seven(0) << 21 |
    out_sign << 14 |
    out_high << 7  |
    out_low;
  IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_HEX_HIGH28_BASE,out);
}

void show_position(INT16U position)
{
  // The bit pattern calculated for the position
  // is written to the LED bar in the SwitchIOTask function
  PositionLEDPattern = 0;
  if (position>0)
  {
    PositionLEDPattern = PositionLEDPattern | 0x20000;
  }

  if (position>3999)
  {
    PositionLEDPattern = PositionLEDPattern | 0x10000;
  }

  if (position>7999)
  {
    PositionLEDPattern = PositionLEDPattern | 0x08000;
  }

  if (position>11999)
  {
    PositionLEDPattern = PositionLEDPattern | 0x04000;
  }

  if (position>15999)
  {
    PositionLEDPattern = PositionLEDPattern | 0x02000;
  }

  if (position>19999)
  {
    PositionLEDPattern = PositionLEDPattern | 0x01000;
  }
}

/*
 * The function 'adjust_position()' adjusts the position depending on the
 * acceleration and velocity.
 */
INT16U adjust_position(INT16U position, INT16S velocity,
               INT8S acceleration, INT16U time_interval)
{
  INT16S new_position = position + velocity * time_interval / 1000
    + acceleration / 2  * (time_interval / 1000) * (time_interval / 1000);

  if (new_position > 24000) {
    new_position -= 24000;
  } else if (new_position < 0){
    new_position += 24000;
  }

  show_position(new_position);
  return new_position;
}

/*
 * The function 'adjust_velocity()' adjusts the velocity depending on the
 * acceleration.
 */
INT16S adjust_velocity(INT16S velocity, INT8S acceleration,
               enum active brake_pedal, INT16U time_interval)
{
  INT16S new_velocity;
  INT8U brake_retardation = 200;

  if (brake_pedal == off)
    new_velocity = velocity  + (float) (acceleration * time_interval) / 1000.0;
  else {
    if (brake_retardation * time_interval / 1000 > velocity)
      new_velocity = 0;
    else
      new_velocity = velocity - brake_retardation * time_interval / 1000;
  }

  return new_velocity;
}

/*
 * The task 'VehicleTask' updates the current velocity of the vehicle
 */
void VehicleTask(void* pdata)
{
  INT8U err;
  void* msg;
  INT8U* throttle;
  INT8S acceleration;  /* Value between 40 and -20 (4.0 m/s^2 and -2.0 m/s^2) */
  INT8S retardation;   /* Value between 20 and -10 (2.0 m/s^2 and -1.0 m/s^2) */
  INT16U position = 0; /* Value between 0 and 20000 (0.0 m and 2000.0 m)  */
  INT16S velocity = 0; /* Value between -200 and 700 (-20.0 m/s amd 70.0 m/s) */
  INT16S wind_factor;   /* Value between -10 and 20 (2.0 m/s^2 and -1.0 m/s^2) */

  printf("Vehicle task created!\n");


  while(1)
  {
      OSSemPend(VehicleTaskSem,0,&err);
      //printf("VehicleTask, returnerade fr�n OSSemPend\n");
      err = OSMboxPost(Mbox_Velocity, (void *) &velocity);

      //OSTimeDlyHMSM(0,0,0,VEHICLE_PERIOD);

      /* Non-blocking read of mailbox:
     - message in mailbox: update throttle
     - no message:         use old throttle
      */
      msg = OSMboxPend(Mbox_Throttle, 1, &err);
      if (err == OS_NO_ERR)
          throttle = (INT8U*) msg;

      /* Retardation : Factor of Terrain and Wind Resistance */
      if (velocity > 0)
    wind_factor = velocity * velocity / 10000 + 1;
      else
    wind_factor = (-1) * velocity * velocity / 10000 + 1;

      if (position < 4000)
    retardation = wind_factor; // even ground
      else if (position < 8000)
    retardation = wind_factor + 15; // traveling uphill
      else if (position < 12000)
    retardation = wind_factor + 25; // traveling steep uphill
      else if (position < 16000)
    retardation = wind_factor; // even ground
      else if (position < 20000)
    retardation = wind_factor - 10; //traveling downhill
      else
    retardation = wind_factor - 5 ; // traveling steep downhill

      acceleration = *throttle / 2 - retardation;
      position = adjust_position(position, velocity, acceleration, 300);
      velocity = adjust_velocity(velocity, acceleration, brake_pedal, 300);

      printf("Position: %dm\n", position / 10);
      printf("Velocity: %4.1fm/s\n", velocity /10.0);
      printf("Throttle: %dV\n", *throttle / 10);
      show_velocity_on_sevenseg((INT8S) (velocity / 10));
    }
}

/*
 * The task 'ControlTask' is the main task of the application. It reacts
 * on sensors and generates responses.
 */

void ControlTask(void* pdata)
{
  INT8U err;

  //INT8U *CruiseControl,*GasPedal;

  //INT8U *Engine,*Gear;

  INT8U gas_p;


  INT8U throttle = 40; /* Value between 0 and 80, which is interpreted as between 0.0V and 8.0V */
  void* msg;

  INT16S* current_velocity;

  printf("Control Task created!\n");

  while(1)
  {
    CC_PreviousState = CC_CurrentState;
    Engine_PreviousState = Engine_CurrentState;

    OSSemPend(ControlTaskSem,0,&err);
    msg = OSMboxPend(Mbox_Velocity, 0, &err);
    current_velocity = (INT16S*) msg;

    if (cruise_control == on)
    {
      CC_Flag = 1;
    } else
    {
      CC_Flag = 0;
    }

    if (engine == on)
    {
      Engine_Flag = 1;
    } else
    {
      Engine_Flag = 0;
    }



    // Check if conditions are acceptable to turn off engine
    if ( (Engine_Flag==0) && Engine_PreviousState && (*current_velocity==0))
    {
      engine_active = off;
      Engine_CurrentState = 0;
    } else if (Engine_Flag)
    {
      Engine_CurrentState = 1;
      engine_active = on;
    }

    // Check if conditions are acceptable for CC to be activated
    //
    if ((CC_Flag) && ((gas_pedal == off) && (top_gear == on) && (*current_velocity>199)))
    {
      //Target_Velocity = (INT8S)(*current_velocity/10);
      Target_Velocity = (INT16S)(*current_velocity);
      CC_CurrentState = 1;
      cruise_control_active = on;
    }

    // Check if we should disable Cruise Control
    if ((gas_pedal == on) || (top_gear == off) || (*current_velocity<200))
    {
      Target_Velocity = 0;
      cruise_control_active = off;
      CC_CurrentState = 0;
    }

    //show_target_velocity((INT8S)Target_Velocity);
    show_target_velocity((INT8S)(Target_Velocity/10));

    if ((gas_pedal == on) && (engine_active == on))
    {
      gas_p = 1;
    } else
    {
      gas_p = 0;
    }

    if (CC_CurrentState == 1)
    {
      if ((INT16S)(*current_velocity) < (Target_Velocity))
      {
        gas_p = 1;
      }
    }

    if (engine_active == off)
    {
      gas_p = 0;
    }

    // Send throttle position to VehicleTask
    throttle = (INT8U)(gas_p)*80;
    err = OSMboxPost(Mbox_Throttle, (void *) &throttle);

  }
}



void SwitchIOTask(void* pdata)
{

  INT32S sw;
  INT8U err;

  INT16U Gear;

  while(1)
  {
    INT32S tmp = 0;
    OSSemPend(SwitchIOTaskSem,0,&err);
    sw = switches_pressed();
    Utilization = ((sw >> 4) & 0x3F);
    if (sw & ENGINE_FLAG)
    {
      tmp = tmp | LED_RED_0;
      engine = on;
    } else
    {
      engine = off;
    }

    if (sw & TOP_GEAR_FLAG)
    {
      top_gear = on;
      tmp = tmp | LED_RED_1;
      Gear = 1;
    } else
    {
      top_gear = off;
      Gear = 0;
    }
    IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_REDLED18_BASE,tmp|PositionLEDPattern);
  }
}



void ButtonIOTask(void* pdata)
{
  INT8U err;
  int btn;
  INT8U GasPedal, CruiseControl, BrakePedal, GreenLeds;

  while(1)
  {
    GreenLeds = 0;
    OSSemPend(ButtonIOTaskSem,0,&err);

    btn = buttons_pressed();

    if (cruise_control_active == on)
    { // The ControlTask decides if the conditions are right
      // to activate Cruise Control and it tells us via the
      // cruise_control_active flag
      GreenLeds = GreenLeds | LED_GREEN_0;
    }

    if (btn & CRUISE_CONTROL_FLAG)
    {
      CruiseControl = 1;
      cruise_control = on;
      GreenLeds = GreenLeds | LED_GREEN_2;

    } else
    {
      cruise_control = off;
      CruiseControl = 0;
    }

    if (btn & GAS_PEDAL_FLAG)
    {
      GasPedal = 1;
      gas_pedal = on;
      GreenLeds = GreenLeds | LED_GREEN_6;
    } else
    {
      GasPedal = 0;
      gas_pedal = off;
    }

    if (btn & BRAKE_PEDAL_FLAG)
    {
      BrakePedal = 1;
      brake_pedal = on;
      GreenLeds = GreenLeds | LED_GREEN_4;
    } else
    {
      brake_pedal = off;
      BrakePedal = 0;
    }

    IOWR_ALTERA_AVALON_PIO_DATA(DE2_PIO_GREENLED9_BASE,GreenLeds);
  }
}

void ExtraLoadTask(void* pdata)
{
  INT8U err;
  INT32U Util_MS;
  while(1)
  {
    OSSemPend(ExtraLoadTaskSem,0,&err);
    Util_MS = (INT32U)Utilization*2*0.01*300;
    if (Util_MS>300)
    {
      Util_MS = 300;
    }
    printf("ExtraLoadTask: Utilization %d (%d%%) Delay: %d\n",(alt_u32)Utilization,2*(alt_u32)Utilization,(alt_u32)Util_MS);
    delay_asm((alt_u32)Util_MS);
  }
}

void OverloadDetectionTask(void* pdata)
{
  INT8U err;
  while(1)
  {
    OSSemPend(OverloadDetectionTaskSem,0,&err);
    OverloadDetectionFlag = 1;
  }
}

void WatchDogTask(void* pdata)
{
  INT8U err;
  INT32U Last_OK_Time,OverloadDetected;
  while(1)
  {
    OSSemPend(WatchDogTaskSem,0,&err);
    if (OverloadDetectionFlag)
    {

      Last_OK_Time = (INT32U)OSTimeGet();
      OverloadDetected = 0;
      OverloadDetectionFlag = 0;
    } else
    { // 1 second of no activity from OverloadDetectionTask before
      // we trigger an overload alarm
      if ((INT32U)OSTimeGet() - Last_OK_Time > 1000)
      {

        OverloadDetected = 1;
      } else
      {
        OverloadDetected = 0;
      }
    }
    if (OverloadDetected)
    {
      printf("Watchdog Task: Overload detected. No signal received from OverloadDetectionTask in 1 second or more.\n");
    }
  }
}



/*
 * The task 'StartTask' creates all other tasks kernel objects and
 * deletes itself afterwards.
 */


void StartTask(void* pdata)
{
  INT8U err;
  void* context;

  static alt_alarm alarm;     /* Is needed for timer ISR function */

  /* Base resolution for SW timer : HW_TIMER_PERIOD ms */
  delay = alt_ticks_per_second() * HW_TIMER_PERIOD / 1000;
  printf("delay in ticks %d\n", delay);

  /*
   * Create Hardware Timer with a period of 'delay'
   */
  if (alt_alarm_start (&alarm,
               delay,
               alarm_handler,
               context) < 0)
    {
      printf("No system clock available!n");
    }

  /*
   * Create and start Software Timer
   */

   VehicleTaskTmr = OSTmrCreate(0, //delay
                                VEHICLE_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                VehicleTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "VehicleTaskTmr",
                               &err);

   ControlTaskTmr = OSTmrCreate(0, //delay
                                CONTROL_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                ControlTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "ControlTaskTmr",
                               &err);


   SwitchIOTaskTmr = OSTmrCreate(0, //delay
                                CONTROL_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                SwitchIOTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "SwitchIOTaskTmr",
                               &err);

   ButtonIOTaskTmr = OSTmrCreate(0, //delay
                                CONTROL_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                ButtonIOTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "ButtonIOTaskTmr",
                               &err);




   ExtraLoadTaskTmr = OSTmrCreate(0, //delay
                                CONTROL_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                ExtraLoadTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "ExtraLoadTaskTmr",
                               &err);


   OverloadDetectionTaskTmr = OSTmrCreate(0, //delay
                                CONTROL_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                OverloadDetectionTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "OverloadDetectionTaskTmr",
                               &err);


   WatchDogTaskTmr = OSTmrCreate(0, //delay
                                CONTROL_PERIOD/HW_TIMER_PERIOD, //period
                                OS_TMR_OPT_PERIODIC,
                                WatchDogTaskTmrCallback, //OS_TMR_CALLBACK
                                (void *)0,
                               "WatchDogTaskTmr",
                               &err);



  OSTmrStart(VehicleTaskTmr, &err);
  OSTmrStart(ControlTaskTmr, &err);

  OSTmrStart(SwitchIOTaskTmr, &err);
  OSTmrStart(ButtonIOTaskTmr, &err);


  OSTmrStart(ExtraLoadTaskTmr, &err);
  OSTmrStart(OverloadDetectionTaskTmr, &err);
  OSTmrStart(WatchDogTaskTmr, &err);




  /*
   * Creation of Kernel Objects
   */

  VehicleTaskSem  = OSSemCreate(1);
  ControlTaskSem  = OSSemCreate(1);
  ButtonIOTaskSem = OSSemCreate(1);
  SwitchIOTaskSem = OSSemCreate(1);


  ExtraLoadTaskSem = OSSemCreate(1);
  OverloadDetectionTaskSem = OSSemCreate(1);
  WatchDogTaskSem = OSSemCreate(1);




  // Mailboxes
  Mbox_Throttle = OSMboxCreate((void*) 0); /* Empty Mailbox - Throttle */
  Mbox_Velocity = OSMboxCreate((void*) 0); /* Empty Mailbox - Velocity */

  Mbox_CruiseControl = OSMboxCreate((void*) 0);
  Mbox_GasPedal = OSMboxCreate((void*) 0);

  Mbox_Engine = OSMboxCreate((void*) 0);
  Mbox_Gear = OSMboxCreate((void*) 0);

  Mbox_BrakePedal = OSMboxCreate((void*) 0);

  /*
   * Create statistics task
   */

  OSStatInit();

  /*
   * Creating Tasks in the system
   */


  err = OSTaskCreateExt(
            ControlTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &ControlTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            CONTROLTASK_PRIO,
            CONTROLTASK_PRIO,
            (void *)&ControlTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);

  err = OSTaskCreateExt(
            VehicleTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &VehicleTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            VEHICLETASK_PRIO,
            VEHICLETASK_PRIO,
            (void *)&VehicleTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);


  err = OSTaskCreateExt(
            ButtonIOTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &ButtonIOTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            ButtonIOTASK_PRIO,
            ButtonIOTASK_PRIO,
            (void *)&ButtonIOTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);


  err = OSTaskCreateExt(
            SwitchIOTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &SwitchIOTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            SwitchIOTASK_PRIO,
            SwitchIOTASK_PRIO,
            (void *)&SwitchIOTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);


  err = OSTaskCreateExt(
            ExtraLoadTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &ExtraLoadTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            ExtraLoadTASK_PRIO,
            ExtraLoadTASK_PRIO,
            (void *)&ExtraLoadTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);


  err = OSTaskCreateExt(
            OverloadDetectionTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &OverloadDetectionTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            OverloadDetectionTASK_PRIO,
            OverloadDetectionTASK_PRIO,
            (void *)&OverloadDetectionTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);


  err = OSTaskCreateExt(
            WatchDogTask, // Pointer to task code
            NULL,        // Pointer to argument that is
                    // passed to task
            &WatchDogTask_Stack[TASK_STACKSIZE-1], // Pointer to top
            // of task stack
            WatchDogTASK_PRIO,
            WatchDogTASK_PRIO,
            (void *)&WatchDogTask_Stack[0],
            TASK_STACKSIZE,
            (void *) 0,
            OS_TASK_OPT_STK_CHK);

  printf("All Tasks and Kernel Objects generated!\n");

  /* Task deletes itself */

  OSTaskDel(OS_PRIO_SELF);
}

/*
 *
 * The function 'main' creates only a single task 'StartTask' and starts
 * the OS. All other tasks are started from the task 'StartTask'.
 *
 */

int main(void) {

  printf("Lab: Cruise Control\n");

  OSTaskCreateExt(
          StartTask, // Pointer to task code
          NULL,      // Pointer to argument that is
          // passed to task
          (void *)&StartTask_Stack[TASK_STACKSIZE-1], // Pointer to top
          // of task stack
          STARTTASK_PRIO,
          STARTTASK_PRIO,
          (void *)&StartTask_Stack[0],
          TASK_STACKSIZE,
          (void *) 0,
          OS_TASK_OPT_STK_CHK | OS_TASK_OPT_STK_CLR);



  OSStart();

  return 0;
}




