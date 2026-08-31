cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_boton_test_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../boton-test/src
)
target_link_libraries(app PRIVATE
    kfsw::boton_test
    kfsw::services
)
