pragma Task_Dispatching_Policy(FIFO_Within_Priorities);

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Float_Text_IO;

with Ada.Real_Time; use Ada.Real_Time;

procedure PeriodicTasks_Priority is
   package Duration_IO is new Ada.Text_IO.Fixed_IO(Duration);
   package Int_IO is new Ada.Text_IO.Integer_IO(Integer);
	
   Start : Time; -- Start Time of the System
	Calibrator: constant Integer := 1000; -- Calibration for correct timing
	                                     -- ==> Change parameter for your architecture!
	Warm_Up_Time: constant Integer := 100; -- Warmup time in milliseconds
	
	-- Conversion Function: Time_Span to Float
	function To_Float(TS : Time_Span) return Float is
        SC : Seconds_Count;
        Frac : Time_Span;
   begin
		Split(Time_Of(0, TS), SC, Frac);
		return Float(SC) + Time_Unit * Float(Frac/Time_Span_Unit);
   end To_Float;
	
	-- Function F is a dummy function that is used to model a running user program.
	function F(N : Integer) return Integer;

   function F(N : Integer) return Integer is
      X : Integer := 0;
   begin
      for Index in 1..N loop
         for I in 1..500 loop
            X := X + I;
         end loop;
      end loop;
      return X;
   end F;
   -- watchdog timer task
	task Watchdog is
		entry reset;
	end Watchdog;

	task body Watchdog is
		Release: Time;
		Deadline: Time;
		Hyperperiod : Integer:=12000; --12ms;
		Phase : Integer:=100; -- same as warm up time
	begin
		Release := Clock + Milliseconds(Phase); --make sure watchdog starts at the same time
		Deadline := Release + Milliseconds(Hyperperiod); -- watchdog starts every hyperperiod
		delay until Release;
		Put_Line("Watchdog Timer starts");
		Put("next deadline: ");
		Duration_IO.Put(To_Duration(Deadline - Start), 2, 3);
		Put_Line("");
		loop
			select
				accept reset do					
					delay until Deadline;
					Deadline := Deadline + Milliseconds(Hyperperiod); --set watchdog deadline to next end of hyperperiod
					Put_Line("----watchdog reset----");
					Put("next deadline: ");
					Duration_IO.Put(To_Duration(Deadline - Start), 2, 3);
					Put_Line("");
				end reset;
			or
				delay until Deadline;
				Put_Line("----------------------------------");
				Put_Line("Warning: the system is overloaded");
				Put_Line("----------------------------------");
				Deadline := Deadline + Milliseconds(Hyperperiod);
			end select;
		end loop;
	end Watchdog;
	-- Overload detection task

	task type OD(Prio: Integer; Phase: Integer) is
		pragma Priority(Prio);
	end OD;

	task body OD is 
	--overload means U>100%
	--very fast task with lowest prioity. 
	--it can run in a hyperperiod => the system still have spare resource
		Release: Time;
	begin
		Release := Clock + Milliseconds(Phase);
		delay until Release;
		loop
		Watchdog.reset;
		end loop;
	end  OD;

	
	-- Workload Model for a Parametric Task
   task type T(Id: Integer; Prio: Integer; Phase: Integer; Period : Integer; 
									 Computation_Time : Integer; Relative_Deadline: Integer) is
      pragma Priority(Prio); -- A higher number gives a higher priority
   end;

   task body T is
      Next : Time;
		Release: Time;
		Completed : Time;
		Response : Time_Span;
		Average_Response : Float;
		Absolute_Deadline: Time;
		WCRT: Time_Span; -- measured WCRT (Worst Case Response Time)
      Dummy : Integer;
		Iterations : Integer;
   begin
		-- Initial Release - Phase
		Release := Clock + Milliseconds(Phase);
		delay until Release;
		Next := Release;
		Iterations := 0;
		Average_Response := 0.0;
		WCRT := Milliseconds(0);
      loop
         Next := Release + Milliseconds(Period);
			Absolute_Deadline := Release + Milliseconds(Relative_Deadline);
         -- Simulation of User Function
			for I in 1..Computation_Time loop
				Dummy := F(Calibrator); 
			end loop;	
			Completed := Clock;
			Response := Completed - Release;
			Average_Response := (Float(Iterations) * Average_Response + To_Float(Response)) / Float(Iterations + 1);
			if Response > WCRT then
				WCRT := Response;
			end if;
			Iterations := Iterations + 1;			
			Put("Task ");
			Int_IO.Put(Id, 1);
			Put("- Release: ");
			Duration_IO.Put(To_Duration(Release - Start), 2, 3);
			Put(", Completion: ");
			Duration_IO.Put(To_Duration(Completed - Start), 2, 3);
			Put(", Response: ");
			Duration_IO.Put(To_Duration(Response), 1, 3);
			Put(", WCRT: ");
			Ada.Float_Text_IO.Put(To_Float(WCRT), fore => 1, aft => 3, exp => 0);	
			Put(", Next Release: ");
			Duration_IO.Put(To_Duration(Next - Start), 2, 3);
			if Completed > Absolute_Deadline then 
				Put(" ==> Task ");
				Int_IO.Put(Id, 1);
				Put(" violates Deadline!");
			end if;
         Put_Line("");
			Release := Next;
         delay until Release;
      end loop;
   end T;

   -- Running Tasks
	-- NOTE: All tasks should have a minimum phase, so that they have the same time base!
	
    Task_1 : T(1, 20, Warm_Up_Time, 3000, 1000, 3000); -- ID: 1
	                                                   -- Priority: 20
                                                       -- Phase: Warm_Up_Time (100)
	                                                   -- Period 2000, 
	                                                   -- Computation Time: 1000 (if correctly calibrated) 
	                                                   -- Relative Deadline: 2000
    Task_2 : T(2, 19, Warm_Up_Time, 4000, 1000, 4000);
    Task_3 : T(3, 18, Warm_Up_Time, 6000, 1000, 6000);
    --Task_4 : T(4, 17, Warm_Up_Time, 12000, 2000, 12000);
	--Task_Watchdog : Watchdog;                  --rendezvous don't need indication
	Task_overloaddetection : OD(1,Warm_Up_Time); -- lowest prioity
	
-- Main Program: Terminates after measuring start time	
begin
   Start := Clock; -- Central Start Time
   null;
end PeriodicTasks_Priority;
