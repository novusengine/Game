project(shadercookerstandalone VERSION 1.0.0 DESCRIPTION "Standalone Shader Cooker for compiling shaders")

file(GLOB_RECURSE SHADER_COOKER_STANDALONE_FILES "*.cpp" "*.h")

add_executable(${PROJECT_NAME} ${SHADER_COOKER_STANDALONE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER ${ROOT_FOLDER})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

create_vs_filters(${SHADER_COOKER_STANDALONE_FILES})

include_directories(include/)

if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE c++fs)
elseif (UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE stdc++fs)
endif()

add_compile_definitions(NOMINMAX _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)

target_link_libraries(${PROJECT_NAME} PRIVATE
    base::base
    shadercooker::shadercooker
)

install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    TARGET shadercookerstandalone 
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${DXCOMPILER_DYNAMIC_LIB_PATH}"
        "$<TARGET_FILE_DIR:shadercookerstandalone>/dxcompiler.dll"
)