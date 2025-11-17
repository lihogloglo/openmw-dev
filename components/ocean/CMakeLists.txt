set(OCEAN_HEADER
    watertype.hpp
    watersubdivider.hpp
    watersubdivisiontracker.hpp
    oceanfftsimulation.hpp
)

set(OCEAN_SOURCE
    watersubdivider.cpp
    watersubdivisiontracker.cpp
    oceanfftsimulation.cpp
)

add_library(components_ocean OBJECT ${OCEAN_SOURCE} ${OCEAN_HEADER})
target_compile_definitions(components_ocean PRIVATE OPENMW_DOC)

if (BUILD_WITH_CODE_COVERAGE)
    target_compile_options(components_ocean PRIVATE --coverage)
    target_link_libraries(components_ocean PRIVATE gcov)
endif()
