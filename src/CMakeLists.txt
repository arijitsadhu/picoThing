set(PROGRAM_NAME picoThing)

# generate web files
execute_process(COMMAND
        perl ${PICO_SDK_PATH}/lib/lwip/src/apps/http/makefsdata/makefsdata
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        ECHO_OUTPUT_VARIABLE
        ECHO_ERROR_VARIABLE
        )
file(RENAME ${PROJECT_SOURCE_DIR}/fsdata.c tmp_fsdata.c)


# compile code
add_executable(${PROGRAM_NAME}
    ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dhserver.c
    ${PICO_SDK_PATH}/lib/tinyusb/lib/networking/dnserver.c
    main.c
)

add_subdirectory(bm)
add_subdirectory(uc8151c)

target_compile_definitions(${PROGRAM_NAME} PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    PROGRAM_NAME=\"${PROGRAM_NAME}\"
)

target_include_directories(${PROGRAM_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${PICO_SDK_PATH}/lib/tinyusb/lib/networking
)

target_link_libraries(${PROGRAM_NAME}
    hardware_adc
    hardware_gpio
    hardware_spi
    hardware_watchdog
    pico_aon_timer
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_http
    pico_lwip_mdns
    pico_lwip_mqtt
    pico_lwip_sntp
    pico_stdlib
    qrcodegen
)

pico_enable_stdio_usb(${PROGRAM_NAME} 1)
pico_enable_stdio_uart(${PROGRAM_NAME} 1)

pico_add_extra_outputs(${PROGRAM_NAME})
