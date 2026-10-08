add_library(athena_hodarium STATIC revisions.cpp)
set_target_properties(athena_hodarium PROPERTIES AUTOMOC OFF)
target_compile_features(athena_hodarium PUBLIC cxx_std_17)
target_include_directories(athena_hodarium PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR} ${LIBSODIUM_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS})
target_link_libraries(athena_hodarium PRIVATE ${SQLITE3_LIBRARIES} ${LIBSODIUM_LIBRARIES})
