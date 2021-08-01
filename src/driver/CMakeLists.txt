cmake_minimum_required(VERSION 3.18.4)

PROJECT(sfs)

#define objetos a compilar
set (SfsSource disk.cpp fs.cpp)

#define os includes
set (SfsInclude ${CMAKE_SOURCE_DIR}/include) # Raiz do projeto

# Define o Target
add_library (sfs STATIC ${SfsSource})

# Define os includes privados para este target
target_include_directories (sfs PUBLIC ${SfsInclude})

INSTALL(FILES ${sfs_BINARY_DIR}/libsfs.a DESTINATION lib)
INSTALL(DIRECTORY ${SfsInclude}/sfs DESTINATION ${CMAKE_INSTALL_PREFIX}/include/sfs FILES_MATCHING PATTERN "*.h*")
