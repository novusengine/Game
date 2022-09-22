project(assetconverter VERSION 1.0.0 DESCRIPTION "AssetConverter is an application")

file(GLOB_RECURSE ASSETCONVERTER_FILES "*.cpp" "*.h")

set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.rc")
add_executable(${PROJECT_NAME} ${ASSETCONVERTER_FILES} ${APP_ICON_RESOURCE_WINDOWS})
target_compile_definitions(${PROJECT_NAME} PUBLIC NOMINMAX _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC
	base::base
    fileformat::fileformat
    meshoptimizer
    casc_static
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER ${ROOT_FOLDER} LINKER_LANGUAGE CXX)
create_vs_filters(${ASSETCONVERTER_FILES})
    
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})