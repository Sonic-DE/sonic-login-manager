# - Try to find utmps and its dependencies
# Once done this will define
# UTMPS_FOUND - system has utmps and dependencies
# UTMPS_INCLUDE_DIR - the utmps include dir
# UTMPS_LIBRARIES - libutmps

find_path(UTMPS_INCLUDE_DIR NAMES utmps/utmpx.h)
find_library(UTMPS_LIBRARIES utmps)

if (UTMPS_INCLUDE_DIR AND UTMPS_LIBRARIES)
    set(UTMPS_FOUND TRUE)
endif (UTMPS_INCLUDE_DIR AND UTMPS_LIBRARIES)
