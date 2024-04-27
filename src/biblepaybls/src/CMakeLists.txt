file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/../include/biblepaybls/*.hpp)
source_group("SrcHeaders" FILES ${HEADERS})

add_library(biblepaybls
  ${HEADERS}
  ${CMAKE_CURRENT_SOURCE_DIR}/privatekey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/bls.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/chaincode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/elements.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/extendedprivatekey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/extendedpublickey.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/legacy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/schemes.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/threshold.cpp)

target_include_directories(biblepaybls
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<$<BOOL:${GMP_FOUND}>:${GMP_INCLUDES}>
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/biblepaybls
    ${CMAKE_CURRENT_SOURCE_DIR}/../depends/mimalloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../depends/relic/include
    ${CMAKE_CURRENT_BINARY_DIR}/../depends/relic/include
)

target_compile_definitions(biblepaybls
  PRIVATE
    BLSALLOC_MIMALLOC=1
)

target_link_libraries(biblepaybls
  PUBLIC
    relic_s
    mimalloc-static
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../depends/relic/include/ DESTINATION include/biblepaybls)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/../depends/relic/include/ DESTINATION include/biblepaybls)
install(FILES ${HEADERS} DESTINATION include/biblepaybls)
install(FILES $<TARGET_FILE:biblepaybls> DESTINATION lib)

if(BUILD_BLS_TESTS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../depends/catch2/CMakeLists.txt)

  add_executable(runtest test.cpp)
  target_link_libraries(runtest
                        PRIVATE biblepaybls
                        PRIVATE catch2)
endif()

if(BUILD_BLS_BENCHMARKS)
  add_executable(runbench test-bench.cpp)
  target_link_libraries(runbench PRIVATE biblepaybls)
endif()
