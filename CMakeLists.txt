cmake_minimum_required(VERSION 3.16)
project(rtlgen CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra)

find_package(nlohmann_json 3 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

add_library(rtlgen_core STATIC src/expr.cpp src/prims.cpp src/elab.cpp src/checks.cpp src/emit.cpp)
target_include_directories(rtlgen_core PUBLIC src)
target_link_libraries(rtlgen_core PUBLIC nlohmann_json::nlohmann_json)

add_executable(rtlgen src/main.cpp)
target_link_libraries(rtlgen PRIVATE rtlgen_core)

enable_testing()
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE rtlgen_core)
add_test(NAME unit COMMAND unit_tests)
add_test(NAME sim_prims COMMAND sh ${CMAKE_SOURCE_DIR}/tests/sim_prims.sh $<TARGET_FILE:rtlgen>)
add_test(NAME examples COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/regress.py --rtlgen $<TARGET_FILE:rtlgen>
         --examples ${CMAKE_SOURCE_DIR}/examples)
