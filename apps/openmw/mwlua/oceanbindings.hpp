#ifndef MWLUA_OCEANBINDINGS_H
#define MWLUA_OCEANBINDINGS_H

#include <sol/forward.hpp>

namespace MWLua
{
    sol::table initOceanPackage(sol::state_view lua);
}

#endif
