/* parserCfgSched.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "parserLib.h"
#include <stdint.h>

typedef int SCF_ROW_FUNCTION;
typedef int (*SCF_ROW_FNPTR)(SchedConf *sc, int row, char *str, uint8_t confId);

static int
app_parse_scf_mac_addr_str(uint8_t *mac, char *str)
{
	return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
}

static SCF_ROW_FUNCTION
app_parse_scf_row_CONFIG_DESCRIPTION(SchedConf *dummy, int rowId, char *cd_str, uint8_t confId)
{
  // Informational
  if (dummy && rowId && confId) {}  // avoid compiler warning
  printf("\t%s\n", cd_str);
  return 0;
}

static SCF_ROW_FUNCTION
app_parse_scf_row_CONFIG_TOPLVL(SchedConf *sc, int rowId, char *tl_str, uint8_t confId)
{
#define TL_TOKENS  7
  char *tokens[TL_TOKENS];
  int ret;
  if (rowId && confId) {}  // avoid compiler warning

  ret = parser_opt_str_vals(tl_str, "\t", TL_TOKENS, tokens);
  if (ret != TL_TOKENS)
    return -1;

  if (strcmp(tokens[0],"DCB_Q")==0)
    sc->schedMode = SCHED_MODE_DCB_Q;
  else if (strcmp(tokens[0],"RR")==0)
    sc->schedMode = SCHED_MODE_SRR;
  else if (strcmp(tokens[0],"L2FWD")==0)
    sc->schedMode = SCHED_MODE_L2FWD;
  else
    rte_exit(EXIT_FAILURE, "ERROR: unexpected schedule algorithms %s, expects GBS or RR!\n", tokens[0]);

  // AF250619: The number of GBS queues is defined in the *tm.cfg file. The number includes Queue 0,
  // which is the virtual empty queue. The number does not include lower-priority queues
  // The lower-priority queues cannot be added to the set of queues derived from the configuration,
  // because their placement would change when the scheduler configuration is swapped (and their packets would get lost).
  
  sc->queuesNum = (uint16_t)  atoi(tokens[1]) & 0xffff;
  sc->timeslotsPerSeq = (uint16_t) atoi(tokens[2]) & 0xffff;
  sc->maxPktSize = (uint16_t) atoi(tokens[3]) & 0xffff;
  sc->baseStreamId = (uint16_t) atoi(tokens[4]) & 0xffff;
  schedClassifierType = sc->classifierType = (uint16_t) atoi(tokens[5]) & 0xffff;

  sc->ecnThreshold   = (uint32_t)atoi(tokens[6]);
  ecn_mark_threshold = sc->ecnThreshold; 



  double  delay_ms = strtod(tokens[6], NULL);        /* may be 0        */
  uint32_t speed   = runConf.linkSpeedMbpsConf;

 
  const double bits_per_pkt = 1542.0 * 8.0;
  double pkts = delay_ms * (double)speed * 1000.0 / bits_per_pkt;
  ecn_mark_threshold = (uint32_t)(pkts + 0.5);   /* round ‑> nearest */

  // AF DEBUG
  printf("Recorded classifier type: %u\n", schedClassifierType);
  // END DEBUG

  // Initialize the VLAN ID table
  memset(vlanid_table, 0, 4096 * sizeof(VlanLookupEntry));
  
  return 0;
}

static SCF_ROW_FUNCTION
app_parse_scf_row_GBS_PSS(SchedConf *sc, int rowId, char *tsq_str, uint8_t confId)
{
  #define TSQ_TOKENS 2
  char *token[TSQ_TOKENS];
  int i; char *ptr;
  if (rowId) {}  // avoid compiler warning

  for (i=0, ptr=strtok(tsq_str, "\t"); ptr && i<TSQ_TOKENS; i++, ptr=strtok(NULL, "\t"))
    token[i] = ptr;

  int slot = atoi(token[0]);
  if (slot != rowId)
  {
    printf("ERROR: GBS_PSS column 1 expected %d, got %s\n", rowId, token[0]);
    return -1;
  }

  uint16_t bid = (uint16_t) atoi(token[1]);
  if (bid >= sc->queuesNum)
  {
    printf("ERROR: GBS_PSS slot#%d field2 bundleId %s is not within 0..%d\n", slot, token[1], (int)(sc->queuesNum - 1));
    return -1;
  }

  sc->pss[confId][slot] = bid;
  sc->bundleConf[confId][bid].bid = bid;
  sc->bundleConf[confId][bid].numTimeslots++;

  return 0;
}

