add_llvm_component_group(TMS9900)

set(LLVM_TARGET_DEFINITIONS TMS9900.td)

tablegen(LLVM TMS9900GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM TMS9900GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM TMS9900GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM TMS9900GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM TMS9900GenCallingConv.inc -gen-callingconv)
tablegen(LLVM TMS9900GenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM TMS9900GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM TMS9900GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM TMS9900GenDisassemblerTables.inc -gen-disassembler)

add_public_tablegen_target(TMS9900CommonTableGen)

add_llvm_target(TMS9900CodeGen
  TMS9900AsmPrinter.cpp
  TMS9900FrameLowering.cpp
  TMS9900InstrInfo.cpp
  TMS9900ISelDAGToDAG.cpp
  TMS9900ISelLowering.cpp
  TMS9900LongBranch.cpp
  TMS9900MachineFunctionInfo.cpp
  TMS9900MCInstLower.cpp
  TMS9900Peephole.cpp
  TMS9900RegisterInfo.cpp
  TMS9900Subtarget.cpp
  TMS9900TargetMachine.cpp
  TMS9900TargetObjectFile.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TMS9900Desc
  TMS9900Info

  ADD_TO_COMPONENT
  TMS9900
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
