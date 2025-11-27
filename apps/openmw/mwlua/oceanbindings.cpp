#include "oceanbindings.hpp"

#include <components/lua/luastate.hpp>
#include <components/misc/finitevalues.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwrender/water.hpp"

namespace MWLua
{
    sol::table initOceanPackage(sol::state_view lua)
    {
        using Misc::FiniteFloat;

        sol::table api(lua, sol::create);

        // Wind parameters
        api["setWindSpeed"] = [](FiniteFloat speed) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanWindSpeed(speed);
            }
        };

        api["getWindSpeed"] = []() -> float {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                return rendering->getWater()->getOceanWindSpeed();
            }
            return 20.0f; // default
        };

        api["setWindDirection"] = [](FiniteFloat direction) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanWindDirection(direction);
            }
        };

        api["getWindDirection"] = []() -> float {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                return rendering->getWater()->getOceanWindDirection();
            }
            return 0.0f; // default
        };

        // Water appearance
        api["setWaterColor"] = [](float r, float g, float b) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanWaterColor(osg::Vec3f(r, g, b));
            }
        };

        api["setFoamColor"] = [](float r, float g, float b) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanFoamColor(osg::Vec3f(r, g, b));
            }
        };

        // Wave physics parameters
        api["setFetchLength"] = [](FiniteFloat length) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanFetchLength(length);
            }
        };

        api["setSwell"] = [](FiniteFloat swell) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanSwell(swell);
            }
        };

        api["setDetail"] = [](FiniteFloat detail) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanDetail(detail);
            }
        };

        api["setSpread"] = [](FiniteFloat spread) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanSpread(spread);
            }
        };

        api["setFoamAmount"] = [](FiniteFloat amount) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanFoamAmount(amount);
            }
        };

        // Lake Debug Mode
        api["setLakeDebugMode"] = [](int mode) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setLakeDebugMode(mode);
            }
        };

        return LuaUtil::makeReadOnly(api);
    }
}
