/* dumpLib.h
**
** This file is intended for debugging purposes. It provides functions to
** dump to screen individual configuration and state data structures.
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef DUMP_LIB_H_
#define DUMP_LIB_H_

#include "tmDefs.h"

void dumpRunConf(RunConf *rc);
void dumpIntfConf(IntfConf *ic);
void dumpPss(SchedConf *sc);
void dumpSchedConf(SchedConf *sc);
void dumpStreamConf(StreamCfg *sc);

#endif // DUMP_LIB_H_
