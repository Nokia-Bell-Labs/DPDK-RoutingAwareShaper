/* parserLib.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "parserLib.h"
#include <rte_string_fns.h>

#define MAX_OPT_VALUES 8

/* returns:
	 number of values parsed
	-1 in case of error
*/
int
parser_opt_int_vals(const char *conf_str, char separator, uint32_t n_vals, uint32_t *opt_vals)
{
	char *string;
	int i, n_tokens;
	char *tokens[MAX_OPT_VALUES];

	if (conf_str == NULL || opt_vals == NULL || n_vals == 0 || n_vals > MAX_OPT_VALUES)
		return -1;

	/* duplicate configuration string before splitting it to tokens */
	string = strdup(conf_str);
	if (string == NULL)
		return -1;

	n_tokens = rte_strsplit(string, strnlen(string, 32), tokens, n_vals, separator);

	if (n_tokens > MAX_OPT_VALUES)
		return -1;

	for (i = 0; i < n_tokens; i++)
		opt_vals[i] = (uint32_t)atol(tokens[i]);	// TODO: replace with strtol(tokens[i], NULL, 0) for hexdecimal

	free(string);

	return n_tokens;
}

/*
 * C++ template would be nice!
 * For ANSI-C, we can have parser_opt_str_vals() replace parser_opt_int_vals() and let caller do
 * context sensitive token specific conversion.
 * WARNING: strtok modifies conf_str. Caller should do strdup() if it needs to be preserved!!
 */
int
parser_opt_str_vals(char *conf_str, const char *separator, uint32_t n_vals, char *token[])
{
	uint32_t i;
	char *ptr;

	for (i=0, ptr=strtok(conf_str, separator); ptr && i<n_vals; i++, ptr=strtok(NULL, separator))
		token[i] = ptr;
	return i;
}

// Make copy of original string and rid off trailing \n with NULL
int
parser_dupstr(char *new, char *orig, int max)
{
	int i;
	new[max-1] = '\0';
	for (i=0; i<(max-1); i++)
	{
		new[i] = orig[i];
		if (orig[i] == '\n')
		{
			new[i] = '\0';
			return i;
		}
	}
	return i;
}

