# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\appCodexRemoteBootstrap_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\appCodexRemoteBootstrap_autogen.dir\\ParseCache.txt"
  "appCodexRemoteBootstrap_autogen"
  )
endif()
