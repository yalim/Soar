#include "portability.h"

/*************************************************************************
 * PLEASE SEE THE FILE "license.txt" (INCLUDED WITH THIS SOFTWARE PACKAGE)
 * FOR LICENSE AND COPYRIGHT INFORMATION.
 *************************************************************************/

/*************************************************************************
 *
 *  file:  lexer.cpp
 *
 * =======================================================================
 *
 *                              lexer.c
 *
 *  The lexer reads files and returns a stream of lexemes.  Get_lexeme() is
 *  the main routine; it looks for the next lexeme in the input, and stores
 *  it in the global variable "lexeme".  See the structure definition below.
 *
 *  Restrictions:  the lexer cannot read individual input lines longer than
 *  MAX_LEXER_LINE_LENGTH characters.  Thus, a single lexeme can't be longer
 *  than that either.
 *
 *  Determine_possible_symbol_types_for_string() is a utility routine which
 *  figures out what kind(s) of symbol a given string could represent.
 *
 *  Print_location_of_most_recent_lexeme() is used to print an indication
 *  of where a parser error occurred.  It tries to print out the current
 *  source line with a pointer to where the error was detected.
 *
 *  Current_lexer_parentheses_level() returns the current level of parentheses
 *  nesting (0 means no open paren's have been encountered).
 *  Skip_ahead_to_balanced_parentheses() eats lexemes until the appropriate
 *  closing paren is found (0 means eat until back at the top level).
 *
 *  Set_lexer_allow_ids() tells the lexer whether to allow identifiers to
 *  be read.  If false, things that look like identifiers will be returned
 *  as STR_CONSTANT_LEXEME's instead.
 *
 *  BUGBUG There are still problems with Soar not being very friendly
 *  when users have typos in productions, particularly with mismatched
 *  braces and parens.  see also parser.c
 * =======================================================================
 */
/* ======================================================================
                             lexer.c
   ====================================================================== */

#include <stdlib.h>

#include "lexer.h"
#include "mem.h"
#include "kernel.h"
#include "agent.h"
#include "print.h"
#include "init_soar.h"
#include "xml.h"

#include <math.h>
#include <ctype.h>

#include <assert.h>

//forward declaration for internal use
void consume_whitespace_and_comments(agent* thisAgent);

extern void dprint_current_lexeme(TraceMode mode);

//
// These three should be safe for re-entrancy.  --JNW--
//
bool constituent_char[256];   /* is the character a symbol constituent? */
bool whitespace[256];         /* is the character whitespace? */
bool number_starters[256];    /* could the character initiate a number? */

/* ======================================================================
                             Get next char

  Get_next_char() gets the next character from the current input file and
  puts it into the agent variable current_char.
====================================================================== */

void get_next_char(agent* thisAgent)
{
    char* s;

    if(thisAgent->current_char == EOF)
    {
        return;
    }
    if (thisAgent->lexer_input_string == NULL)
    {
        thisAgent->current_char = EOF;
        return;
    }
    thisAgent->current_char = *thisAgent->lexer_input_string++;
    if (thisAgent->current_char == '\0')
    {
        thisAgent->lexer_input_string = NIL;
        thisAgent->current_char = EOF;
        return;
    }
 }

/* ======================================================================

                         Lexer Utility Routines

====================================================================== */

inline void record_position_of_start_of_lexeme(agent* thisAgent)
{
    // TODO: rewrite this, since the lexer no longer keeps track of files
    // thisAgent->current_file->column_of_start_of_last_lexeme =
    //     thisAgent->current_file->current_column - 1;
    // thisAgent->current_file->line_of_start_of_last_lexeme =
    //     thisAgent->current_file->current_line;
}

inline void store_and_advance(agent* thisAgent)
{
    thisAgent->lexeme.string[thisAgent->lexeme.length++] = char(thisAgent->current_char);
    get_next_char(thisAgent);
}

/*#define finish() { thisAgent->lexeme.string[thisAgent->lexeme.length]=0; }*/
inline void finish(agent* thisAgent)
{
    thisAgent->lexeme.string[thisAgent->lexeme.length] = 0;
}

void read_constituent_string(agent* thisAgent)
{

    while ((thisAgent->current_char != EOF) &&
            constituent_char[static_cast<unsigned char>(thisAgent->current_char)])
    {
        store_and_advance(thisAgent);
    }
    finish(thisAgent);
}

