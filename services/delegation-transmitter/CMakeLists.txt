add_executable (athena-transmitter
  athena_transmitter.cpp
  ${ATHENA_SOURCE_DIR}/src/Services/RAG/rag_delegation_crypto.cpp
)

target_include_directories (athena-transmitter PRIVATE
  ${ATHENA_SOURCE_DIR}/src/Services/RAG
  ${LIBSODIUM_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries (athena-transmitter PRIVATE
  ${LIBSODIUM_LIBRARIES}
  pthread
)

install (TARGETS athena-transmitter DESTINATION bin)
