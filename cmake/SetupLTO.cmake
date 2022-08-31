if (CMAKE_BUILD_TYPE MATCHES "Debug")
    set(LTO_FLAG FALSE)
else()
    set(LTO_FLAG TRUE)
endif()

if (NOT USERVER_OPEN_SOURCE_BUILD)
    option(LTO "Use link time optimizations" ${LTO_FLAG})
    option(USERVER_LTO "Use link time optimizations" ${LTO})
else()
    option(USERVER_LTO "Use link time optimizations" ${LTO_FLAG})
endif()

if(NOT LTO_FLAG AND NOT USERVER_LTO)
    message(STATUS "LTO: disabled (local build)")
elseif(NOT USERVER_LTO)
    message(STATUS "LTO: disabled (user request)")
elseif (NOT CUSTOM_LD_OK AND NOT USERVER_OPEN_SOURCE_BUILD)
    message(STATUS "LTO: disabled (poorly set up linker)")
else()
    message(STATUS "LTO: on")
    include(RequireLTO)
endif()
