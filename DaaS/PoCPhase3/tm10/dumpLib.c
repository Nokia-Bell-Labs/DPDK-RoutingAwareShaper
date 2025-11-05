/* dumpLib.c
**
** This file is intended for debugging purposes.  It provides functions to
** dump to screen individual configuration and state data structures.
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include <stdio.h>
#include "dumpLib.h"
#include <rte_ether.h>
#include <rte_byteorder.h>

#define BOOL_STR_XXXABLED(_bool) ((_bool==true)?"enabled":"disabled")

void dumpStreamConf(StreamCfg *sc)
{
  printf("Stream#%d: ", sc->streamId);
  printf("\n\tsrcIP:     %hhu.%hhu.%hhu.%hhu", sc->srcIP[0],sc->srcIP[1],sc->srcIP[2],sc->srcIP[3]);
  printf("\n\tdstIP:     %hhu.%hhu.%hhu.%hhu", sc->dstIP[0],sc->dstIP[1],sc->dstIP[2],sc->dstIP[3]);
  printf("\n\trate:      %.3f", sc->rate);
  printf("\n\tlatency:   %.3f", sc->latency);
  printf("\n\tpktsize:   %u", sc->pktsize);
  printf("\n\tvlanId:    0x%2x", sc->vlanId);
  printf("\n\tvlanPri:   %u", sc->vlanPri);
  printf("\n\tttl:       %u", sc->ttl);
  printf("\n\tprotocol:  %s", ( (sc->protocol == IPPROTO_UDP) ? "udp" : "tcp"));
  printf("\n\tavg_on:    %.2f", sc->avg_on_time);
  printf("\n\tavg_off:   %.2f", sc->avg_off_time);
  printf("\n\tdominance: ");
  switch (sc->dominance) {
    case STREAM_TYPE_UNKNOWN:
      printf("Unknown");
      break;
    case STREAM_TYPE_BW_DOMINATE:
      printf("BW");
      break;
    case STREAM_TYPE_LAT_DOMINIATE:
      printf("Lat");
      break;
    case STREAM_TYPE_OTHER:
      printf("Other");
      break;
    default:
      printf("Error");
  }
  printf("\n");
}

void dumpRunConf(RunConf *rc)
{
  printf("******************\n");
  printf("Run Configuration:\n");
  printf("******************\n");
  printf("RunConf:\n");
  printf("initMask            %i\n", rc->initMask);
  printf("promiscuous         %s\n", (rc->promiscuous == 0 ? "false" : "true"));
  printf("linkSpeedMbpsActual %u\n", rc->linkSpeedMbpsActual);
  printf("linkSpeedMbpsConf   %u\n", rc->linkSpeedMbpsConf);
  printf("maxRunPkts          %u\n", rc->maxRunPkts);
  printf("maxRunTimeslots     %u\n", rc->maxRunTimeslots);
  printf("txqNum              %u\n", rc->txqNum);
  printf("txqId               %u\n", rc->txqId);
  printf("statsTimerSec       %u\n", rc->statsTimerSec);
  printf("******************\n\n");
}

void dumpIntfConf(IntfConf *ic)
{
  printf("***********************\n");
  printf("Interface Configuration\n");
  printf("***********************\n");
  printf("IntfConf:\n");
  printf("schedPath srcAddr   %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
          ic->schedPath.srcAddr.addr_bytes[0],
          ic->schedPath.srcAddr.addr_bytes[1],
          ic->schedPath.srcAddr.addr_bytes[2],
          ic->schedPath.srcAddr.addr_bytes[3],
          ic->schedPath.srcAddr.addr_bytes[4],
          ic->schedPath.srcAddr.addr_bytes[5]);
  printf("schedPath dstAddr   %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
          ic->schedPath.dstAddr.addr_bytes[0],
          ic->schedPath.dstAddr.addr_bytes[1],
          ic->schedPath.dstAddr.addr_bytes[2],
          ic->schedPath.dstAddr.addr_bytes[3],
          ic->schedPath.dstAddr.addr_bytes[4],
          ic->schedPath.dstAddr.addr_bytes[5]);
  printf("l2fwdPath srcAddr   %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
          ic->l2fwdPath.srcAddr.addr_bytes[0],
          ic->l2fwdPath.srcAddr.addr_bytes[1],
          ic->l2fwdPath.srcAddr.addr_bytes[2],
          ic->l2fwdPath.srcAddr.addr_bytes[3],
          ic->l2fwdPath.srcAddr.addr_bytes[4],
          ic->l2fwdPath.srcAddr.addr_bytes[5]);
  printf("l2fwdPath dstAddr   %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
          ic->l2fwdPath.dstAddr.addr_bytes[0],
          ic->l2fwdPath.dstAddr.addr_bytes[1],
          ic->l2fwdPath.dstAddr.addr_bytes[2],
          ic->l2fwdPath.dstAddr.addr_bytes[3],
          ic->l2fwdPath.dstAddr.addr_bytes[4],
          ic->l2fwdPath.dstAddr.addr_bytes[5]);
  printf("vlanTag             %s\n", (ic->vlanTag == 0 ? "false" : "true"));
  printf("vlanPri             %u\n", ic->vlanPri);
  printf("vlanId              %u\n", ic->vlanId);
  uint32_t ip = rte_bswap32(ic->srcIP);
  printf("srcIP               %" PRIu8 ":%" PRIu8 ":%" PRIu8 ":%" PRIu8 "\n", 
         (ip >> 24), ((ip >> 16) & 0xff), ((ip >> 8) & 0xff), (ip & 0xff));
  ip = rte_bswap32(ic->dstIP);
  printf("dstIP               %" PRIu8 ":%" PRIu8 ":%" PRIu8 ":%" PRIu8 "\n", 
         (ip >> 24), ((ip >> 16) & 0xff), ((ip >> 8) & 0xff), (ip & 0xff));
  printf(" ipVer:           %d\n", ic->ipVer);
  printf(" dscp:            0x%02x\n", ic->dscp);
  printf(" ecn:             %d\n", ic->ecn);
  printf(" dstPort:         0x%4x\n", ic->dstPort);

  printf(" hwChksumOffload: %s\n", BOOL_STR_XXXABLED(ic->hwChksumOffload));
  printf(" updateSeqNo:     %s\n", BOOL_STR_XXXABLED(ic->updateSeqNo));
  printf("***********************\n\n");
}

void dumpPss(SchedConf *sc)
{
  printf("*****************\n");
  printf("PSS Configuration\n");
  printf("*****************\n");
  for (int i = 0; i < sc->timeslotsPerSeq; i++)
  {
    printf("%4hu ", *(sc->pss[i]));
    if ( (i+1) % 20 == 0 )
    {
      printf("\n");
    }
  }
  printf("*****************\n\n");
}

void dumpSchedConf(SchedConf *sc)
{
  printf("***********************\n");
  printf("Scheduler Configuration- CONFIG 0 ONLY!\n");
  printf("***********************\n");
  printf("schedId             %u\n", sc->schedId);
  printf("schedMode           %u\n", sc->schedMode);
  printf("txPort              %u\n", sc->txPort);
  printf("tmCore              %u\n", sc->tmCore);
  printf("txCore              %u\n", sc->txCore);
  printf("rxCore              %u\n", sc->rxCore);
  printf("linkSpeedMbps       %u\n", sc->linkSpeedMbps);
  printf("timeslotsPerSeq     %u\n", sc->timeslotsPerSeq);
  printf("maxPktSize          %u\n", sc->maxPktSize);
  printf("timeslotNsec        %u\n", sc->timeslotNsec);
  printf("timeslotTsc         %u\n", sc->timeslotTsc);
  printf("tscHz               %lu\n", sc->tscHz);
  printf("tscHzMeasured       %lf\n", sc->tscHzMeasured);
  printf("linkSpeedBpMTsc     %lu\n", sc->linkSpeedBpMTsc);
  printf("queuesNum           %u\n", sc->queuesNum);
  //pss[NUM_TIMESLOTS_MAX]  *** printed separately due to size ***
  printf("***********************\n");
  printf("schedCfgFile        %s\n", sc->schedCfgFile);
  printf("intfCfgFile         %s\n", sc->intfCfgFile);
  printf("***********************\n");
  printf("numStreams:         %d\n", sc->numStreams);
  printf("streamsBaseNum:     %u\n", sc->streamsBaseNum);
  printf("***********************\n");
  for (int i=0; i<sc->numStreams; i++)
    dumpStreamConf(&sc->streamCfg[0][i]);  // Update stream cfg
  printf("***********************\n");
  for (int i = 0; i < sc->queuesNum; i++)
  {
    printf("bundle %-4u         numQueues %-4u numTimeslots %-7u schedRate %-6u queue(s): ", 
           sc->bundleConf[0][i].bid,
           sc->bundleConf[0][i].numQueues,
           sc->bundleConf[0][i].numTimeslots,
           sc->bundleConf[0][i].schedRate);
    for (int j = 0; j < sc->bundleConf[0][i].numQueues; j++)
    {
      printf("%u ", sc->bundleConf[0][i].queues[j]);
    }
    printf("\n");
  }
  printf("***********************\n\n");
}

