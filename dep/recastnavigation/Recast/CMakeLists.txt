# Copyright (C) 2011-2012 DarkCore <http://www.darkpeninsula.eu/>
# Copyright (C) 2011-2012 Project SkyFire <http://www.projectskyfire.org/>
# Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
# Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


set(Recast_STAT_SRCS
    Recast.cpp
    RecastAlloc.cpp
    RecastArea.cpp
    RecastContour.cpp
    RecastFilter.cpp
    RecastMesh.cpp
    RecastMeshDetail.cpp
    RecastRasterization.cpp
    RecastRegion.cpp
)

if(WIN32)
  include_directories(
    ${CMAKE_SOURCE_DIR}/dep/zlib
  )
endif()

add_library(Recast STATIC ${Recast_STAT_SRCS})

target_link_libraries(Recast ${ZLIB_LIBRARIES})