static SCF_ROW_FUNCTION
app_parse_scf_row_GBS_SCHEDULING_RATE(SchedConf *sc, int rowId, char *sr_str, uint8_t confId)
{
  #define SR_TOKENS 3
  char *token[SR_TOKENS];
  int i; char *ptr;
  if (rowId) {}  // avoid compiler warning

  for (i=0, ptr=strtok(sr_str, "\t"); ptr && i<SR_TOKENS; i++, ptr=strtok(NULL, "\t"))
    {
      token[i] = ptr;
    }

  uint16_t bid = atoi(token[0]);
  if (bid >= NUM_GBSQUEUES_MAX)
  {
    printf("ERROR: gbs bid#%d outside range 0..%d\n", bid, (NUM_GBSQUEUES_MAX - 1));
    return -1;
  }

  uint32_t schedRate = (uint32_t) atoi(token[1]);
  if (schedRate > runConf.linkSpeedMbpsConf)
  {
    printf("ERROR: gbs bundle#%d has rate %s greater than link rate %d\n", bid, token[1], runConf.linkSpeedMbpsConf);
    return -1;
  }
  sc->bundleConf[confId][bid].schedRate = schedRate;

  uint16_t pathid = atoi(token[2]);
  if (pathid >= NUM_GBSQUEUES_MAX)
  {
    printf("ERROR: gbs path ID #%d outside range 0..%d\n", pathid, (NUM_GBSQUEUES_MAX - 1));
    return -1;
  }
  sc->bundleConf[confId][bid].pathId = pathid;
  sc->pathConf[confId][pathid].schedRate += schedRate;
  sc->pathConf[confId][pathid].numTimeslots += sc->bundleConf[confId][bid].numTimeslots;

  
  // DEBUG
  printf("Conf #%d Bundle Id %u  Rate %u  numTimeslots: %d  Path: %u  PathRate: %u  PathTimeslots: %d\n",
	 confId, sc->bundleConf[confId][bid].bid, 
	 sc->bundleConf[confId][bid].schedRate, 
	 sc->bundleConf[confId][bid].numTimeslots, 
	 sc->bundleConf[confId][bid].pathId,
	 sc->pathConf[confId][sc->bundleConf[confId][bid].pathId].schedRate, 
	 sc->pathConf[confId][sc->bundleConf[confId][bid].pathId].numTimeslots) ;
  // END DEBUG
  
  return 0;
}

static SCF_ROW_FUNCTION
app_parse_scf_row_GBS_BUNDLE_MAPPING(SchedConf *sc, int rowId, char *bm_str, uint8_t confId)
{
  int bmTokens = 2;

  if (schedClassifierType == VLANID_SRCMAC_CLASSIFIER)
    {
      bmTokens = 4;
    }

  char **token = (char **)malloc(bmTokens * sizeof(char *));

  int i; char *ptr;
  if (rowId) {}  // avoid compiler warning

  for (i=0, ptr=strtok(bm_str, "\t"); ptr && i<bmTokens; i++, ptr=strtok(NULL, "\t"))
    token[i] = ptr;

  int bid = atoi(token[0]);
  if (bid < 0 || bid >= NUM_GBSQUEUES_MAX)
  {
    printf("ERROR: bundle id %d outside range 0..%d\n", bid, (NUM_GBSQUEUES_MAX - 1));
    return -1;
  }

  BundleConf *bc = &(sc->bundleConf[confId][bid]);

  int qid = atoi(token[1]);
  if (qid < 0 || qid >= NUM_GBSQUEUES_MAX)
  {
    printf("ERROR: gbs qid#%d outside range 0..%d\n", qid, (NUM_GBSQUEUES_MAX - 1));
    return -1;
  }

  if (bc->numQueues == QUEUES_PER_BUNDLE_MAX)
  {
    printf("ERROR: max number of queues reach (%d = %d) for bundle id %d\n", 
           bc->numQueues, QUEUES_PER_BUNDLE_MAX, bid);
    return -1;
  }

  bc->queues[bc->numQueues] = qid;

  if (schedClassifierType == VLANID_SRCMAC_CLASSIFIER)
    {
      // If packet classification is based on VLAN ID and SRC MAC address,
      // load the corresponding two fields
      int vlanid = atoi(token[2]);
      if ((vlanid < 0) || (vlanid > 4095))
	{
	  rte_exit(EXIT_FAILURE, "ERROR: Bad VLAN ID value (%d) for Queue %d in Bundle %d\n",
		   vlanid, qid, bid);
	}

      int addrlen = 0;
      if(vlanid_table[vlanid].qid1 == 0)
	{
	  addrlen = app_parse_scf_mac_addr_str((uint8_t *)(&(vlanid_table[vlanid].macaddr1)), token[3]);
	  vlanid_table[vlanid].qid1 = (uint16_t)qid;
	  vlanid_table[vlanid].vlanId = (uint16_t)vlanid;
	}
      else
	{
	  addrlen = app_parse_scf_mac_addr_str((uint8_t *)(&(vlanid_table[vlanid].macaddr2)), token[3]);
	  vlanid_table[vlanid].qid2 = (uint16_t)qid;
	}
      if (addrlen != 6)
	{
	  rte_exit(EXIT_FAILURE, "ERROR: Bad GBS_BUNDLE_MAPPING srcMAC %s!\n", token[3]);	// expected colon separated 2-digit hex string
	}

      // AF250617 DEBUG
      printf("VLAN ID TABLE ENTRY %d: VLANID: %u QID1: %u QID2: %u",
	     vlanid,
	     vlanid_table[vlanid].vlanId,
	     vlanid_table[vlanid].qid1,
	     vlanid_table[vlanid].qid2);
      printf(" MAC1: ");
      mac_address_printf(&(vlanid_table[vlanid].macaddr1));
      printf(" MAC2: ");
      mac_address_printf(&(vlanid_table[vlanid].macaddr2));
      printf("\n");      
      // END DEBUG
    }

  // Look at the next row, if any
  (bc->numQueues)++;

  return 0;
}

