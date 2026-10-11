# ResourcePrecacher — Plugify C++ plugin: port of the Pisex "ResourcePrecacher" plugin (adds the
# resources of resource_precacher.ini / precacher.ini to every game session manifest).
#
#   plugify-ResourcePrecacher  the plugin shared library (entry "bin/ResourcePrecacher")

set(RESOURCE_PRECACHER_VERSION "1.0.1")

add_library(plugify-ResourcePrecacher SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
)
target_include_directories(plugify-ResourcePrecacher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated s2sdk/plg/pisexcompat headers must not fail -Werror on our
# sources.
target_include_directories(plugify-ResourcePrecacher SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/include"
        "${CMAKE_SOURCE_DIR}/libs/pisexcompat/include")
target_link_libraries(plugify-ResourcePrecacher PRIVATE
        pisexcompat
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-ResourcePrecacher PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-ResourcePrecacher PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="ResourcePrecacher"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-ResourcePrecacher PROPERTIES OUTPUT_NAME ResourcePrecacher)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-ResourcePrecacher PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/ResourcePrecacher.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ResourcePrecacher.pplugin"
)

# --- tests --------------------------------------------------------------------
# Exercise the real plugin lifecycle with stubbed s2sdk bindings, as well as config parsing.
add_executable(resource_precacher_tests tests/resource_precacher_tests.cpp src/plugin.cpp)
target_include_directories(resource_precacher_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(resource_precacher_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(resource_precacher_tests PRIVATE pisexcompat s2sdk_bindings -static-libstdc++ -static-libgcc)
target_compile_options(resource_precacher_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(resource_precacher_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="ResourcePrecacher"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
        RP_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
set_target_properties(resource_precacher_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
