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

        // Shore smoothing parameters
        api["setShoreWaveAttenuation"] = [](FiniteFloat attenuation) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanShoreWaveAttenuation(attenuation);
            }
        };

        api["getShoreWaveAttenuation"] = []() -> float {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                return rendering->getWater()->getOceanShoreWaveAttenuation();
            }
            return 0.8f; // default
        };

        api["setShoreDepthScale"] = [](FiniteFloat scale) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanShoreDepthScale(scale);
            }
        };

        api["getShoreDepthScale"] = []() -> float {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                return rendering->getWater()->getOceanShoreDepthScale();
            }
            return 500.0f; // default
        };

        api["setShoreFoamBoost"] = [](FiniteFloat boost) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanShoreFoamBoost(boost);
            }
        };

        api["getShoreFoamBoost"] = []() -> float {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                return rendering->getWater()->getOceanShoreFoamBoost();
            }
            return 1.5f; // default
        };

        // Vertex displacement smoothing (manual global control)
        api["setVertexShoreSmoothing"] = [](FiniteFloat smoothing) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanVertexShoreSmoothing(smoothing);
            }
        };

        api["getVertexShoreSmoothing"] = []() -> float {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                return rendering->getWater()->getOceanVertexShoreSmoothing();
            }
            return 0.0f; // default
        };

        // Debug visualization
        api["setDebugShore"] = [](bool enabled) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setOceanDebugShore(enabled);
            }
        };

        // Shore distance map generation
        // Usage: ocean.generateShoreMap(-500000, -500000, 500000, 500000)
        // Bounds are in MW world units. For Vvardenfell, try: (-400000, -400000, 400000, 400000)
        api["generateShoreMap"] = [](FiniteFloat minX, FiniteFloat minY, FiniteFloat maxX, FiniteFloat maxY) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->generateShoreDistanceMap(minX, minY, maxX, maxY);
            }
        };

        // Set max shore distance before generating the map
        // Default is 2000 MW units (~28 meters). Increase for wider shore calming zone.
        // Must be called BEFORE generateShoreMap to take effect.
        api["setShoreMapMaxDistance"] = [](FiniteFloat distance) {
            MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
            if (rendering && rendering->getWater())
            {
                rendering->getWater()->setShoreMapMaxDistance(distance);
            }
        };

        return LuaUtil::makeReadOnly(api);
    }
}
