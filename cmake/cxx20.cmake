# Upstream subprojects may reset CMAKE_CXX_STANDARD to 11/14. Apply the
# requested standard after every target has been declared, including their
# dependencies, so generated command lines have one effective C++20 setting.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    function(goldcraft_cxx20_in_directory directory)
        get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target IN LISTS targets)
            set_target_properties("${target}" PROPERTIES
                CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
        endforeach()
        get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
        foreach(child IN LISTS children)
            goldcraft_cxx20_in_directory("${child}")
        endforeach()
    endfunction()
    cmake_language(DEFER CALL goldcraft_cxx20_in_directory "${CMAKE_SOURCE_DIR}")
endif()
