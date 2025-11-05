/* parserCmdline.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "parserLib.h"

#include "../common/OrionDpdk.h"
#include "../common/OrionLog.h"

#include <locale.h>

static const char usage[] =
	"%s <application parameters>                                                    \n"
	"Application mandatory parameters:                                              \n"
	"    --pfc \"A,B,C,D,E,F\" :  Scheduler Instance Packet Flow Configs (pfc)\n"
	"           A = Scheduler Id (0..x)                                             \n"
	"           B = Tx port for scheduler output                                    \n"
	"           C = TM lcore to dequeue pkts for transmission                       \n"
	"           D = Tx lcore for scheduled pkts to dpdk driver                      \n"
	"           E = \"interface config\" : interface Config File                    \n"
	"           F = \"scheduler config\" : Scheduler Config File                    \n"
	"           G = \"stream config\"    : Stream Config File                       \n"
	"                                                                               \n"
	"Application optional parameters:                                               \n"
	"    --help:  print this usage                                                  \n"
	"    --lim A,B:  Limit of maximum run duration                                  \n"
	"           A = packets (default=0 for no limit)                                \n"
	"           B = timeslots (default=0 for no limit)                              \n"
	"    --speed mbps : override link speed                                         \n"
	"    --promis-off : disable unmatched dstMAC unicast traffic also to DPDK       \n"
	"    --stp sec : Statistics display timer priod in seconds (default is %u)      \n"
;

/* display usage */
static void
app_usage(const char *prgname)
{
	printf(usage, prgname, STATS_TIMER_PERIOD_DEFAULT);
}

static int
sched_parse_timer_period(const char *q_arg)
{
	char *end = NULL;
	int n;

	/* parse number string */
	n = strtol(q_arg, &end, 10);
	if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;
	if (n >= STATS_TIMER_PERIOD_MAX)
		return -1;

	return n;
}

