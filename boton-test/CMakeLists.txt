set(kfsw_boton_test_sources
    src/boton_test.c
)

if(CONFIG_KFSW_BOTON_TEST_GPIO)
    list(APPEND kfsw_boton_test_sources
        src/boton_test_gpio.c
    )
endif()

add_library(kfsw_boton_test STATIC ${kfsw_boton_test_sources})
add_library(kfsw::boton_test ALIAS kfsw_boton_test)
add_dependencies(kfsw_boton_test zephyr_generated_headers)

target_include_directories(kfsw_boton_test
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(kfsw_boton_test
    PUBLIC
        zephyr_interface
    PRIVATE
        kfsw::platform
        kfsw::services
)
