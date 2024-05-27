target_sources(${PROGRAM_NAME}
    PRIVATE
        uc8151c.c
        uc8151c.h
)

target_include_directories(${PROGRAM_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)