static int
sched_parse_speed(const char *q_arg)
{
	char *end = NULL;
	int n;

	/* parse number string */
	n = strtol(q_arg, &end, 10);
	if ((q_arg[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	return n;
}

static int
app_parse_pfc(const char *pfc_arg)
{
#define NUM_TOKENS 8
	int ret;
	char *tokens[NUM_TOKENS];
	char pfc_str[512];
        struct stat file_stat;
	strcpy(pfc_str, pfc_arg);

	ret = parser_opt_str_vals(pfc_str, ",", NUM_TOKENS, tokens);
	if (ret != NUM_TOKENS)
		rte_exit(EXIT_FAILURE, "ERROR: unexpected tokens for pfc option, expected %u got %d!\n", NUM_TOKENS, ret); 

	int sid = atoi(tokens[0]);
	if (sid != 0 || sid>=NUM_SCHED_MAX)
		rte_exit(EXIT_FAILURE, "ERROR: only 1 scheduler instance supported, got %u!\n", sid); 
	SchedConf *sc = &schedConf[sid];

	sc->schedId    = (uint8_t) sid;
	// Set to use initial config
	sc->confId   = 0;   
	sc->newConfig   = false;   
	sc->rxPort  = (uint8_t) atoi(tokens[1]) & 0xff;
        sc->txPort  = (uint8_t) atoi(tokens[2]) & 0xff;
        sc->rxCore  = (uint8_t) atoi(tokens[3]) & 0xff;
        sc->tmCore  = (uint8_t) atoi(tokens[4]) & 0xff;
        sc->txCore  = (uint8_t) atoi(tokens[5]) & 0xff;
	char *icf      = tokens[6];
	char *scf      = tokens[7];
	// char *strmcf   = tokens[6];

	// parse interface config file
	uint32_t icfLenMax = sizeof(sc->intfCfgFile) - 1;
	if (strlen(icf) >= icfLenMax)
	{
		RTE_LOG(ERR, PARSER, "Interface config file (%s) string length exceeded max of %u\n", icf, icfLenMax);
		return -1;
	}
	memcpy(sc->intfCfgFile, icf, icfLenMax);
	ret = app_parse_icf(sc->schedId, sc->intfCfgFile);
	if (ret)
	{
		RTE_LOG(ERR, PARSER, "Invalid interface config file %s parsing of pfc %s\n", icf, pfc_str);
		return ret;
	}

	// parse scheduler config file
	uint32_t scfLenMax = sizeof(sc->schedCfgFile) - 1;
	memcpy(sc->schedCfgFile, scf, scfLenMax);
	if (strlen(scf) >= scfLenMax)
	{
		RTE_LOG(ERR, PARSER, "Scheduler config file (%s) string length exceeded max of %u\n", scf, scfLenMax);
		return -1;
	}
	int err = stat(sc->schedCfgFile, &file_stat);
        if(err != 0) {
          printf("Error getting file stat in file_is_modified ");
        }
        sc->lastUpdateTime = file_stat.st_mtime;
        // Initial configId is 0
	ret = app_parse_scf(sc->schedId, sc->schedCfgFile, sc->confId);
	if (ret)
	{
		RTE_LOG(ERR, PARSER, "Invalid scheduler config file %s parsing of pfc %s\n", scf, pfc_str);
		return ret;
	}
#if 0
	// parse stream config file
	uint32_t strmcfLenMax = sizeof(sc->streamCfgFile) - 1;
	if (strlen(strmcf) >= strmcfLenMax)
	{
		RTE_LOG(ERR, PARSER, "Stream config file (%s) string length exceeded max of %u\n", strmcf, strmcfLenMax);
		return -1;
	}
	memcpy(sc->streamCfgFile, strmcf, strmcfLenMax);
	ret = app_parse_strmcf(sc->schedId, sc->streamCfgFile, sc->confId); // Update stream cfg
	if (ret)
	{
		RTE_LOG(ERR, PARSER, "Invalid stream config file %s parsing of pfc %s\n", strmcf, pfc_str);
		return ret;
	}
#endif
	// NOTE: No validation of overlapping rxPorts when multiple scheduler instanance is added!
	//enabledPortsMask = (1 << sc->txPort);
        enabledPortsMask = (1 << sc->rxPort) | (1 << sc->txPort);
	printf("Derived portsMask = 0x%x\n", enabledPortsMask);

	return 0;
}

static int
app_parse_lim(const char *lim_str)
{
#define NUM_LIMVALS 2
	int ret;
	uint32_t vals[NUM_LIMVALS];

	memset(vals, 0, sizeof(vals));
	ret = parser_opt_int_vals(lim_str, ',', NUM_LIMVALS, vals);
	if (ret != NUM_LIMVALS)
		rte_exit(EXIT_FAILURE, "ERROR: unexpected tokens for lim option for %s, expected %u got %d!\n", lim_str, NUM_LIMVALS, ret); 

	runConf.maxRunPkts = (uint32_t) vals[0];
	runConf.maxRunTimeslots  = (uint32_t) vals[1];
	return 0;
}

/*
 * Parses the argument given in the command line of the application,
 * calculates mask for used cores and initializes EAL with calculated core mask
 */
int
parse_args(int argc, char **argv)
{
	int opt, ret;
	int option_index;
	const char *optname;
	char *prgname = argv[0];

	enum ParsedOptionsMask_e
	{
		PARSED_OPTION_PFC	= 0x0001,
		PARSED_OPTION_SPEED	= 0x0002,
		PARSED_OPTION_STP	= 0x0004,
		PARSED_OPTION_LIM	= 0x0008,
		PARSED_OPTION_PROMIS	= 0x0010,
		PARSED_OPTION_HELP	= 0x8000
	};

	#define PARSED_OPTIONS_REQUIRED (PARSED_OPTION_PFC)
	uint16_t parsedOptionsMask=0;	// ParsedOptionsMask_e bit mask

	// Fields: char *name, int has_arg, int *flag, int val.
	static struct option lgopts[] = {
		{ "pfc", 1, NULL, 0 },
		{ "speed", 1, NULL, 0 },
		{ "stp", 1, NULL, 0 },
		{ "lim", 1, NULL, 0 },
		{ "promis-off", 0, NULL, 0 },
		{ "help", 0, NULL, 0 },
		{ NULL,  0, NULL, 0 }
	};

	/* set en_US locale to print big numbers with ',' */
	setlocale(LC_NUMERIC, "en_US.utf-8");

	while ((opt = getopt_long(argc, argv, "i",
		lgopts, &option_index)) != EOF)
	{

                //printf("%s\n", lgopts[option_index].name);
		switch (opt)
		{
			/* long options */
			case 0:
				optname = lgopts[option_index].name;
				if (strcmp(optname, "pfc")==0)
				{
					ret = app_parse_pfc(optarg);
					if (ret)
					{
						RTE_LOG(ERR, PARSER, "Invalid parsing of pfc %s\n", optarg);
						return -1;
					}
					parsedOptionsMask |= PARSED_OPTION_PFC;
					break;
				}
				else if (strcmp(optname, "lim")==0)
				{
					ret = app_parse_lim(optarg);
					if (ret)
					{
						RTE_LOG(ERR, PARSER, "Invalid parsing of lim %s\n", optarg);
						return -1;
					}
					parsedOptionsMask |= PARSED_OPTION_LIM;
					break;
				}
				else if (strcmp(optname, "promis-off")==0)
				{
					runConf.promiscuous = false;
					parsedOptionsMask |= PARSED_OPTION_PROMIS;
					break;
				}
				else if (strcmp(optname, "speed")==0)
				{
					int speed = sched_parse_speed(optarg);
					if (speed < 1 || speed > 100000)
					{
						RTE_LOG(ERR, PARSER, "Invalid speed %s, expected between 1Mbps and 100Gbps\n", optarg);
						return -1;
					}
					runConf.linkSpeedMbpsConf = speed;
					parsedOptionsMask |= PARSED_OPTION_SPEED;
					break;
				}
				else if (strcmp(optname, "stp")==0)
				{
					int sec = sched_parse_timer_period(optarg);
					// if (sec == 0 || sec > STATS_TIMER_PERIOD_MAX)
					if (sec < 0 || sec > STATS_TIMER_PERIOD_MAX)
					{
						RTE_LOG(ERR, PARSER, "Invalid parsing of Statistics Timer Period %s\n", optarg);
						return -1;
					}
					runConf.statsTimerSec = sec;
					parsedOptionsMask |= PARSED_OPTION_STP;
					break;
				}
				else if (strcmp(optname, "help")==0)
				{
					app_usage(prgname);
					parsedOptionsMask |= PARSED_OPTION_HELP;
					rte_exit(EXIT_SUCCESS, "Bye...\n");
				}
				break;
			default:
				app_usage(prgname);
				rte_exit(EXIT_FAILURE, "Bye...\n");
		}
	}

	if ((parsedOptionsMask & PARSED_OPTIONS_REQUIRED) != PARSED_OPTIONS_REQUIRED)
	{
		printf("ERROR: Parser missing required options, mask expected=x%04x has=x%04x\n", 
		       PARSED_OPTIONS_REQUIRED, (parsedOptionsMask & PARSED_OPTIONS_REQUIRED));
		app_usage(prgname);
		rte_exit(EXIT_FAILURE, "Bye...\n");
	}

	return 0;
}
