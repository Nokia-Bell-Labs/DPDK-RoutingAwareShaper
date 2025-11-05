/* parserCfgIntf.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "parserLib.h"
#include "../common/OrionDpdk.h"

typedef int ICF_ROW_FUNCTION;
typedef int (*ICF_ROW_FNPTR)(IntfConf *ic, int row, char *str);

static int
app_parse_mac_addr_str(uint8_t *mac, char *str)
{
	return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
}

static int
parse_ipv4_str(uint32_t *ipv4, char *str)
{
	#define IPV4_ADDR(_ip)((_ip[0] << 24) | (_ip[1] << 16) | (_ip[2] << 8) | _ip[3])
	uint8_t ip[4];
	int ret = sscanf(str, "%hhu.%hhu.%hhu.%hhu", &ip[0], &ip[1], &ip[2], &ip[3]);
	if (ret!=4)
		return -1;
	//*ipv4 = rte_cpu_to_be_32(IPV4_ADDR(ipv4));	
	*ipv4 = rte_cpu_to_be_32(IPV4_ADDR(ip));	
	return 0;
}

static ICF_ROW_FUNCTION
app_parse_icf_row_CONFIG_DESCRIPTION(IntfConf *dummy, int rowId, char *cd_str)
{
	// Informational
	if (dummy && rowId && cd_str) {}	// avoid compiler warning
	printf("\t%s\n", cd_str);
	return 0;
}

static ICF_ROW_FUNCTION
app_parse_icf_row_CONFIG_SCHED_MAC(IntfConf *ic, int rowId, char *tl_str)
{
#define TL_TOKENS	2
	char *tokens[TL_TOKENS];
	int ret;
	if (rowId) {}	// avoid compiler warning

	ret = parser_opt_str_vals(tl_str, "\t", TL_TOKENS, tokens);
	if (ret != TL_TOKENS)
		return -1;

	if (strcmp(tokens[0],"srcMAC")==0)
	{
		int l = app_parse_mac_addr_str(ic->schedPath.srcAddr.addr_bytes, tokens[1]);
		if (l != 6)
			rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_SCHED_MAC srcMAC %s!\n", tokens[1]);	// expected colon separated 2-digit hex string
	}
	else if (strcmp(tokens[0],"dstMAC")==0)
	{
		int l = app_parse_mac_addr_str(ic->schedPath.dstAddr.addr_bytes, tokens[1]);
		if (l != 6)
			rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_SCHED_MAC dstMAC %s!\n", tokens[1]);	// expected colon separated 2-digit hex string
	}
	else
		rte_exit(EXIT_FAILURE, "ERROR: unexpected CONFIG_SCHED_MAC field type %s!\n", tokens[0]);

	return 0;
}

static ICF_ROW_FUNCTION
app_parse_icf_row_CONFIG_L2FWD_MAC(IntfConf *ic, int rowId, char *tl_str)
{
#define TL_TOKENS	2
	char *tokens[TL_TOKENS];
	int ret;
	if (rowId) {}	// avoid compiler warning

	ret = parser_opt_str_vals(tl_str, "\t", TL_TOKENS, tokens);
	if (ret != TL_TOKENS)
		return -1;

	if (strcmp(tokens[0],"srcMAC")==0)
	{
		int l = app_parse_mac_addr_str(ic->l2fwdPath.srcAddr.addr_bytes, tokens[1]);
		if (l != 6)
			rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_L2FWD_MAC srcMAC %s!\n", tokens[1]);	// expected colon separated 2-digit hex string
	}
	else if (strcmp(tokens[0],"dstMAC")==0)
	{
		int l = app_parse_mac_addr_str(ic->l2fwdPath.dstAddr.addr_bytes, tokens[1]);
		if (l != 6)
			rte_exit(EXIT_FAILURE, "ERROR: Bad CONFIG_L2FWD_MAC dstMAC %s!\n", tokens[1]);	// expected colon separated 2-digit hex string
	}
	else
		rte_exit(EXIT_FAILURE, "ERROR: unexpected CONFIG_L2FWD_MAC field type %s!\n", tokens[0]);

	return 0;
}

static ICF_ROW_FUNCTION
app_parse_icf_row_CONFIG_VLAN_ENCAP(IntfConf *ic, int rowId, char *tl_str)
{
#define TL_TOKENS	2
	char *tokens[TL_TOKENS];
	int ret;
	if (rowId) {}	// avoid compiler warning

	ret = parser_opt_str_vals(tl_str, "\t", TL_TOKENS, tokens);
	if (ret != TL_TOKENS)
		return -1;

	if (strcmp(tokens[0],"vlan")==0)
	{
		if (str_identical(tokens[1], "enable"))
			ic->vlanTag = true;
		else if (str_identical(tokens[1], "disable"))
			ic->vlanTag = false;
		else
			rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_VLAN_ENCAP vlan argument %s!\n", tokens[1]);
	}
	else if (strcmp(tokens[0],"vlanPri")==0)
	{
		int l = atoi(tokens[1]);
		if (l<0 || l>7)
			rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_VLAN_ENCAP vlanPri argument %s!\n", tokens[1]);
		ic->vlanPri = l;
	}
	else if (strcmp(tokens[0],"vlanId")==0)
	{
		int l = atoi(tokens[1]);
		if (l<0 || l>1023)
			rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_VLAN_ENCAP vlanId argument %s!\n", tokens[1]);
		ic->vlanId = l;
	}
	else
		rte_exit(EXIT_FAILURE, "ERROR: unexpected INTF_CONFIG_VLAN_ENCAP field type %s!\n", tokens[0]);

	return 0;
}

static ICF_ROW_FUNCTION
app_parse_icf_row_CONFIG_SCHED(IntfConf *ic, int rowId, char *tl_str)
{
#define TL_TOKENS	2
	char *tokens[TL_TOKENS];
	int ret;
	if (rowId) {}	// avoid compiler warning

	ret = parser_opt_str_vals(tl_str, "\t", TL_TOKENS, tokens);
	if (ret != TL_TOKENS)
		return -1;

        if (strcmp(tokens[0],"ipVer")==0)
        {
                if (strcmp(tokens[1], "IPV4")==0)
                        ic->ipVer = 4;
                else
                        rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED ipVer %s, expected IPV4!\n", tokens[1]);
        }
	else if (strcmp(tokens[0],"srcIP")==0)
	{
		ret = parse_ipv4_str(&ic->srcIP, tokens[1]);
		if (ret!=0)
			rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED srcIP argument %s!\n", tokens[1]);
	}
	else if (strcmp(tokens[0],"dstIP")==0)
	{
		ret = parse_ipv4_str(&ic->dstIP, tokens[1]);
		if (ret!=0)
			rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED dstIP argument %s!\n", tokens[1]);
	}
        else if (strcmp(tokens[0],"dscp")==0)
        {
                long dscp = strtol(tokens[1], NULL, 0);
                if (dscp > 0x3f)
                        rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED dscp %s, 0..63!\n", tokens[1]);
                ic->dscp = dscp;
	}
        else if (strcmp(tokens[0],"ecn")==0)
        {
                int ecn = atoi(tokens[1]);
                if (ecn < 0 || ecn > 3)
                        rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED ecn %s, 0..3!\n", tokens[1]);
                ic->ecn = ecn;
	}
        else if (strcmp(tokens[0],"dstPort")==0)
        {
                long dstPort = strtol(tokens[1], NULL, 0);
                if ( (dstPort < 0) || (dstPort > 0xffff) )
                        rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED dstPort %s!\n", tokens[1]);
                ic->dstPort = dstPort;
        }
        else if (strcmp(tokens[0],"updateSeqNo")==0)
        {
                if (strcmp(tokens[1], "enable")==0)
                        ic->updateSeqNo = true;
                else if (strcmp(tokens[1], "disable")==0)
                        ic->updateSeqNo = false;
                else
                        rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED updateSeqNo argument %s!\n", tokens[1]);
        }
        else if (strcmp(tokens[0],"hwChksumOffload")==0)
        {
                if (strcmp(tokens[1], "enable")==0)
                        ic->hwChksumOffload = true;
                else if (strcmp(tokens[1], "disable")==0)
                        ic->hwChksumOffload = false;
                else
                        rte_exit(EXIT_FAILURE, "ERROR: Bad INTF_CONFIG_SCHED hwChksumOffload argument %s!\n", tokens[1]);
        }
	else
		rte_exit(EXIT_FAILURE, "ERROR: unexpected INTF_CONFIG_SCHED argument %s!\n", tokens[0]);

	return 0;
}

static int
app_parse_icf_cfgfile(uint8_t sid, const char *cfgfile)
{
	#define LINE_LENGTH_MAX 255
	char line[LINE_LENGTH_MAX+1] = { 0 };
	char copied[LINE_LENGTH_MAX+1];
	int lines=0, sectRow=0;
	int ret=0;
	static struct IcfSectMap_s
	{
		const char *sectName;
		ICF_ROW_FNPTR fnptr;
	} icfSectMap[] = 
	{
		{ "[INTF_CONFIG_DESCRIPTION]",		&app_parse_icf_row_CONFIG_DESCRIPTION },
		{ "[INTF_CONFIG_SCHED_MAC]",		&app_parse_icf_row_CONFIG_SCHED_MAC },
		{ "[INTF_CONFIG_L2FWD_MAC]",		&app_parse_icf_row_CONFIG_L2FWD_MAC },
		{ "[INTF_CONFIG_VLAN_ENCAP]",		&app_parse_icf_row_CONFIG_VLAN_ENCAP },
		{ "[INTF_CONFIG_SCHED]",		&app_parse_icf_row_CONFIG_SCHED }
	};
	#define ICF_SECTMAP_NUM	(sizeof(icfSectMap)/sizeof(icfSectMap[0]))
	ICF_ROW_FNPTR sectFnptr = NULL;

	IntfConf *ic = &intfConf[sid];

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
		//printf("Read line: %s\n", line);
		if (strlen(line) == 0 || line[0]=='#')
			continue;

		parser_dupstr(copied, line, LINE_LENGTH_MAX);
		if (copied[0] == '[')
		{
			sectFnptr = NULL;
			sectRow=0;	// row index within its section
			for (s=0; s<ICF_SECTMAP_NUM; s++)
			{
				if (strncmp(copied, icfSectMap[s].sectName, strlen(icfSectMap[s].sectName)) == 0) 
				{
					sectFnptr = icfSectMap[s].fnptr;
					break;
				}
			}
			if (sectFnptr == NULL)
			{
				printf("ERROR: intf cfgfile %s line #%d has unknown section name %s\n", cfgfile, lines, copied);
				ret = -1;
			}
			continue;
		}

		// Invoke its parser function. s:index to icfSectMap[] table; sectRow:index within its section in cfgfile. 
		ret = icfSectMap[s].fnptr(ic, sectRow, copied);
		if (ret != 0)
		{
			printf("ERROR: intf cfgfile %s line #%d parsing failed for schedId %d\n", cfgfile, lines, sid);
			ret = -1;
			break;
		}

		sectRow++;
	}

	if (ret!=0)
	{
		printf("ERROR: intf cfgfile %s parsing failed after %d lines\n", cfgfile, lines);
	}
	else
	{
		// Sanity check: NONE
	}

	fclose(file);
	return ret;
}

// Parse Interface Configuration File
int
app_parse_icf(uint8_t sid, const char *fname)
{
	printf("Parsing %s\n", fname);

	int ret = app_parse_icf_cfgfile(sid, fname);
	if (ret != 0)
		rte_exit(EXIT_FAILURE, "ERROR: intf cfgfile %s parsing failed for schedId%u!\n", fname, sid);

	return 0;
}

