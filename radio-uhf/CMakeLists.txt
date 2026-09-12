set(kfsw_radio_uhf_sources
    src/radio_uhf.c
)

if(CONFIG_KFSW_PARAM)
    list(APPEND kfsw_radio_uhf_sources parameters/radio_uhf_table.c)
endif()

if(CONFIG_KFSW_RADIO_UHF_HOLYBRO)
    list(APPEND kfsw_radio_uhf_sources
        holybro/src/holybro.c
    )
endif()

if(CONFIG_KFSW_RADIO_UHF_CRYPTO)
    list(APPEND kfsw_radio_uhf_sources src/radio_crypto.c src/radio_crypto_store.c)
    if(CONFIG_BOARD_NATIVE_SIM)
        target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/radio_crypto_host.c)
    endif()
endif()

add_library(kfsw_radio_uhf STATIC ${kfsw_radio_uhf_sources})
add_library(kfsw::radio_uhf ALIAS kfsw_radio_uhf)
add_dependencies(kfsw_radio_uhf zephyr_generated_headers)

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

if(CONFIG_KFSW_PARAM)
    target_link_libraries(kfsw_radio_uhf PUBLIC kfsw::services)
endif()

if(CONFIG_KFSW_RADIO_UHF_CRYPTO)
    target_link_libraries(kfsw_radio_uhf PUBLIC kfsw::comms mbedTLS)
endif()
