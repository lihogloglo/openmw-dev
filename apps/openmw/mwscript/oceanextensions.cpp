#include "oceanextensions.hpp"

#include <components/compiler/opcodes.hpp>
#include <components/interpreter/interpreter.hpp>
#include <components/interpreter/opcodes.hpp>
#include <components/interpreter/runtime.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwrender/water.hpp"

namespace MWScript
{
    namespace Ocean
    {
        // Set ocean water color (R, G, B values 0.0-1.0)
        class OpSetOceanWaterColor : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float b = runtime[0].mFloat;
                runtime.pop();
                float g = runtime[0].mFloat;
                runtime.pop();
                float r = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanWaterColor(osg::Vec3f(r, g, b));
                }
            }
        };

        // Set ocean foam color (R, G, B values 0.0-1.0)
        class OpSetOceanFoamColor : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float b = runtime[0].mFloat;
                runtime.pop();
                float g = runtime[0].mFloat;
                runtime.pop();
                float r = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanFoamColor(osg::Vec3f(r, g, b));
                }
            }
        };

        // Set ocean wind speed (meters per second)
        class OpSetOceanWindSpeed : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float speed = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanWindSpeed(speed);
                }
            }
        };

        // Set ocean wind direction (degrees)
        class OpSetOceanWindDirection : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float direction = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanWindDirection(direction);
                }
            }
        };

        // Set ocean fetch length (meters)
        class OpSetOceanFetchLength : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float length = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanFetchLength(length);
                }
            }
        };

        // Set ocean swell (0.0-2.0)
        class OpSetOceanSwell : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float swell = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanSwell(swell);
                }
            }
        };

        // Set ocean detail (0.0-1.0)
        class OpSetOceanDetail : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float detail = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanDetail(detail);
                }
            }
        };

        // Set ocean spread (0.0-1.0)
        class OpSetOceanSpread : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float spread = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanSpread(spread);
                }
            }
        };

        // Set ocean foam amount (0.0-10.0)
        class OpSetOceanFoamAmount : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                float amount = runtime[0].mFloat;
                runtime.pop();

                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                if (rendering && rendering->getWater())
                {
                    rendering->getWater()->setOceanFoamAmount(amount);
                }
            }
        };

        // Get ocean wind speed
        class OpGetOceanWindSpeed : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                float speed = 20.0f; // default
                if (rendering && rendering->getWater())
                {
                    speed = rendering->getWater()->getOceanWindSpeed();
                }
                runtime.push(speed);
            }
        };

        // Get ocean wind direction
        class OpGetOceanWindDirection : public Interpreter::Opcode0
        {
        public:
            void execute(Interpreter::Runtime& runtime) override
            {
                MWRender::RenderingManager* rendering = MWBase::Environment::get().getWorld()->getRenderingManager();
                float direction = 0.0f; // default
                if (rendering && rendering->getWater())
                {
                    direction = rendering->getWater()->getOceanWindDirection();
                }
                runtime.push(direction);
            }
        };

        void installOpcodes(Interpreter::Interpreter& interpreter)
        {
            interpreter.installSegment5<OpSetOceanWaterColor>(Compiler::Ocean::opcodeSetOceanWaterColor);
            interpreter.installSegment5<OpSetOceanFoamColor>(Compiler::Ocean::opcodeSetOceanFoamColor);
            interpreter.installSegment5<OpSetOceanWindSpeed>(Compiler::Ocean::opcodeSetOceanWindSpeed);
            interpreter.installSegment5<OpSetOceanWindDirection>(Compiler::Ocean::opcodeSetOceanWindDirection);
            interpreter.installSegment5<OpSetOceanFetchLength>(Compiler::Ocean::opcodeSetOceanFetchLength);
            interpreter.installSegment5<OpSetOceanSwell>(Compiler::Ocean::opcodeSetOceanSwell);
            interpreter.installSegment5<OpSetOceanDetail>(Compiler::Ocean::opcodeSetOceanDetail);
            interpreter.installSegment5<OpSetOceanSpread>(Compiler::Ocean::opcodeSetOceanSpread);
            interpreter.installSegment5<OpSetOceanFoamAmount>(Compiler::Ocean::opcodeSetOceanFoamAmount);
            interpreter.installSegment5<OpGetOceanWindSpeed>(Compiler::Ocean::opcodeGetOceanWindSpeed);
            interpreter.installSegment5<OpGetOceanWindDirection>(Compiler::Ocean::opcodeGetOceanWindDirection);
        }
    }
}
