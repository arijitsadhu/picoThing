target_sources(${PROGRAM_NAME}
    PRIVATE
        bm.c
        bm.h
)

target_include_directories(${PROGRAM_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)