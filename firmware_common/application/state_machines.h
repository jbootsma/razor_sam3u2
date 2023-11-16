#ifndef STATE_MACHINES_H
#define STATE_MACHINES_H

typedef enum {
  SM_EVT_ENTER,
  SM_EVT_EXIT,
  SM_EVT_TICK,
  SM_EVT_TIMEOUT,
} StateMachineEventType;

typedef void (*StateMachineFn)(StateMachineEventType eEvt);

typedef struct {
  StateMachineFn pfnCurrState;
  StateMachineFn pfnNextState;
  u32 u32Timer;
} StateMachineType;

void InitStateMachine(StateMachineType *psMach, StateMachineFn pfnInitState);
void RunStateMachine(StateMachineType *psMach);
void ChangeState(StateMachineType *psMach, StateMachineFn pfnNextState);
void SetTimeout(StateMachineType *psMach, u32 u32Timeout);

#endif