set(kfsw_button_test_sources
    src/button_test.c
    parameters/button_test_table.c
)

if(CONFIG_KFSW_BUTTON_TEST_SHELL)
    list(APPEND kfsw_button_test_sources
        src/button_test_shell.c
    )
endif()

if(CONFIG_KFSW_BUTTON_TEST_GPIO OR CONFIG_KFSW_BUTTON_TEST_LED_GPIO)
    list(APPEND kfsw_button_test_sources
        src/button_test_gpio.c
    )
endif()

add_library(kfsw_button_test STATIC ${kfsw_button_test_sources})
add_library(kfsw::button_test ALIAS kfsw_button_test)
add_dependencies(kfsw_button_test zephyr_generated_headers)

target_include_directories(kfsw_button_test
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(kfsw_button_test
    PUBLIC
        zephyr_interface
    PRIVATE
        kfsw::platform
        kfsw::services
)
