cmake_minimum_required(VERSION 3.16)

project(DirectoryTreeUtility
    VERSION 1.0.0
    DESCRIPTION "Directory Tree Utility for Windows"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set Windows specific options
if(WIN32)
    set(CMAKE_SYSTEM_VERSION 10.0)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/DirectoryTreeBuilder.cpp
    src/FileExplorerIntegration.cpp
    src/SystemTray.cpp
    src/GlobalHotkeys.cpp
)

# Header files
set(HEADERS
    src/Application.h
    src/DirectoryTreeBuilder.h
    src/FileExplorerIntegration.h
    src/SystemTray.h
    src/GlobalHotkeys.h
)

# Create the executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS})

# Set target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "DirectoryTreeUtility"
    WIN32_EXECUTABLE TRUE
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        comctl32
        ole32
        shell32
        user32
        kernel32
        gdi32
        comdlg32
    )
endif()

# Compiler-specific options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4          # Warning level 4
        /permissive- # Strict conformance mode
        /utf-8       # UTF-8 encoding
    )
    
    # Set subsystem to Windows for GUI application
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup"
    )
elseif(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    
    # Set subsystem to Windows for GUI application
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "-mwindows"
    )
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Set output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Debug configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG _DEBUG)
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE NDEBUG)
endif()

# Install rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")