set(kfsw_gpredict_sources
    src/gpredict.c
    src/gpredict_profiles.c
)

if(CONFIG_KFSW_PARAM)
    list(APPEND kfsw_gpredict_sources parameters/gpredict_table.c)
endif()

if(CONFIG_KFSW_COMMAND)
    list(APPEND kfsw_gpredict_sources src/gpredict_commands.c)
endif()

add_library(kfsw_gpredict STATIC ${kfsw_gpredict_sources})
add_library(kfsw::gpredict ALIAS kfsw_gpredict)
add_dependencies(kfsw_gpredict zephyr_generated_headers)

target_include_directories(kfsw_gpredict
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(kfsw_gpredict
    PUBLIC
        zephyr_interface
    PRIVATE
        kfsw::platform
        kfsw::services
)
