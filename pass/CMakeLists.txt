add_library(Interpolate SHARED
  Pass.cpp
  Interpolate.cpp
  Compile.cpp
)
target_link_libraries(Interpolate ${LLVM_LIB})

if (NOT LLVM_ENABLE_RTTI)
  set_target_properties(Interpolate PROPERTIES COMPILE_FLAGS "-fno-rtti")
endif()