/*
 * Copyright (C) 2009-2013 Codership Oy <info@codership.com>
 *
 * $Id$
 */

#include "glb_misc.h"

#include <ctype.h>
#include <assert.h>

/*!
 * convert string into array of tokens
 *
 * @param sep - additional separator to whitespace
 */
bool
glb_parse_token_string (char*         tok_str,
                        const char*** tok_list,
                        int*          tok_num,
                        int           sep)
{
    assert (tok_str);

    *tok_list = NULL;
    *tok_num  = 0;

    if (!tok_str) return true;

    size_t const tlen = strlen(tok_str);
    if (!tlen) return true;

    const char** list = NULL;
    int num = 0;

    size_t i;
    for (i = 1; i <= tlen; i++) /* we can skip the first string char */
    {
        if (isspace(tok_str[i]) || sep == tok_str[i]) tok_str[i] = '\0';
        if (tok_str[i] == '\0' && tok_str[i-1] != '\0') num++;/* end of token */
    }

    if (num == 0) return true;

    list = calloc (num, sizeof(const char*));
    if (!list) return true;

    list[0] = tok_str;
    num = 1;

    for (i = 1; i <= tlen; i++)
    {
        if (tok_str[i-1] == '\0' && tok_str[i] != '\0') /* beginning of token */
        {
            list[num] = &tok_str[i];
            num++;
        }
    }

    *tok_list = list;
    *tok_num  = num;

    return false;
}

