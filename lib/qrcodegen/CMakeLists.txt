include(FetchContent)
set(FETCHCONTENT_BASE_DIR_SAVE ${FETCHCONTENT_BASE_DIR})

FetchContent_Declare(
        qr_code_generator
        URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.zip
        URL_HASH MD5=50e42738704091a9b76a67e31eb4f58a
)

if (NOT qr_code_generator)
    message("Downloading QR Code generator")
    FetchContent_MakeAvailable(qr_code_generator)
endif ()
set(QR_CODE_GENERATOR_PATH ${qr_code_generator_SOURCE_DIR})

add_library(qrcodegen
    ${QR_CODE_GENERATOR_PATH}/c/qrcodegen.c
)

target_include_directories(qrcodegen
    PUBLIC
        ${QR_CODE_GENERATOR_PATH}/c/
)
