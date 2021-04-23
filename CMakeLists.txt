#
# Sporks, the learning, scriptable Discord bot!
#
# Copyright 2019 Craig Edwards <support@sporks.gg>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required (VERSION 3.6)
project(bot)

aux_source_directory("src" coresrc)
add_executable(bot ${coresrc})

string(ASCII 27 Esc)

include(buildtools/cmake/FindMYSQL.cmake)
include(buildtools/cmake/FindPCRE.cmake)

include_directories( "include" )

target_link_libraries(bot dl mysqlclient pcre dpp fmt spdlog)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pthread -g -O2 -fPIC -rdynamic")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2")

target_link_libraries(bot)

set (modules_dir "modules")
file(GLOB subdirlist ${modules_dir}/*)
foreach (fullmodname ${subdirlist})
	get_filename_component(modname ${fullmodname} NAME)
	message(STATUS "Found module '${Esc}[1;34mmodule_${modname}${Esc}[m'")
	set (modsrc "")
	aux_source_directory(${modules_dir}/${modname} modsrc)
	add_library(module_${modname} SHARED ${modsrc})
	set_target_properties(module_${modname} PROPERTIES PREFIX "")
endforeach(fullmodname)

