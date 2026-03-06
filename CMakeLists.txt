cmake_minimum_required(VERSION 3.16)

project(DirectoryTreeUtility
    VERSION 1.4.0
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
    src/app/main.cpp
    src/app/Application.cpp
    src/app/ApplicationUi.cpp
    src/app/ApplicationWorkflows.cpp
    src/app/ApplicationDialogs.cpp
    src/platform/win32/DarkMode.cpp
    src/platform/win32/FileExplorerIntegration.cpp
    src/platform/win32/SystemTray.cpp
    src/platform/win32/GlobalHotkeys.cpp
    src/rendering/UiRenderer.cpp
    src/services/DirectoryTreeBuilder.cpp
    src/services/TreeGenerationService.cpp
    src/services/FileSaveService.cpp
    src/services/UpdateService.cpp
)

# Header files
set(HEADERS
    src/app/Application.h
    src/app/ApplicationInternal.h
    src/platform/win32/DarkMode.h
    src/platform/win32/IatHook.h
    src/platform/win32/FileExplorerIntegration.h
    src/platform/win32/SystemTray.h
    src/platform/win32/GlobalHotkeys.h
    src/rendering/UiRenderer.h
    src/services/DirectoryTreeBuilder.h
    src/services/TreeGenerationService.h
    src/services/FileSaveService.h
    src/services/UpdateService.h
    src/shared/AppInfo.h
    src/shared/AppTheme.h
    src/resources/resource.h
)

# Resource files
set(RESOURCES
    src/resources/resource.rc
)

# Enable resource compilation
enable_language(RC)

# Create the executable
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})

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
        shlwapi
        uxtheme
        winhttp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/services
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared
)

# Set output directories
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)

# Configuration-specific compile definitions (works for both single- and multi-config generators)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG;_DEBUG>
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# Install rules
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

include(CTest)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Output directory: ${CMAKE_BINARY_DIR}/bin")
