#ifndef GAME_SCRIPT_OCEANEXTENSIONS_H
#define GAME_SCRIPT_OCEANEXTENSIONS_H

namespace Compiler
{
    class Extensions;
}

namespace Interpreter
{
    class Interpreter;
}

namespace MWScript
{
    namespace Ocean
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}

#endif
