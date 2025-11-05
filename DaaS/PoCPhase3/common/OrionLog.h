/* OrionLog.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _ORION_LOG_H_
#define _ORION_LOG_H_

#ifdef ORION_APP_TM
#define  PKTCNTR  (schedState[0].txPktsTotal)
#endif
#ifdef ORION_APP_TA
#define  PKTCNTR  (taInfo.rxPkts)
#endif
#ifdef ORION_APP_TG
#define  PKTCNTR  (tgInfo.rxPkts)
#endif

#define DBGLOG(fmt...) \
  do { \
    if (debugLog) \
    { \
      printf("DBG:%u Pkt%u ", __LINE__, PKTCNTR); \
      printf(fmt); \
    } \
  } while(0);
#define ERRLOG(fmt...) \
  do { \
    if (errorLog) \
    { \
      printf("ERROR:%u Pkt%u ", __LINE__, PKTCNTR); \
      printf(fmt); \
    } \
  } while(0);
#define INFOLOG(fmt...) \
  do { \
    if (infoLog) \
    { \
      printf("INFO:%u Pkt%u ", __LINE__, PKTCNTR); \
      printf(fmt); \
    } \
  } while(0);
#define ERRASSERT(x, fmt...) \
  do { \
    if (errorLog && !(x)) \
    { \
      printf("ERROR:%u Pkt%u %s", __LINE__, PKTCNTR, #x); \
      printf(fmt); \
    } \
  } while(0);

extern int debugLog, errorLog, infoLog;

#endif  // End of _ORION_LOG_H_
