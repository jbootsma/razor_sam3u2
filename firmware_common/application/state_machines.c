#include "configuration.h"

#define SAFE_SM_CALL(psMach, eEvt)                                             \
  do {                                                                         \
    if (psMach->pfnCurrState != NULL) {                                        \
      psMach->pfnCurrState(eEvt);                                              \
    }                                                                          \
  } while (0)

void InitStateMachine(StateMachineType *psMach, StateMachineFn pfnInitState) {
  psMach->pfnCurrState = NULL;
  psMach->pfnNextState = pfnInitState;
  psMach->u32Timer = 0;
}

void RunStateMachine(StateMachineType *psMach) {
  SAFE_SM_CALL(psMach, SM_EVT_TICK);

  if (psMach->u32Timer > 0) {
    if (--psMach->u32Timer == 0 && psMach->pfnCurrState == psMach->pfnNextState) {
      SAFE_SM_CALL(psMach, SM_EVT_TIMEOUT);
    }
  }

  if (psMach->pfnNextState != psMach->pfnCurrState) {
    SAFE_SM_CALL(psMach, SM_EVT_EXIT);
    psMach->pfnCurrState = psMach->pfnNextState;
    psMach->u32Timer = 0;
    SAFE_SM_CALL(psMach, SM_EVT_ENTER);
  }
}

void ChangeState(StateMachineType *psMach, StateMachineFn pfnNextState) {
  psMach->pfnNextState = pfnNextState;
}

void SetTimeout(StateMachineType *psMach, u32 u32Timeout) 
{
  psMach->u32Timer = u32Timeout;
}
