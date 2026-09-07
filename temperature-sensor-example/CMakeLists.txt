set(kfsw_temperature_sensor_example_sources
    src/temperature_sensor_example.c
    parameters/temperature_sensor_example_table.c
)

if(CONFIG_KFSW_TEMP_EXAMPLE_SENSOR)
    list(APPEND kfsw_temperature_sensor_example_sources
        src/temperature_sensor_example_sensor.c
    )
endif()

add_library(kfsw_temperature_sensor_example STATIC ${kfsw_temperature_sensor_example_sources})
add_library(kfsw::temperature_sensor_example ALIAS kfsw_temperature_sensor_example)
add_dependencies(kfsw_temperature_sensor_example zephyr_generated_headers)

target_include_directories(kfsw_temperature_sensor_example
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(kfsw_temperature_sensor_example
    PUBLIC
        zephyr_interface
    PRIVATE
        kfsw::platform
        kfsw::services
)
