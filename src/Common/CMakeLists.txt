# src/Common/CMakeLists.txt
# Common libraries used across Canvas components

# RingBuffer library
add_library(RingBuffer STATIC
    RingBuffer/RingBuffer.cpp
    Inc/RingBuffer.h
    pch.h
)

target_include_directories(RingBuffer 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Inc
        ${CMAKE_SOURCE_DIR}/deps/GEM/Inc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_precompile_headers(RingBuffer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

# TaskManager library
add_library(TaskManager STATIC
    TaskManager/TaskManager.cpp
    Inc/TaskManager.h
    pch.h
)

target_include_directories(TaskManager 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Inc
        ${CMAKE_SOURCE_DIR}/deps/GEM/Inc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(TaskManager PUBLIC
    RingBuffer
)

target_precompile_headers(TaskManager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)

# Set folder organization in IDEs
set_target_properties(RingBuffer PROPERTIES FOLDER "Common")
set_target_properties(TaskManager PROPERTIES FOLDER "Common")
