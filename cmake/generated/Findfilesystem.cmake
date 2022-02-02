# AUTOGENERATED, DON'T CHANGE THIS FILE!


if (TARGET filesystem)
  if (NOT filesystem_FIND_VERSION)
      set(filesystem_FOUND ON)
      return()
  endif()

  if (filesystem_VERSION)
      if (filesystem_FIND_VERSION VERSION_LESS_EQUAL filesystem_VERSION)
          set(filesystem_FOUND ON)
          return()
      else()
          message(FATAL_ERROR
              "Already using version ${filesystem_VERSION} "
              "of filesystem when version ${filesystem_FIND_VERSION} "
              "was requested."
          )
      endif()
  endif()
endif()

set(FULL_ERROR_MESSAGE "Could not find `filesystem` package.\n\tDebian: sudo apt update && sudo apt install libboost-filesystem-dev\n\tMacOS: brew install boost")


include(FindPackageHandleStandardArgs)

find_library(filesystem_LIBRARIES_boost_filesystem
  NAMES boost_filesystem
)
list(APPEND filesystem_LIBRARIES ${filesystem_LIBRARIES_boost_filesystem})

find_path(filesystem_INCLUDE_DIRS_boost_filesystem_config_hpp
  NAMES boost/filesystem/config.hpp
)
list(APPEND filesystem_INCLUDE_DIRS ${filesystem_INCLUDE_DIRS_boost_filesystem_config_hpp})



if (filesystem_VERSION)
  set(filesystem_VERSION ${filesystem_VERSION})
endif()

if (filesystem_FIND_VERSION AND NOT filesystem_VERSION)
  include(DetectVersion)

  if (UNIX AND NOT APPLE)
    deb_version(filesystem_VERSION libboost-filesystem-dev)
  endif()
  if (APPLE)
    brew_version(filesystem_VERSION boost)
  endif()
endif()

 
find_package_handle_standard_args(
  filesystem
    REQUIRED_VARS
      filesystem_LIBRARIES
      filesystem_INCLUDE_DIRS
      
    FAIL_MESSAGE
      "${FULL_ERROR_MESSAGE}"
)
mark_as_advanced(
  filesystem_LIBRARIES
  filesystem_INCLUDE_DIRS
  
)

if (NOT filesystem_FOUND)
  if (filesystem_FIND_REQUIRED)
      message(FATAL_ERROR "${FULL_ERROR_MESSAGE}. Required version is at least ${filesystem_FIND_VERSION}")
  endif()

  return()
endif()

if (filesystem_FIND_VERSION)
  if (filesystem_VERSION VERSION_LESS filesystem_FIND_VERSION)
      message(STATUS
          "Version of filesystem is '${filesystem_VERSION}'. "
          "Required version is at least '${filesystem_FIND_VERSION}'. "
          "Ignoring found filesystem."
      )
      set(filesystem_FOUND OFF)
      return()
  endif()
endif()

 
if (NOT TARGET filesystem)
  add_library(filesystem INTERFACE IMPORTED GLOBAL)

  if (TARGET Boost::filesystem)
    target_link_libraries(filesystem INTERFACE Boost::filesystem)
  endif()
  target_include_directories(filesystem INTERFACE ${filesystem_INCLUDE_DIRS})
  target_link_libraries(filesystem INTERFACE ${filesystem_LIBRARIES})
  
  # Target filesystem is created
endif()

if (filesystem_VERSION)
  set(filesystem_VERSION "${filesystem_VERSION}" CACHE STRING "Version of the filesystem")
endif()