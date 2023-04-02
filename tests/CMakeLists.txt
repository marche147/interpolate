if (${LLVM_VERSION_MAJOR} VERSION_GREATER_EQUAL 14)
  set(FILECHECK_ARGS "${FILECHECK_ARGS} --allow-unused-prefixes")
endif()

configure_file(lit.site.cfg.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
  @ONLY)

add_custom_target(check
  lit 
  --verbose 
  --path=${LLVM_TOOLS_BINARY_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Unit tests"
  USES_TERMINAL
)

add_dependencies(check Interpolate)