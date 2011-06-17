include_directories( ${CMAKE_CURRENT_BINARY_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/.. )

add_llvm_library(LLVMTMS320C64XInfo
	TMS320C64XTargetInfo.cpp
)

add_dependencies(LLVMTMS320C64XInfo TMS320C64X)
