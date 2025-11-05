/* tmMain.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "tmFlow.h"
#include "parserLib.h"

#include "../common/OrionDpdk.h"

// debug
#include "dumpLib.h"

// Globals
volatile bool forceQuit;
uint32_t enabledPortsMask = 0;    // mask of enabled ports
RunConf runConf;
IntfConf intfConf[NUM_SCHED_MAX];
SchedConf schedConf[NUM_SCHED_MAX];
SchedState schedState[NUM_SCHED_MAX];

static int
app_launch_one_lcore(__attribute__((unused)) void *dummy)
{
  SchedThreadsDispatcher();
  return 0;
}

static void
signal_handler(int signum)
{
  if (signum == SIGINT || signum == SIGTERM)
  {
    printf("\n\nSignal %d received, preparing to exit...\n", signum);
    forceQuit = true;
  }
}

int
StreamRatesValidate(SchedConf *sc, uint8_t confId)
{
        // validate total stream rates and set other stream 0 values as well
        sc->streamCfg[confId][0].streamId = 0;
        sc->streamCfg[confId][0].srcIP[0] = sc->streamCfg[confId][1].srcIP[0];
        sc->streamCfg[confId][0].srcIP[1] = sc->streamCfg[confId][1].srcIP[1];
        sc->streamCfg[confId][0].srcIP[2] = sc->streamCfg[confId][1].srcIP[2];
        sc->streamCfg[confId][0].srcIP[3] = sc->streamCfg[confId][1].srcIP[3];
        sc->streamCfg[confId][0].dstIP[0] = sc->streamCfg[confId][1].dstIP[0];
        sc->streamCfg[confId][0].dstIP[1] = sc->streamCfg[confId][1].dstIP[1];
        sc->streamCfg[confId][0].dstIP[2] = sc->streamCfg[confId][1].dstIP[2];
        sc->streamCfg[confId][0].dstIP[3] = sc->streamCfg[confId][1].dstIP[3];

	sc->numStreams++;

        float total=0;
        for (int i=0; i<sc->numStreams; i++)
                total += sc->streamCfg[confId][i].rate;
        if (total > (float)(sc->linkSpeedMbps))
                rte_panic(" Error: total rate %.3f Mb/s of all streams exceeds link capacity %u Mbps\n", total, sc->linkSpeedMbps);
        if (sc->streamCfg[confId][0].rate == 0)
        {
                sc->streamCfg[confId][0].rate =  sc->linkSpeedMbps - total;
                printf("INFO: stream 0 get leftover bandwidth of %.3f\n", sc->streamCfg[confId][0].rate);
        }
        sc->streamCfg[confId][0].pktsize =  PKT_SIZE_DEFAULT;
        sc->streamCfg[confId][0].vlanId =  0x100;
        //sc->streamCfg[0].vlanPri =  0;
        sc->streamCfg[confId][0].vlanPri =  7;
//        sc->streamCfg[0].ttl =  32;// AF221201: Removing old behavior
        sc->streamCfg[confId][0].ttl =  128;
        sc->streamCfg[confId][0].protocol =  IPPROTO_UDP;
        sc->streamCfg[confId][0].avg_on_time =  1.0;
        sc->streamCfg[confId][0].avg_off_time =  0.0;
        return 0;
}

#define NUMA_ERRASSERT(x) \
  do { \
    if (!(x)) \
    { \
      err = true; \
      printf("ERROR:%u NUMA check failed for %s\n", __LINE__, #x); \
    } \
  } while(0);

static uint32_t
TmCpuSocketCheck(SchedConf *sc)
{
  bool err=false;
  uint32_t rxCoreSocket = rte_lcore_to_socket_id(sc->rxCore);
  uint32_t tmCoreSocket = rte_lcore_to_socket_id(sc->tmCore);
  uint32_t txCoreSocket = rte_lcore_to_socket_id(sc->txCore);
  uint32_t rxPortSocket = rte_eth_dev_socket_id(sc->rxPort);
  if(rxPortSocket == 0xFFFFFFFF)
        {
                printf("Ignore negative rxPortSocket - Error is: %s Error code: %d\n", rte_strerror(rte_errno), rte_errno);
                rxPortSocket = rxCoreSocket;
        }

  uint32_t txPortSocket = rte_eth_dev_socket_id(sc->txPort);
  if(txPortSocket == 0xFFFFFFFF)
        {
                printf("Ignore negative txPortSocket - Error is: %s Error code: %d\n", rte_strerror(rte_errno), rte_errno);
                txPortSocket = txCoreSocket;
        }
  
  printf("INFO: NUMA Map: tmCore%u on socket%u, txCore%u on socket%u\n",
               sc->tmCore, tmCoreSocket, sc->txCore, txCoreSocket);
  printf("INFO: NUMA Map: txPort%u on socket%u\n", sc->txPort, txPortSocket);
  NUMA_ERRASSERT((rxCoreSocket==tmCoreSocket));
  NUMA_ERRASSERT((rxCoreSocket==txCoreSocket));
  NUMA_ERRASSERT((rxPortSocket==txPortSocket));
  NUMA_ERRASSERT((rxPortSocket==rxCoreSocket));
  NUMA_ERRASSERT((tmCoreSocket==txCoreSocket));
  NUMA_ERRASSERT((txPortSocket==txCoreSocket));

  if (err)
    rte_exit(EXIT_FAILURE, "Exit due to NUMA validation exception!\n");

  return txCoreSocket;  // pick this as expected to have rx/tx lcores and ports all on same cpu socket!!
}

static void
TmAppPreinit(void)
{
  memset(&runConf,    0, sizeof(runConf));
  memset(&intfConf,   0, sizeof(intfConf));
  memset(&schedConf,  0, sizeof(schedConf));
  memset(&schedState, 0, sizeof(schedState));

  // parse_arg() and config files may subsequently modify default settings!

  runConf.statsTimerSec = STATS_TIMER_PERIOD_DEFAULT;
  runConf.txqNum = 1;
  runConf.txqId  = 0;
  //  runConf.rxqNum = 3;
  runConf.rxqNum = 1;
  runConf.rxFlows = 0;
  runConf.promiscuous=true;  // true for DPDK to receive all traffic. Disable if unmatched dstMac unicast traffic also handled
}

static int
TmAppInit(unsigned portsMask)
{
  struct rte_eth_link link;
  RunConf   *rc = &runConf;
  SchedConf *sc = &schedConf[0];  // Only 1 instanced assumed!

  uint32_t cpuSocket = TmCpuSocketCheck(sc);

  ethdev_init(cpuSocket, portsMask, rc);

  sc->tscHzMeasured = tscClockCalibrate(true);  // Another method of TSC calibaration for comparison

  sc->tscHz = rte_get_tsc_hz();
  sc->timeslotNsec = (uint32_t) sc->maxPktSize * 8 * 1000 / (uint32_t) rc->linkSpeedMbpsConf;
  sc->timeslotTsc = ((uint64_t) sc->timeslotNsec * sc->tscHz) / NSEC_PER_SEC;  // not using get_tsc_cycles_per_ns() to avoid truncation inaccuracy
  printf("INFO: timeslot duration %u nsec or %u\n", sc->timeslotNsec, sc->timeslotTsc);

  sc->rxBurstSize = TM_RX_PKT_BURST_MAX;  // FUTURE: add cmdline to support optional rx/tx burst input. See DPDK_TM --bsz option.
  printf("INFO: rxBurstSize %u\n", sc->rxBurstSize);
  rte_eth_link_get(sc->txPort, &link);
  sc->linkSpeedMbps = link.link_speed;    // Both in mbps
  rc->linkSpeedMbpsActual = link.link_speed;

  if (rc->linkSpeedMbpsConf != 0)
  {
    if (rc->linkSpeedMbpsConf > rc->linkSpeedMbpsActual)
      printf("WARNING: configured speed %u mbps > actual link speed %u mbps\n", rc->linkSpeedMbpsConf, rc->linkSpeedMbpsActual);
    sc->linkSpeedMbps = rc->linkSpeedMbpsConf;
    printf("INFO: Overrided link speed to %u mbps, actual link speed %u mbps\n", rc->linkSpeedMbpsConf, rc->linkSpeedMbpsActual);
  }
  else
  {
    if (sc->linkSpeedMbps < 1000)
    {
      printf("ERROR: linkSpeedMbps detected as %u\n", sc->linkSpeedMbps);
      rte_exit(EXIT_FAILURE, "TmAppInit() failed due to bad link speed, try use optionally set speed as work-around!\n");
    }
  }

  sc->linkSpeedBpMTsc = ((uint64_t) sc->linkSpeedMbps * 1E6 * 1E6) / sc->tscHz;

  for (unsigned f=0; rc->rxFlows > f; f++)
  {
    struct rte_flow *flow;
    RxFlow *fc=&rc->rxFlow[f];

    printf("rxFlow%u: port%u fid=%u vlanTci=0x%x (pri%u,id=0x%03x;mask=0x%04x) -> queue%u\n",
            f,  sc->rxPort, fc->flowId, fc->vlanTci, (unsigned)(fc->vlanTci >> 13), (unsigned)(fc->vlanTci & 0xfff),
            (unsigned)fc->vlanTciMask, (unsigned)fc->queueId);

    flow = TmRxFlowConfig(sc->rxPort, fc->queueId, fc->vlanTci, fc->vlanTciMask);
    if (!flow)
      rte_exit(EXIT_FAILURE, "Port%u Flow%u initialization failed!\n", sc->rxPort, f);
  }

  printf("INFO: dequeue thread found link speed in %u mbps for port %u\n", sc->linkSpeedMbps, sc->txPort);

  int ret = StreamPktInit(0,0);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Error TmStreamsInit\n");

  return 0;
}

int
main(int argc, char **argv)
{
  unsigned lcore_id;
  int ret;

  forceQuit = false;
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  /* init EAL */
  ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
  argc -= ret;
  argv += ret;

  TmAppPreinit();

  parse_args(argc, argv);

  ethdev_wait_all_ports_up(enabledPortsMask, 5);

  ret = TmAppInit(enabledPortsMask);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "TmAppInit(0 failed!\n");

  StreamRatesValidate(&schedConf[0], 0);

  // debug
  dumpRunConf(&runConf);
  dumpIntfConf(&intfConf[0]);
  dumpSchedConf(&schedConf[0]);
  //dumpPss(&schedConf[0]);


  /* launch per-lcore init on every lcore */
  rte_eal_mp_remote_launch(app_launch_one_lcore, NULL, CALL_MAIN);
  RTE_LCORE_FOREACH_WORKER(lcore_id) {
    if (rte_eal_wait_lcore(lcore_id) < 0) {
      ret = -1;
      break;
    }
  }

  printf("Ports cleanup...\n");

  uint16_t portid;
  RTE_ETH_FOREACH_DEV(portid) {
    if ((enabledPortsMask & (1 << portid)) == 0)
      continue;
    printf("Closing port %d...", portid);
    rte_eth_dev_stop(portid);
    rte_eth_dev_close(portid);
    printf(" Done\n");
  }

  printf("Bye...\n");

  return ret;
}
