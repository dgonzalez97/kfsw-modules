set(kfsw_radio_uhf_sources
    src/radio_uhf.c
)

if(CONFIG_KFSW_RADIO_UHF_HOLYBRO)
    list(APPEND kfsw_radio_uhf_sources
        holybro/src/holybro.c
    )
endif()

add_library(kfsw_radio_uhf STATIC ${kfsw_radio_uhf_sources})
add_library(kfsw::radio_uhf ALIAS kfsw_radio_uhf)

target_include_directories(kfsw_radio_uhf
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(kfsw_radio_uhf
    PUBLIC
        zephyr_interface
)