void read_rest_of_floating_point_number(agent* thisAgent)
{
    /* --- at entry, current_char=="."; we read the "." and rest of number --- */
    store_and_advance(thisAgent);
    while (isdigit(thisAgent->current_char))
    {
        store_and_advance(thisAgent);    /* string of digits */
    }
    if ((thisAgent->current_char == 'e') || (thisAgent->current_char == 'E'))
    {
        store_and_advance(thisAgent);                             /* E */
        if ((thisAgent->current_char == '+') || (thisAgent->current_char == '-'))
        {
            store_and_advance(thisAgent);    /* optional leading + or - */
        }
        while (isdigit(thisAgent->current_char))
        {
            store_and_advance(thisAgent);    /* string of digits */
        }
    }
    finish(thisAgent);
}

bool determine_type_of_constituent_string(agent* thisAgent)
{
    bool possible_id, possible_var, possible_sc, possible_ic, possible_fc;
    bool rereadable;

    determine_possible_symbol_types_for_string(thisAgent->lexeme.string,
            thisAgent->lexeme.length,
            &possible_id,
            &possible_var,
            &possible_sc,
            &possible_ic,
            &possible_fc,
            &rereadable);

    if (possible_var)
    {
        thisAgent->lexeme.type = VARIABLE_LEXEME;
        return true;
    }

    if (possible_ic)
    {
        errno = 0;
        thisAgent->lexeme.type = INT_CONSTANT_LEXEME;
        thisAgent->lexeme.int_val = strtol(thisAgent->lexeme.string, NULL, 10);
        if (errno)
        {
            print(thisAgent,  "Error: bad integer (probably too large)\n");
            print_location_of_most_recent_lexeme(thisAgent);
            thisAgent->lexeme.int_val = 0;
        }
        return (errno == 0);
    }

    if (possible_fc)
    {
        errno = 0;
        thisAgent->lexeme.type = FLOAT_CONSTANT_LEXEME;
        thisAgent->lexeme.float_val = strtod(thisAgent->lexeme.string, NULL);
        if (errno)
        {
            print(thisAgent,  "Error: bad floating point number\n");
            print_location_of_most_recent_lexeme(thisAgent);
            thisAgent->lexeme.float_val = 0.0;
        }
        return (errno == 0);
    }

    if (thisAgent->allow_ids && possible_id)
    {
        // long term identifiers start with @
        unsigned lti_index = 0;
        if (thisAgent->lexeme.string[lti_index] == '@')
        {
            lti_index += 1;
        }
        thisAgent->lexeme.id_letter = static_cast<char>(toupper(thisAgent->lexeme.string[lti_index]));
        lti_index += 1;
        errno = 0;
        thisAgent->lexeme.type = IDENTIFIER_LEXEME;
        if (!from_c_string(thisAgent->lexeme.id_number, &(thisAgent->lexeme.string[lti_index])))
        {
            print(thisAgent,  "Error: bad number for identifier (probably too large)\n");
            print_location_of_most_recent_lexeme(thisAgent);
            thisAgent->lexeme.id_number = 0;
            errno = 1;
        }
        return (errno == 0);
    }

    if (possible_sc)
    {
        thisAgent->lexeme.type = STR_CONSTANT_LEXEME;
        if (thisAgent->sysparams[PRINT_WARNINGS_SYSPARAM])
        {
            if ((thisAgent->lexeme.string[0] == '<') ||
                    (thisAgent->lexeme.string[thisAgent->lexeme.length - 1] == '>'))
            {
                print(thisAgent,  "Warning: Suspicious string constant \"%s\"\n", thisAgent->lexeme.string);
                print_location_of_most_recent_lexeme(thisAgent);
                xml_generate_warning(thisAgent, "Warning: Suspicious string constant");
            }
        }
        return true;
    }

    thisAgent->lexeme.type = QUOTED_STRING_LEXEME;
    return true;
}

/* ======================================================================
                        Lex such-and-such Routines

  These routines are called from get_lexeme().  Which routine gets called
  depends on the first character of the new lexeme being read.  Each routine's
  job is to finish reading the lexeme and store the necessary items in
  the agent variable "lexeme".
====================================================================== */

