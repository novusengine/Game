project(game-dependencies VERSION 1.0.0)

add_subdirectory(meshoptimizer)

## game-dependencies

add_library(${PROJECT_NAME} INTERFACE)
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
add_library(game::dependencies ALIAS ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} INTERFACE
)
