# ResourcePrecacher — Plugify C++ plugin: port of the Pisex "ResourcePrecacher" plugin (adds the
# resources of resource_precacher.ini / precacher.ini to every game session manifest).
#
# The plugin calls s2sdk directly (PLUGIN_STANDARD §7: no pisexcompat, no guard library); its
# engine-free parts - the KV1 reader, the list extraction and the config path resolution - live in
# src/precache_core.hpp so resource_precacher_tests can run exactly that code.
#
#   plugify-ResourcePrecacher  the plugin shared library (entry "bin/ResourcePrecacher")

set(RESOURCE_PRECACHER_VERSION "1.1.0")

add_library(plugify-ResourcePrecacher SHARED
        src/plugin.cpp
        # The __s2sdk_* binding globals the plugify module fills from the manifest's dependencies.
        $<TARGET_OBJECTS:s2sdk_bindings>
)
target_include_directories(plugify-ResourcePrecacher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated s2sdk/plg headers must not fail -Werror on our sources.
target_include_directories(plugify-ResourcePrecacher SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-ResourcePrecacher PRIVATE
        plugify_sdk
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

# --- tests --------------------------------------------------------------------------------------
# resource_precacher_tests: the self-contained KV1 reader (grammar and malformed input), the list
# extraction against the two real config files, the config path resolution under <GetConfigsDir()>,
# and the real plugin lifecycle with stubbed s2sdk bindings. src/plugin.cpp is compiled in, so the
# PLUGIFY_PLUGIN globals (plg::GetConfigsDir, ...) are the plugin's own.
add_executable(resource_precacher_tests tests/resource_precacher_tests.cpp src/plugin.cpp)
target_include_directories(resource_precacher_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(resource_precacher_tests SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(resource_precacher_tests PRIVATE plugify_sdk s2sdk_bindings -static-libstdc++ -static-libgcc)
target_compile_options(resource_precacher_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(resource_precacher_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="ResourcePrecacher"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
        RP_TEST_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
set_target_properties(resource_precacher_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
