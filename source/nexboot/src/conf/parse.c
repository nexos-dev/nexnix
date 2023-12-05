/*
    parse.c - contains shell conf parser
    Copyright 2023 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "conf.h"
#include <assert.h>
#include <nexboot/nexboot.h>
#include <stdio.h>
#include <string.h>

// Parser error states
#define PARSE_ERROR_UNEXPECTED_TOKEN 1
#define PARSE_ERROR_INTERNAL         2
#define PARSE_ERROR_OVERFLOW         3

// Reports a diagnostic message
static void parseError (ConfContext_t* ctx, confToken_t* tok, int err)
{
#define ERRBUFSZ 512
    // Prepare error buffer
    char bufData[ERRBUFSZ] = {0};

    char* obuf = bufData;
    char* buf = bufData;
    buf +=
        snprintf (buf, ERRBUFSZ - (buf - obuf), "nexboot: error: %d: ", tok->line);
    // Decide how to handle the error
    switch (err)
    {
        case PARSE_ERROR_UNEXPECTED_TOKEN:
            if (ctx->lastToken)
            {
                buf += snprintf (buf,
                                 ERRBUFSZ - (buf - obuf),
                                 "unexpected token %s after token %s",
                                 confGetTokName (tok),
                                 confGetTokName (ctx->lastToken));
            }
            else
                buf += snprintf (buf,
                                 ERRBUFSZ - (buf - obuf),
                                 "unexpected token %s",
                                 confGetTokName (tok));
            break;
        case PARSE_ERROR_OVERFLOW:
            buf += snprintf (buf,
                             ERRBUFSZ - (buf - obuf),
                             "string too long on token %s",
                             confGetTokName (tok));
            break;
        case PARSE_ERROR_INTERNAL:
            buf += snprintf (buf, ERRBUFSZ - (buf - obuf), "internal error");
            break;
    }
    NbLogMessage (obuf, NEXBOOT_LOGLEVEL_ERROR);
    NbLogMessage ("\n", NEXBOOT_LOGLEVEL_ERROR);
}

static void destroyCmdArg (const void* data)
{
    ConfBlockCmdArg_t* arg = (ConfBlockCmdArg_t*) data;
    if (arg->str.literal)
        StrRefDestroy (arg->str.literal);
    free (arg);
}

static void destroyCmd (ConfBlockCmd_t* cmd)
{
    if (cmd->args)
        ListDestroy (cmd->args);
    if (cmd->cmd.literal)
        StrRefDestroy (cmd->cmd.literal);
}

static void destroyMenuEnt (ConfBlockMenu_t* menu)
{
    StrRefDestroy (menu->name);
    ListDestroy (menu->blocks);
}

static void destroyVarSet (ConfBlockSet_t* var)
{
    StrRefDestroy (var->var);
    StrRefDestroy (var->val.literal);
}

// Destroy a parser block
static void destroyBlock (const void* data)
{
    ConfBlock_t* block = (ConfBlock_t*) data;
    if (block->type == CONF_BLOCK_CMD)
        destroyCmd ((ConfBlockCmd_t*) block);
    else if (block->type == CONF_BLOCK_VARSET)
        destroyVarSet ((ConfBlockSet_t*) block);
    else if (block->type == CONF_BLOCK_MENUENTRY)
        destroyMenuEnt ((ConfBlockMenu_t*) block);
    free (block);
}

// Parses next token
static confToken_t* parseToken (ConfContext_t* ctx, confToken_t* lastTok)
{
    // Free current last token
    if (ctx->lastToken)
    {
        if (ctx->lastToken->semVal)
            StrRefDestroy (ctx->lastToken->semVal);
        free (ctx->lastToken);
    }
    ctx->lastToken = lastTok;
    confToken_t* tok = confLex (ctx);
    if (tok->type == LEX_TOKEN_ERROR)
    {
        free (tok);
        return NULL;
    }
    return tok;
}

// Parses next token, expecting a certain type
static confToken_t* parseExpect (ConfContext_t* ctx, confToken_t* lastTok, int type)
{
    confToken_t* tok = parseToken (ctx, lastTok);
    if (!tok)
        return NULL;
    else if (tok->type != type)
    {
        parseError (ctx, tok, PARSE_ERROR_UNEXPECTED_TOKEN);
        return NULL;
    }
    return tok;
}

// Parses a command construct
static confToken_t* parseCmd (ConfContext_t* ctx,
                              ListHead_t* blocks,
                              confToken_t* tok)
{
    ConfBlockCmd_t* cmd = calloc (1, sizeof (ConfBlockCmd_t));
    if (!cmd)
    {
        parseError (ctx, tok, PARSE_ERROR_INTERNAL);
        return NULL;
    }
    cmd->hdr.lineNo = tok->line;
    cmd->hdr.type = CONF_BLOCK_CMD;
    cmd->args = ListCreate ("ConfBlockCmdArg_t", false, 0);
    ListSetDestroy (cmd->args, destroyCmdArg);
    // Check what we need to do. If tok is ID or string, copy semVal to cmd field
    // If it starts a variable, we need to parse the variable
    if (tok->type == LEX_TOKEN_ID || tok->type == LEX_TOKEN_STR)
    {
        // Copy semVal
        cmd->cmd.literal = StrRefNew (tok->semVal);
        cmd->cmd.type = CONF_STRING_LITERAL;
    }
    else if (tok->type == LEX_TOKEN_DOLLAR)
    {
        // Expect obrace
        tok = parseExpect (ctx, tok, LEX_TOKEN_OBRACE);
        if (!tok)
        {
            destroyCmd (cmd);
            return NULL;
        }
        // Expect identifier
        tok = parseExpect (ctx, tok, LEX_TOKEN_ID);
        if (!tok)
        {
            destroyCmd (cmd);
            return NULL;
        }
        // Copy name
        cmd->cmd.var = StrRefNew (tok->semVal);
        cmd->cmd.type = CONF_STRING_VAR;
        // Expect ebrace
        tok = parseExpect (ctx, tok, LEX_TOKEN_EBRACE);
        if (!tok)
        {
            destroyCmd (cmd);
            return NULL;
        }
    }
    // Loop until we reach newline
    tok = parseToken (ctx, tok);
    if (!tok)
    {
        destroyCmd (cmd);
        return NULL;
    }
    while (tok->type != LEX_TOKEN_NEWLINE && tok->type != LEX_TOKEN_EOF)
    {
        // See if this token is not valid here
        if (!(tok->type == LEX_TOKEN_DOLLAR || tok->type == LEX_TOKEN_ID ||
              tok->type == LEX_TOKEN_STR || tok->type == LEX_TOKEN_LITERAL))
        {
            // Error
            parseError (ctx, tok, PARSE_ERROR_UNEXPECTED_TOKEN);
            destroyCmd (cmd);
            return NULL;
        }
        // Prepare this arguments
        ConfBlockCmdArg_t* arg = calloc (1, sizeof (ConfBlockCmdArg_t));
        if (!arg)
        {
            parseError (ctx, tok, PARSE_ERROR_INTERNAL);
            destroyCmd (cmd);
            return NULL;
        }
        arg->hdr.lineNo = tok->line;
        arg->hdr.type = CONF_BLOCK_CMDARG;
        // See what this is
        if (tok->type == LEX_TOKEN_DOLLAR)
        {
            // Expect obrace
            tok = parseExpect (ctx, tok, LEX_TOKEN_OBRACE);
            if (!tok)
            {
                destroyCmd (cmd);
                return NULL;
            }
            // Expect identifier
            tok = parseExpect (ctx, tok, LEX_TOKEN_ID);
            if (!tok)
            {
                destroyCmd (cmd);
                return NULL;
            }
            // Copy name
            arg->str.var = StrRefNew (tok->semVal);
            arg->str.type = CONF_STRING_VAR;
            // Expect ebrace
            tok = parseExpect (ctx, tok, LEX_TOKEN_EBRACE);
            if (!tok)
            {
                destroyCmd (cmd);
                return NULL;
            }
        }
        else
        {
            // Copy string value
            arg->str.type = CONF_STRING_LITERAL;
            arg->str.literal = StrRefNew (tok->semVal);
        }
        // Add to list
        ListAddBack (cmd->args, arg, 0);
        tok = parseToken (ctx, tok);
    }
    // Add to list
    ListAddBack (blocks, (void*) cmd, 0);
    return tok;
}

static ListHead_t* parseInternal (ConfContext_t* ctx,
                                  ListHead_t* blocks,
                                  confToken_t** tokp,
                                  int ender)
{
    confToken_t* tok = *tokp;
    while (tok->type != ender)
    {
        // Figure out what this is
        if (tok->type == LEX_TOKEN_ID || tok->type == LEX_TOKEN_STR ||
            tok->type == LEX_TOKEN_DOLLAR)
        {
            // Parse a command
            tok = parseCmd (ctx, blocks, tok);
            if (!tok)
            {
                ListDestroy (blocks);
                return NULL;
            }
        }
        else if (tok->type == LEX_TOKEN_SET)
        {
            // Expect an identifier
            tok = parseExpect (ctx, tok, LEX_TOKEN_ID);
            if (!tok)
            {
                ListDestroy (blocks);
                return NULL;
            }
            ConfBlockSet_t* set = malloc (sizeof (ConfBlockSet_t));
            set->hdr.lineNo = tok->line;
            set->hdr.type = CONF_BLOCK_VARSET;
            set->var = StrRefNew (tok->semVal);
            // Next token can be string, id, literal or variable
            tok = parseToken (ctx, tok);
            if (tok->type == LEX_TOKEN_STR || tok->type == LEX_TOKEN_ID ||
                tok->type == LEX_TOKEN_LITERAL)
            {
                set->val.literal = StrRefNew (tok->semVal);
                set->val.type = CONF_STRING_LITERAL;
            }
            else if (tok->type == LEX_TOKEN_DOLLAR)
            {
                // Expect obrace
                tok = parseExpect (ctx, tok, LEX_TOKEN_OBRACE);
                if (!tok)
                {
                    ListDestroy (blocks);
                    return NULL;
                }
                // Expect identifier
                tok = parseExpect (ctx, tok, LEX_TOKEN_ID);
                if (!tok)
                {
                    ListDestroy (blocks);
                    return NULL;
                }
                // Copy name
                set->val.var = StrRefNew (tok->semVal);
                set->val.type = CONF_STRING_VAR;
                // Expect ebrace
                tok = parseExpect (ctx, tok, LEX_TOKEN_EBRACE);
                if (!tok)
                {
                    ListDestroy (blocks);
                    return NULL;
                }
            }
            ListAddBack (blocks, set, 0);
        }
        else if (tok->type == LEX_TOKEN_MENUENTRY && !ctx->insideMenu)
        {
            // Get menuentry name
            tok = parseExpect (ctx, tok, LEX_TOKEN_ID);
            if (!tok)
            {
                ListDestroy (blocks);
                return NULL;
            }
            ConfBlockMenu_t* block = malloc (sizeof (ConfBlockMenu_t));
            if (!block)
            {
                ListDestroy (blocks);
                return NULL;
            }
            block->hdr.lineNo = tok->line;
            block->hdr.type = CONF_BLOCK_MENUENTRY;
            block->name = StrRefNew (tok->semVal);
            tok = parseExpect (ctx, tok, LEX_TOKEN_OBRACE);
            // Parse inside
            ctx->insideMenu = true;
            tok = parseToken (ctx, tok);
            if (!tok)
            {
                StrRefDestroy (block->name);
                ListDestroy (blocks);
                return NULL;
            }
            ListHead_t* menuBlocks = ListCreate ("ConfBlock_t", false, 0);
            ListSetDestroy (menuBlocks, destroyBlock);
            if (!menuBlocks)
            {
                StrRefDestroy (block->name);
                ListDestroy (blocks);
                return NULL;
            }
            if (!parseInternal (ctx, menuBlocks, &tok, LEX_TOKEN_EBRACE))
            {
                StrRefDestroy (block->name);
                ListDestroy (blocks);
                return NULL;
            }
            block->blocks = (ListHead_t*) ListRef (menuBlocks);
            ctx->insideMenu = false;
            ListAddBack (blocks, block, 0);
        }
        else if (tok->type == LEX_TOKEN_NEWLINE)
        {
            ;
        }
        else
        {
            // Error
            parseError (ctx, tok, PARSE_ERROR_UNEXPECTED_TOKEN);
            ListDestroy (blocks);
            return NULL;
        }
        tok = parseToken (ctx, tok);
        if (!tok)
        {
            ListDestroy (blocks);
            return NULL;
        }
    }
    *tokp = tok;
    return blocks;
}

// Performs parsing
ListHead_t* NbConfParse (ConfContext_t* ctx)
{
    // Initialize lexer
    if (!confLexInit (ctx))
        return NULL;
    // Initialize parser structures
    ListHead_t* blocks = ListCreate ("ConfBlock_t", false, 0);
    ListSetDestroy (blocks, destroyBlock);
    ctx->blocks = blocks;
    // Begin lexing
    confToken_t* tok = parseToken (ctx, NULL);
    if (!tok)
    {
        ListDestroy (blocks);
        return NULL;
    }
    ListHead_t* res = parseInternal (ctx, blocks, &tok, LEX_TOKEN_EOF);
    // Free state
    confLexDestroy (ctx);
    if (res)
    {
        free (tok);
        if (ctx->lastToken)
            free (ctx->lastToken);
    }
    return res;
}
