add_executable (athena-transmitter
  athena_transmitter.cpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/RAG/rag_delegation_crypto.cpp
)

target_include_directories (athena-transmitter PRIVATE
  ${ATHENA_SOURCE_DIR}/src/Subsystems/RAG
  ${LIBSODIUM_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
)

target_link_libraries (athena-transmitter PRIVATE
  ${LIBSODIUM_LIBRARIES}
  pthread
)

install (TARGETS athena-transmitter DESTINATION bin)
