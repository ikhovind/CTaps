project(CTapsExamples)

add_executable(example_client example_client.c)
target_link_libraries(example_client PRIVATE CTaps)

add_executable(example_server example_server.c)
target_link_libraries(example_server PRIVATE CTaps)
