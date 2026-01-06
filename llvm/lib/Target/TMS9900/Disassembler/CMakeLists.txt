add_llvm_component_library(LLVMTMS9900Disassembler
  TMS9900Disassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  Support
  TMS9900Desc
  TMS9900Info

  ADD_TO_COMPONENT
  TMS9900
)
