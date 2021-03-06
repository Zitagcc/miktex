## CMakeLists.txt					-*- CMake -*-
##
## Copyright (C) 2006-2016 Christian Schenk
## 
## This file is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published
## by the Free Software Foundation; either version 2, or (at your
## option) any later version.
## 
## This file is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
## 
## You should have received a copy of the GNU General Public License
## along with this file; if not, write to the Free Software
## Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
## USA.

use_static_crt()

add_library(${curl_lib_name} STATIC ${curl_sources})

set_property(TARGET ${curl_lib_name} PROPERTY FOLDER ${MIKTEX_CURRENT_FOLDER})

target_compile_definitions(${curl_lib_name}
  PUBLIC
    -DCURL_STATICLIB
)

target_include_directories(${curl_lib_name}
  PUBLIC
    ${public_include_directories}
)

if (MIKTEX_NATIVE_WINDOWS)
  target_link_libraries(${curl_lib_name}
    PUBLIC
      crypt32
      winmm
      wsock32
      ws2_32
      Wldap32
  )
endif()
