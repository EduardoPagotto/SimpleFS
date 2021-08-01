cmake_minimum_required(VERSION 3.18.4)

PROJECT(sfssh)

#define os Lib's a serem usados
set (LibsSfs ${CMAKE_SOURCE_DIR}/bin/libsfs.a
			  -lpthread)

#define os includes
set (IncludeShell ${CMAKE_SOURCE_DIR}/include)

add_executable (sfssh sfssh.cpp)

add_dependencies(sfssh sfs)

target_link_libraries(sfssh ${LibsSfs})
target_include_directories (sfssh PRIVATE ${IncludeShell})

INSTALL(FILES ${sfssh_BINARY_DIR}/sfssh DESTINATION bin)
