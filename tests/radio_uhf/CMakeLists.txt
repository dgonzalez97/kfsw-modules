cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(kfsw_radio_uhf_tests LANGUAGES C)

target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE kfsw::radio_uhf)
