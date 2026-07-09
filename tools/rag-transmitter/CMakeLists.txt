add_executable (rag-transmitter
  rag_transmitter.cpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/RAG/rag_delegation_crypto.cpp
)

target_include_directories (rag-transmitter PRIVATE
  ${ATHENA_SOURCE_DIR}/src/Subsystems/RAG
  ${LIBSODIUM_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries (rag-transmitter PRIVATE
  ${LIBSODIUM_LIBRARIES}
  pthread
)

install (TARGETS rag-transmitter DESTINATION bin)
