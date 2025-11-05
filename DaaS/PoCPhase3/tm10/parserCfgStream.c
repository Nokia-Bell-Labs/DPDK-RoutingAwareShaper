/* parserCfgStream.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "parserLib.h"

typedef int CFG_ROW_FUNCTION;
typedef int (*CFG_ROW_FNPTR)(SchedConf *sc, int row, char *str, uint8_t confId);

static int
parse_ipv4_str(uint8_t *ipv4, char *str)
{
  int ret = sscanf(str, "%hhu.%hhu.%hhu.%hhu", &ipv4[0], &ipv4[1], &ipv4[2], &ipv4[3]);
  return ret;
}

static CFG_ROW_FUNCTION
app_parse_cfg_row_CONFIG_DESCRIPTION(SchedConf *dummy, int rowId, char *cd_str, uint8_t confId)
{
  // Informational
  if (dummy && rowId && confId) {}  // avoid compiler warning
  printf("%s\n", cd_str);
  return 0;
}

static CFG_ROW_FUNCTION
app_parse_cfg_row_CONFIG_STREAMS(SchedConf *sc, int rowId, char *tl_str, uint8_t confId)
{
#define TL_STREAMTOKENS  10
  char *tokens[TL_STREAMTOKENS];
  int ret;
  
  if (rowId) {}  // avoid compiler warning

  ret = parser_opt_str_vals(tl_str, "\t", TL_STREAMTOKENS, tokens);
  if (ret != TL_STREAMTOKENS)
    {
      return -1;
    }

  static int streamIdNext;
  int pos = 0;

        // streamId
  char *token=tokens[pos];
  int streamId = atoi(token);
  //if (streamId<=0 || streamIdNext!=streamId || streamId>NUM_STREAMS_MAX) 
  if ( (streamId <= 0) || (streamId > NUM_STREAMS_MAX))
    {
      rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS invalid streamId %s, expected 0..%d!\n", token, NUM_STREAMS_MAX);
    }

  if (sc->streamsBaseNum == 0)
    {
      sc->streamsBaseNum = streamId;
      streamIdNext = streamId;
    }
  if (streamIdNext != streamId)
    {
      rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS invalid streamId %s, streamId not sequential!\n", token);
    }

  StreamCfg *stream = &sc->streamCfg[confId][STREAM_ID_TO_IDX(streamId)];
  stream->streamId = streamId;
  streamIdNext++; // streams defintions expected to be sequential

        // srcIP
  token = tokens[++pos];
  ret = parse_ipv4_str(stream->srcIP, token);
  if (ret != 4)
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d srcIP %s invalid!\n", streamId, token);

        // dstIP
  token = tokens[++pos];
  ret = parse_ipv4_str(stream->dstIP, token);
  if (ret != 4)
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d dstIP %s invalid!\n", streamId, token);

        // rate
  token = tokens[++pos];
  float rate = strtof(token, NULL);
  if ( (rate < 0.0) || (rate > (float)(runConf.linkSpeedMbpsConf)) )
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d rate %s invalid!\n", streamId, token);
  stream->rate = rate;

        // latency
  token = tokens[++pos];
  stream->latency = strtof(token, NULL);

        // pktsize
  token = tokens[++pos];
  stream->pktsize = (uint16_t) strtol(token, NULL, 0);
  if ( (stream->pktsize < 64) || (stream->pktsize > 4000) )
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d pktsize %s!\n", streamId, token);

	// vlanId
  token = tokens[++pos];
  stream->vlanId = (uint32_t) strtol(token, NULL, 0);
  if (stream->vlanId>4095)
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d vlanId=%s, expected 0..4095!\n", streamId, token);

        // vlanPri
  token = tokens[++pos];
  stream->vlanPri = atoi(token);
  if (stream->vlanPri>7)
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d vlanPri=%s, expected 0..7!\n", streamId, token);

	// avg_on_time
  token = tokens[++pos];
  float avg_on_time = strtof(token, NULL);
  if (avg_on_time < 0.0)
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d avg_on_time %s invalid!\n", streamId, token);
  stream->avg_on_time = avg_on_time;

	// avg_off_time
  token = tokens[++pos];
  float avg_off_time = strtof(token, NULL);
  if (avg_off_time < 0.0)
    rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_STREAMS stream#%d avg_off_time %s invalid!\n", streamId, token);
  stream->avg_off_time = avg_off_time;

  // set constant (for now) and derived parameters
  // stream->ttl = stream->streamId + 32;// AF221201: Removing old behavior
  stream->ttl = 128;
  stream->protocol = (uint8_t)IPPROTO_UDP;
  
  // update the number of streams
  sc->numStreams++;
  
  return 0;
}

static int
app_parse_cfg_streamfile(SchedConf *sc, const char *streamfile, uint8_t confId)
{
  #define LINE_LENGTH_MAX 255
  char line[LINE_LENGTH_MAX+1] = { 0 };
  char copied[LINE_LENGTH_MAX+1];
  int lines=0, sectRow=0;
  int ret=0;
  static struct StreamSectMap_s
  {
    const char *sectName;
    CFG_ROW_FNPTR fnptr;
  } streamSectMap[] = 
  {
    { "[CONFIG_DESCRIPTION]",  &app_parse_cfg_row_CONFIG_DESCRIPTION },
    { "[CONFIG_STREAMS]",    &app_parse_cfg_row_CONFIG_STREAMS }
  };
  #define STREAM_SECTMAP_NUM  (sizeof(streamSectMap)/sizeof(streamSectMap[0]))
  CFG_ROW_FNPTR sectFnptr = NULL;

  FILE *file = fopen(streamfile, "r");
  if (file == NULL)
  {
    perror(streamfile);
    return -1;
  }

  //printf("DEBUG: parse_stream_streamfile(%s): Entered\n", streamfile);

  printf("============ TG Stream Config ==============\n");

  unsigned s=0;
  while (fgets(line, LINE_LENGTH_MAX, file) != NULL && ret>=0)
  {
    lines++;
    //printf("DEBUG: Read line#%d: %s\n", lines, line);
    if (strlen(line) == 0 || line[0]=='#')
      continue;

    parser_dupstr(copied, line, LINE_LENGTH_MAX);
    if (copied[0] == '[')
    {
      sectFnptr = NULL;
      sectRow=0;  // row index within its section
      for (s=0; s<STREAM_SECTMAP_NUM; s++)
      {
        if (strncmp(copied, streamSectMap[s].sectName, strlen(streamSectMap[s].sectName)) == 0) 
        {
          sectFnptr = streamSectMap[s].fnptr;
          break;
        }
      }
      if (sectFnptr == NULL)
      {
        printf("ERROR: streamfile %s line#%d has unknown section name %s\n", streamfile, lines, copied);
        ret = -1;
      }
      continue;
    }

    // Invoke its parser function. s:index to streamSectMap[] table; sectRow:index within its section in streamfile. 
    //printf("DEBUG: Parsing Section [%s]\n", copied);
    ret = streamSectMap[s].fnptr(sc, sectRow, copied, confId);
    if (ret != 0)
    {
      printf("ERROR: streamfile %s line#%d parsing failed\n", streamfile, lines);
      ret = -1;
      break;
    }

    sectRow++;
  }

  if (ret!=0)
  {
    printf("ERROR: streamfile %s parsing failed after %d lines\n", streamfile, lines);
  }
  else
  {
    // Sanity check: NONE
  }

  printf("============================================\n");

  fclose(file);
  return ret;
}

// Parse Stream Configuration File
int
app_parse_strmcf(uint8_t sid, const char *fname, uint8_t confId)
{
  SchedConf *sc = &schedConf[sid];

  printf("Parsing %s\n", fname);

  int ret = app_parse_cfg_streamfile(sc, fname, confId);
  if (ret != 0)
    rte_exit(EXIT_FAILURE, "ERROR: streamfile %s parsing failed!\n", fname);

  return 0;
}