void lex_unknown(agent* thisAgent);
#define lu lex_unknown

//
// This should be safe for re-entrant code. --JNW--
//
void (*(lexer_routines[256]))(agent*) =
{
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
    lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu, lu,
};

void lex_eof(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = EOF_LEXEME;
}

void lex_at(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = AT_LEXEME;
}

void lex_tilde(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = TILDE_LEXEME;
}

void lex_up_arrow(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = UP_ARROW_LEXEME;
}

void lex_lbrace(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = L_BRACE_LEXEME;
}

void lex_rbrace(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = R_BRACE_LEXEME;
}

void lex_exclamation_point(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = EXCLAMATION_POINT_LEXEME;
}

void lex_comma(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = COMMA_LEXEME;
}

void lex_equal(agent* thisAgent)
{
    /* Lexeme might be "=", or symbol */
    /* Note: this routine relies on = being a constituent character */

    read_constituent_string(thisAgent);
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = EQUAL_LEXEME;
        return;
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_ampersand(agent* thisAgent)
{
    /* Lexeme might be "&", or symbol */
    /* Note: this routine relies on & being a constituent character */

    read_constituent_string(thisAgent);
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = AMPERSAND_LEXEME;
        return;
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_lparen(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = L_PAREN_LEXEME;
    thisAgent->parentheses_level++;
}

void lex_rparen(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    thisAgent->lexeme.type = R_PAREN_LEXEME;
    if (thisAgent->parentheses_level > 0)
    {
        thisAgent->parentheses_level--;
    }
}

void lex_greater(agent* thisAgent)
{
    /* Lexeme might be ">", ">=", ">>", or symbol */
    /* Note: this routine relies on =,> being constituent characters */

    read_constituent_string(thisAgent);
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = GREATER_LEXEME;
        return;
    }
    if (thisAgent->lexeme.length == 2)
    {
        if (thisAgent->lexeme.string[1] == '>')
        {
            thisAgent->lexeme.type = GREATER_GREATER_LEXEME;
            return;
        }
        if (thisAgent->lexeme.string[1] == '=')
        {
            thisAgent->lexeme.type = GREATER_EQUAL_LEXEME;
            return;
        }
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_less(agent* thisAgent)
{
    /* Lexeme might be "<", "<=", "<=>", "<>", "<<", or variable */
    /* Note: this routine relies on =,<,> being constituent characters */

    read_constituent_string(thisAgent);
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = LESS_LEXEME;
        return;
    }
    if (thisAgent->lexeme.length == 2)
    {
        if (thisAgent->lexeme.string[1] == '>')
        {
            thisAgent->lexeme.type = NOT_EQUAL_LEXEME;
            return;
        }
        if (thisAgent->lexeme.string[1] == '=')
        {
            thisAgent->lexeme.type = LESS_EQUAL_LEXEME;
            return;
        }
        if (thisAgent->lexeme.string[1] == '<')
        {
            thisAgent->lexeme.type = LESS_LESS_LEXEME;
            return;
        }
    }
    if (thisAgent->lexeme.length == 3)
    {
        if ((thisAgent->lexeme.string[1] == '=') && (thisAgent->lexeme.string[2] == '>'))
        {
            thisAgent->lexeme.type = LESS_EQUAL_GREATER_LEXEME;
            return;
        }
    }
    determine_type_of_constituent_string(thisAgent);

}

void lex_period(agent* thisAgent)
{
    store_and_advance(thisAgent);
    finish(thisAgent);
    /* --- if we stopped at '.', it might be a floating-point number, so be
       careful to check for this case --- */
    if (isdigit(thisAgent->current_char))
    {
        read_rest_of_floating_point_number(thisAgent);
    }
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = PERIOD_LEXEME;
        return;
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_plus(agent* thisAgent)
{
    /* Lexeme might be +, number, or symbol */
    /* Note: this routine relies on various things being constituent chars */
    int i;
    bool could_be_floating_point;

    read_constituent_string(thisAgent);
    /* --- if we stopped at '.', it might be a floating-point number, so be
       careful to check for this case --- */
    if (thisAgent->current_char == '.')
    {
        could_be_floating_point = true;
        for (i = 1; i < thisAgent->lexeme.length; i++)
            if (! isdigit(thisAgent->lexeme.string[i]))
            {
                could_be_floating_point = false;
            }
        if (could_be_floating_point)
        {
            read_rest_of_floating_point_number(thisAgent);
        }
    }
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = PLUS_LEXEME;
        return;
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_minus(agent* thisAgent)
{
    /* Lexeme might be -, -->, number, or symbol */
    /* Note: this routine relies on various things being constituent chars */
    int i;
    bool could_be_floating_point;

    read_constituent_string(thisAgent);
    /* --- if we stopped at '.', it might be a floating-point number, so be
       careful to check for this case --- */
    if (thisAgent->current_char == '.')
    {
        could_be_floating_point = true;
        for (i = 1; i < thisAgent->lexeme.length; i++)
            if (! isdigit(thisAgent->lexeme.string[i]))
            {
                could_be_floating_point = false;
            }
        if (could_be_floating_point)
        {
            read_rest_of_floating_point_number(thisAgent);
        }
    }
    if (thisAgent->lexeme.length == 1)
    {
        thisAgent->lexeme.type = MINUS_LEXEME;
        return;
    }
    if (thisAgent->lexeme.length == 3)
    {
        if ((thisAgent->lexeme.string[1] == '-') && (thisAgent->lexeme.string[2] == '>'))
        {
            thisAgent->lexeme.type = RIGHT_ARROW_LEXEME;
            return;
        }
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_digit(agent* thisAgent)
{
    int i;
    bool could_be_floating_point;

    read_constituent_string(thisAgent);
    /* --- if we stopped at '.', it might be a floating-point number, so be
       careful to check for this case --- */
    if (thisAgent->current_char == '.')
    {
        could_be_floating_point = true;
        for (i = 1; i < thisAgent->lexeme.length; i++)
            if (! isdigit(thisAgent->lexeme.string[i]))
            {
                could_be_floating_point = false;
            }
        if (could_be_floating_point)
        {
            read_rest_of_floating_point_number(thisAgent);
        }
    }
    determine_type_of_constituent_string(thisAgent);
}

void lex_unknown(agent* thisAgent)
{
    get_next_char(thisAgent);
    get_lexeme(thisAgent);
}

void lex_constituent_string(agent* thisAgent)
{
    read_constituent_string(thisAgent);
    determine_type_of_constituent_string(thisAgent);
}

void lex_vbar(agent* thisAgent)
{
    thisAgent->lexeme.type = STR_CONSTANT_LEXEME;
    get_next_char(thisAgent);
    do
    {
        if ((thisAgent->current_char == EOF) ||
                (thisAgent->lexeme.length == MAX_LEXEME_LENGTH))
        {
            print(thisAgent,  "Error:  opening '|' without closing '|'\n");
            print_location_of_most_recent_lexeme(thisAgent);
            /* BUGBUG if reading from top level, don't want to signal EOF */
            thisAgent->lexeme.type = EOF_LEXEME;
            thisAgent->lexeme.string[0] = EOF;
            thisAgent->lexeme.string[1] = 0;
            thisAgent->lexeme.length = 1;
            return;
        }
        if (thisAgent->current_char == '\\')
        {
            get_next_char(thisAgent);
            thisAgent->lexeme.string[thisAgent->lexeme.length++] = char(thisAgent->current_char);
            get_next_char(thisAgent);
        }
        else if (thisAgent->current_char == '|')
        {
            get_next_char(thisAgent);
            break;
        }
        else
        {
            thisAgent->lexeme.string[thisAgent->lexeme.length++] = char(thisAgent->current_char);
            get_next_char(thisAgent);
        }
    }
    while (true);
    thisAgent->lexeme.string[thisAgent->lexeme.length] = 0;
}

void lex_quote(agent* thisAgent)
{
    thisAgent->lexeme.type = QUOTED_STRING_LEXEME;
    get_next_char(thisAgent);
    do
    {
        if ((thisAgent->current_char == EOF) || (thisAgent->lexeme.length == MAX_LEXEME_LENGTH))
        {
            print(thisAgent,  "Error:  opening '\"' without closing '\"'\n");
            print_location_of_most_recent_lexeme(thisAgent);
            /* BUGBUG if reading from top level, don't want to signal EOF */
            thisAgent->lexeme.type = EOF_LEXEME;
            thisAgent->lexeme.string[0] = 0;
            thisAgent->lexeme.length = 1;
            return;
        }
        if (thisAgent->current_char == '\\')
        {
            get_next_char(thisAgent);
            thisAgent->lexeme.string[thisAgent->lexeme.length++] = char(thisAgent->current_char);
            get_next_char(thisAgent);
        }
        else if (thisAgent->current_char == '"')
        {
            get_next_char(thisAgent);
            break;
        }
        else
        {
            thisAgent->lexeme.string[thisAgent->lexeme.length++] = char(thisAgent->current_char);
            get_next_char(thisAgent);
        }
    }
    while (true);
    thisAgent->lexeme.string[thisAgent->lexeme.length] = 0;
}

/* ======================================================================
                             Get lexeme

  This is the main routine called from outside the lexer.  It reads past
  any whitespace, then calls some lex_xxx routine (using the lexer_routines[]
  table) based on the first character of the lexeme.
====================================================================== */

void get_lexeme(agent* thisAgent)
{
    thisAgent->lexeme.length = 0;
    thisAgent->lexeme.string[0] = 0;
    consume_whitespace_and_comments(thisAgent);

    // dispatch to lexer routine by first character in lexeme
    record_position_of_start_of_lexeme(thisAgent);
    if (thisAgent->current_char!=EOF)
    {
        (*(lexer_routines[static_cast<unsigned char>(thisAgent->current_char)]))(thisAgent);
    }
    else
    {
        lex_eof(thisAgent);
    }

    dprint(DT_PARSER, "Parser| get_lexeme read ");
    dprint_current_lexeme(DT_PARSER);
}

void consume_whitespace_and_comments(agent* thisAgent)
{
    // loop until whitespace and comments are gone
    while (true)
    {
        if (thisAgent->current_char == EOF)
        {
            break;
        }
        if (whitespace[static_cast<unsigned char>(thisAgent->current_char)])
        {
            get_next_char(thisAgent);
            continue;
        }

        // skip the semi-colon, forces newline in TCL
        if (thisAgent->current_char == ';')
        {
            get_next_char(thisAgent);
            continue;
        }
        // hash is end-of-line comment; read to the end
        if (thisAgent->current_char == '#')
        {
            while ((thisAgent->current_char != '\n') &&
                    (thisAgent->current_char != EOF))
            {
                get_next_char(thisAgent);
            }
            if (thisAgent->current_char != EOF)
            {
                get_next_char(thisAgent);
            }
            continue;
        }
        // if no whitespace or comments found,
        // break out of the loop
        break;
    }
}

/* ======================================================================
                            Init lexer

  This should be called before anything else in this file.  It does all
  the necessary init stuff for the lexer, and starts the lexer reading from
  standard input.
====================================================================== */

// TODO: This file badly need to be locked.
// TODO: Does it still need locking even though memory allocation was removed?
void init_lexer(agent* thisAgent)
{
    static bool initialized = false;

    if (!initialized)
    {
        initialized = true;

        unsigned int i;

        /* --- setup constituent_char array --- */
        char extra_constituents[] = "$%&*+-/:<=>?_@";
        for (i = 0; i < 256; i++)
        {
            //
            // When i == 1, strchr returns true based on the terminating
            // character.  This is not the intent, so we exclude that case
            // here.
            //
            if ((strchr(extra_constituents, i) != 0) && i != 0)
            {
                constituent_char[i] = true;
            }
            else
            {
                constituent_char[i] = (isalnum(i) != 0);
            }
        }

        //  for (i=0; i<strlen(extra_constituents); i++)
        //  {
        //    constituent_char[(int)extra_constituents[i]]=true;
        //  }

        /* --- setup whitespace array --- */
        for (i = 0; i < 256; i++)
        {
            whitespace[i] = (isspace(i) != 0);
        }

        /* --- setup number_starters array --- */
        for (i = 0; i < 256; i++)
        {
            switch (i)
            {
                case '+':
                    number_starters[(int)'+'] = true;
                    break;
                case '-':
                    number_starters[(int)'-'] = true;
                    break;
                case '.':
                    number_starters[(int)'.'] = true;
                    break;
                default:
                    number_starters[i] = (isdigit(i) != 0);
            }
        }

        /* --- setup lexer_routines array --- */
        //
        // I go to some effort here to insure that values do not
        // get overwritten.  That could cause problems in a multi-
        // threaded sense because values could get switched to one
        // value and then another.  If a value is only ever set to
        // one thing, resetting it to the same thing should be
        // perfectly safe.
        //
        for (i = 0; i < 256; i++)
        {
            switch (i)
            {
                case '@':
                    lexer_routines[(int)'@'] = lex_at;
                    break;
                case '(':
                    lexer_routines[(int)'('] = lex_lparen;
                    break;
                case ')':
                    lexer_routines[(int)')'] = lex_rparen;
                    break;
                case '+':
                    lexer_routines[(int)'+'] = lex_plus;
                    break;
                case '-':
                    lexer_routines[(int)'-'] = lex_minus;
                    break;
                case '~':
                    lexer_routines[(int)'~'] = lex_tilde;
                    break;
                case '^':
                    lexer_routines[(int)'^'] = lex_up_arrow;
                    break;
                case '{':
                    lexer_routines[(int)'{'] = lex_lbrace;
                    break;
                case '}':
                    lexer_routines[(int)'}'] = lex_rbrace;
                    break;
                case '!':
                    lexer_routines[(int)'!'] = lex_exclamation_point;
                    break;
                case '>':
                    lexer_routines[(int)'>'] = lex_greater;
                    break;
                case '<':
                    lexer_routines[(int)'<'] = lex_less;
                    break;
                case '=':
                    lexer_routines[(int)'='] = lex_equal;
                    break;
                case '&':
                    lexer_routines[(int)'&'] = lex_ampersand;
                    break;
                case '|':
                    lexer_routines[(int)'|'] = lex_vbar;
                    break;
                case ',':
                    lexer_routines[(int)','] = lex_comma;
                    break;
                case '.':
                    lexer_routines[(int)'.'] = lex_period;
                    break;
                case '"':
                    lexer_routines[(int)'"'] = lex_quote;
                    break;
                default:
                    if (isdigit(i))
                    {
                        lexer_routines[i] = lex_digit;
                        continue;
                    }

                    if (constituent_char[i])
                    {
                        lexer_routines[i] = lex_constituent_string;
                        continue;
                    }
            }
        }
    }
}

/* ======================================================================
                   Print location of most recent lexeme

  This routine is used to print an indication of where a parser or interface
  command error occurred.  It tries to print out the current source line
  with a pointer to where the error was detected.  If the current source
  line is no longer available, it just prints out the line number instead.

  BUGBUG: if the input line contains any tabs, the pointer comes out in
  the wrong place.
====================================================================== */

void print_location_of_most_recent_lexeme(agent* thisAgent)
{
    //TODO: below was commented out because file input isn't used anymore.
    //write something else to track input line, column and offset

    // int i;

    // if (thisAgent->current_file->line_of_start_of_last_lexeme ==
    //         thisAgent->current_file->current_line)
    // {
    //     /* --- error occurred on current line, so print out the line --- */
    //     if (thisAgent->current_file->buffer[strlen(thisAgent->current_file->buffer) - 1] == '\n')
    //     {
    //         print_string(thisAgent, thisAgent->current_file->buffer);
    //     }
    //     else
    //     {
    //         print(thisAgent,  "%s\n", thisAgent->current_file->buffer);
    //     }
    //     for (i = 0; i < thisAgent->current_file->column_of_start_of_last_lexeme; i++)
    //     {
    //         print_string(thisAgent, "-");
    //     }
    //     print_string(thisAgent, "^\n");
    // }
    // else
    // {
    //     /* --- error occurred on a previous line, so just give the position --- */
    //     print(thisAgent,  "File %s, line %lu, column %lu.\n", thisAgent->current_file->filename,
    //           thisAgent->current_file->line_of_start_of_last_lexeme,
    //           thisAgent->current_file->column_of_start_of_last_lexeme + 1);
    // }
}

/* ======================================================================
                       Parentheses Utilities

  Current_lexer_parentheses_level() returns the current level of parentheses
  nesting (0 means no open paren's have been encountered).

  Skip_ahead_to_balanced_parentheses() eats lexemes until the appropriate
  closing paren is found (0 means eat until back at the top level).

====================================================================== */

int current_lexer_parentheses_level(agent* thisAgent)
{
    return thisAgent->parentheses_level;
}

void skip_ahead_to_balanced_parentheses(agent* thisAgent,
                                        int parentheses_level)
{
    while (true)
    {
        if (thisAgent->lexeme.type == EOF_LEXEME)
        {
            return;
        }
        if ((thisAgent->lexeme.type == R_PAREN_LEXEME) &&
                (parentheses_level == thisAgent->parentheses_level))
        {
            return;
        }
        get_lexeme(thisAgent);
    }
}

/* ======================================================================
                        Set lexer allow ids

  This routine should be called to tell the lexer whether to allow
  identifiers to be read.  If false, things that look like identifiers
  will be returned as STR_CONSTANT_LEXEME's instead.
====================================================================== */

void set_lexer_allow_ids(agent* thisAgent, bool allow_identifiers)
{
    thisAgent->allow_ids = allow_identifiers;
}

bool get_lexer_allow_ids(agent* thisAgent)
{
    return thisAgent->allow_ids;
}

/* ======================================================================
               Determine possible symbol types for string

  This is a utility routine which figures out what kind(s) of symbol a
  given string could represent.  At entry:  s, length_of_s represent the
  string.  At exit:  possible_xxx is set to true/false to indicate
  whether the given string could represent that kind of symbol; rereadable
  is set to true indicating whether the lexer would read the given string
  as a symbol with exactly the same name (as opposed to treating it as a
  special lexeme like "+", changing upper to lower case, etc.
====================================================================== */

void determine_possible_symbol_types_for_string(const char* s,
        size_t length_of_s,
        bool* possible_id,
        bool* possible_var,
        bool* possible_sc,
        bool* possible_ic,
        bool* possible_fc,
        bool* rereadable)
{
    const char* ch;
    bool all_alphanum;

    *possible_id = false;
    *possible_var = false;
    *possible_sc = false;
    *possible_ic = false;
    *possible_fc = false;
    *rereadable = false;

    /* --- check if it's an integer or floating point number --- */
    if (number_starters[static_cast<unsigned char>(*s)])
    {
        ch = s;
        if ((*ch == '+') || (*ch == '-'))
        {
            ch++;    /* optional leading + or - */
        }
        while (isdigit(*ch))
        {
            ch++;    /* string of digits */
        }
        if ((*ch == 0) && (isdigit(*(ch - 1))))
        {
            *possible_ic = true;
        }
        if (*ch == '.')
        {
            ch++;                               /* decimal point */
            while (isdigit(*ch))
            {
                ch++;    /* string of digits */
            }
            if ((*ch == 'e') || (*ch == 'E'))
            {
                ch++;                           /* E */
                if ((*ch == '+') || (*ch == '-'))
                {
                    ch++;    /* optional leading + or - */
                }
                while (isdigit(*ch))
                {
                    ch++;    /* string of digits */
                }
            }
            if (*ch == 0)
            {
                *possible_fc = true;
            }
        }
    }

    /* --- make sure it's entirely constituent characters --- */
    for (ch = s; *ch != 0; ch++)
        if (! constituent_char[static_cast<unsigned char>(*ch)])
        {
            return;
        }

    /* --- check for rereadability --- */
    all_alphanum = true;
    for (ch = s; *ch != '\0'; ch++)
    {
        if (!isalnum(*ch))
        {
            all_alphanum = false;
            break;
        }
    }
    if (all_alphanum ||
            (length_of_s > LENGTH_OF_LONGEST_SPECIAL_LEXEME) ||
            ((length_of_s == 1) && (*s == '*')))
    {
        *rereadable = true;
    }

    /* --- any string of constituents could be a sym constant --- */
    *possible_sc = true;

    /* --- check whether it's a variable --- */
    if ((*s == '<') && (*(s + length_of_s - 1) == '>'))
    {
        *possible_var = true;
    }

    /* --- check if it's an identifier --- */
    // long term identifiers start with @
    if (*s == '@')
    {
        ch = s + 1;
    }
    else
    {
        ch = s;
    }
    if (isalpha(*ch) && *(++ch) != '\0')
    {
        /* --- is the rest of the string an integer? --- */
        while (isdigit(*ch))
        {
            ch++;
        }
        if (*ch == '\0')
        {
            *possible_id = true;
        }
    }
}

