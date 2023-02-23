aux_source_directory(. SRC_LIST)

add_executable(Webserver ${SRC_LIST})

target_link_libraries(Webserver pthread mysqlclient)