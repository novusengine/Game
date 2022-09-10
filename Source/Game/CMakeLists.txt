project(game VERSION 1.0.0 DESCRIPTION "Game is an application")

file(GLOB_RECURSE GAME_FILES "*.cpp" "*.h")

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.rc")
add_executable(${PROJECT_NAME} ${GAME_FILES} ${APP_ICON_RESOURCE_WINDOWS})
target_compile_definitions(${PROJECT_NAME} PUBLIC SHADER_SOURCE_DIR="${SHADER_SOURCE_DIR}" NOMINMAX _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC
	base::base
    fileformat::fileformat
    input::input
    renderer::renderer
    scripting::scripting
    meshoptimizer
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER ${ROOT_FOLDER} LINKER_LANGUAGE CXX)
create_vs_filters(${GAME_FILES})

#set the visual studio working directory to the parent path of the client executable
set_target_properties(
    ${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../")
    
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})