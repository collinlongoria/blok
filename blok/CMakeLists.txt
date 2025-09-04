set(SOURCES
        # Add all files under here:
        src/main.cpp
)

add_executable(blok ${SOURCES})

target_include_directories(blok
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
)