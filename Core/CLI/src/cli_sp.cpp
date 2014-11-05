/////////////////////////////////////////////////////////////////
// sp command file.
//
// Author: Jonathan Voigt, voigtjr@gmail.com
// Date  : 2004
//
/////////////////////////////////////////////////////////////////

#include "portability.h"

#include "cli_CommandLineInterface.h"

#include "cli_Commands.h"
#include "sml_AgentSML.h"

#include "agent.h"
#include "production.h"
#include "symtab.h"
#include "rete.h"
#include "parser.h"

using namespace cli;

// FIXME: copied from gSKI
void setLexerInput(agent* ai_agent, const char*  ai_string)
{
    // Side effects:
    //    The soar agents alternate input values are updated and its
    //      current character is reset to a whitespace value.
    ai_agent->lexer_input_string = const_cast<char*>(ai_string);
    // whitespace forces immediate read of first line
    ai_agent->current_char = ' ';
    return;
}

bool CommandLineInterface::DoSP(const std::string& productionString)
{
    // Load the production
    agent* thisAgent = m_pAgentSML->GetSoarAgent();
    setLexerInput(thisAgent, productionString.c_str());
    set_lexer_allow_ids(thisAgent, false);
    get_lexeme(thisAgent);

    production* p;
    unsigned char rete_addition_result = 0;
    p = parse_production(thisAgent, &rete_addition_result);

    set_lexer_allow_ids(thisAgent, true);
    setLexerInput( thisAgent, NULL);

    if (!p)
    {
        // There was an error, but duplicate production is just a warning
        if (rete_addition_result != DUPLICATE_PRODUCTION)
        {
            return SetError("Production addition failed.");
        }
        // production ignored
        m_NumProductionsIgnored += 1;
    }
    else
    {
        if (!m_SourceFileStack.empty())
        {
            p->filename = make_memory_block_for_string(thisAgent, m_SourceFileStack.top().c_str());
        }

        // production was sourced
        m_NumProductionsSourced += 1;
        if (m_RawOutput)
        {
            m_Result << '*';
        }
    }
    return true;
}