int
app_parse_scf_cfgfile(SchedConf *sc, const char *cfgfile, uint8_t confId)
{
  #define LINE_LENGTH_MAX 255
  char line[LINE_LENGTH_MAX+1] = { 0 };
  char copied[LINE_LENGTH_MAX+1];
  int lines=0, sectRow=0;
  int ret=0;
  static struct ScfSectMap_s
  {
    const char *sectName;
    SCF_ROW_FNPTR fnptr;
  } scfSectMap[] = 
  {
    { "[CONFIG_DESCRIPTION]",      &app_parse_scf_row_CONFIG_DESCRIPTION },
    { "[CONFIG_TOPLVL]",           &app_parse_scf_row_CONFIG_TOPLVL },
    { "[GBS_TIMESLOT_QUEUE_MAP]",  &app_parse_scf_row_GBS_PSS },
    { "[GBS_SCHEDULING_RATE]",     &app_parse_scf_row_GBS_SCHEDULING_RATE },
    { "[GBS_BUNDLE_MAPPING]",      &app_parse_scf_row_GBS_BUNDLE_MAPPING }
  };
  #define SCF_SECTMAP_NUM  (sizeof(scfSectMap)/sizeof(scfSectMap[0]))
  SCF_ROW_FNPTR sectFnptr = NULL;

  FILE *file = fopen(cfgfile, "r");
  if (file == NULL)
  {
    perror(cfgfile);
    return -1;
  }

  unsigned s=0;
  while (fgets(line, LINE_LENGTH_MAX, file) != NULL && ret>=0)
  {
    lines++;
    //printf("Read line: %s", line);
    if (strlen(line) == 0 || line[0]=='#')
      continue;

    parser_dupstr(copied, line, LINE_LENGTH_MAX);
    if (copied[0] == '[')
    {
      sectFnptr = NULL;
      sectRow=0;  // row index within its section
      for (s=0; s<SCF_SECTMAP_NUM; s++)
      {
        if (strncmp(copied, scfSectMap[s].sectName, strlen(scfSectMap[s].sectName)) == 0) 
        {
          sectFnptr = scfSectMap[s].fnptr;
          break;
        }
      }
      if (sectFnptr == NULL)
      {
        printf("ERROR: sched cfgfile %s line#%d has unknown section name %s\n", cfgfile, lines, copied);
        ret = -1;
      }
      continue;
    }

    // Invoke its parser function. s:index to scfSectMap[] table; sectRow:index within its section in cfgfile. 
    ret = scfSectMap[s].fnptr(sc, sectRow, copied, confId);
    if (ret != 0)
    {
      printf("ERROR: cfgfile %s line#%d parsing failed!\n", cfgfile, lines);
      ret = -1;
      break;
    }

    sectRow++;
  }

  if (ret!=0)
  {
    printf("ERROR: cfgfile %s parsing failed after %d lines\n", cfgfile, lines);
  }
  else
  {
    // Sanity check
    if (sc->timeslotsPerSeq==0 || sc->queuesNum==0 )
    {
      printf("ERROR: cfgfile %s parsing sanity check failed after %d lines, inconsistent/missing config entries:\n", cfgfile, lines);
      printf("\ttimeslotsPerSeq: %u\n", sc->timeslotsPerSeq);
      printf("\tqueuesNum: %u\n", sc->queuesNum);
      ret = -1;
    }
  }

  fclose(file);
  return ret;
}

// Scheduling Sequence Configuration
int
app_parse_scf(uint8_t sid, const char *fname, uint8_t confId)
{
  SchedConf *sc = &schedConf[sid];

  printf("Parsing %s\n", fname);

  int ret = app_parse_scf_cfgfile(sc, fname, confId);
  if (ret != 0)
    rte_exit(EXIT_FAILURE, "ERROR: scf file %s parsing failed for schedId%u!\n", fname, sid);

  return 0;
}

