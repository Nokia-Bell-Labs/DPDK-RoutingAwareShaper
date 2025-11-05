/* parserLib.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _PARSER_LIB_H_
#define _PARSER_LIB_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// returns non-zero if str1 and str2 are identical
static inline int str_identical(const char *str1, const char *str2)
{
        return strcmp(str1, str2) == 0;
}


int parser_opt_int_vals(const char *conf_str, char separator, uint32_t n_vals, uint32_t *opt_vals);
int parser_opt_str_vals(char *conf_str, const char *separator, uint32_t n_vals, char *token[]);
int parser_dupstr(char *new, char *orig, int max);	// Make copy of original string and rid off trailing \n with NULL

int app_parse_icf(uint8_t sid, const char *fname);
int app_parse_scf(uint8_t sid, const char *fname, uint8_t confId);
//int app_parse_strmcf(uint8_t sid, const char *fname);
int app_parse_strmcf(uint8_t sid, const char *fname, uint8_t confId);  // Update stream cfg

#endif  // End of _PARSER_LIB_H_
