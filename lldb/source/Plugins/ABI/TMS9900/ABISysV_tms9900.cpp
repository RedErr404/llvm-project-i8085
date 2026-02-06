//===-- ABISysV_tms9900.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_tms9900.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Core/ValueObjectRegister.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_tms9900, ABITMS9900)

// TMS9900 DWARF register numbers (from TMS9900RegisterInfo.td)
//   R0-R15 = 0-15  (workspace registers)
//   PC     = 16    (program counter)
//   WP     = 17    (workspace pointer)
//   ST     = 18    (status register)
//
// Special roles:
//   R10 (DWARF 10) = SP (stack pointer)
//   R11 (DWARF 11) = LR (link register / return address)
//   R0  (DWARF 0)  = return value register
enum dwarf_regnums {
  dwarf_r0 = 0,
  dwarf_r1,
  dwarf_r2,
  dwarf_r3,
  dwarf_r4,
  dwarf_r5,
  dwarf_r6,
  dwarf_r7,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,  // SP
  dwarf_r11,  // LR
  dwarf_r12,
  dwarf_r13,
  dwarf_r14,
  dwarf_r15,
  dwarf_pc,   // 16
  dwarf_wp,   // 17
  dwarf_st,   // 18
};

static const RegisterInfo g_register_infos[] = {
    {"r0",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r0, dwarf_r0, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r1",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r1, dwarf_r1, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r2",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r2, dwarf_r2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r3",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r3, dwarf_r3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r4",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r4, dwarf_r4, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r5",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r5, dwarf_r5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r6",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r6, dwarf_r6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r7",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r7, dwarf_r7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r8",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r8, dwarf_r8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r9",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r9, dwarf_r9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r10",
     "sp",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r10, dwarf_r10, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r11",
     "lr",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r11, dwarf_r11, LLDB_REGNUM_GENERIC_RA, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r12",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r12, dwarf_r12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r13",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r13, dwarf_r13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r14",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r14, dwarf_r14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r15",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r15, dwarf_r15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"pc",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"wp",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_wp, dwarf_wp, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"st",
     nullptr,
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_st, dwarf_st, LLDB_REGNUM_GENERIC_FLAGS, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    }};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const lldb_private::RegisterInfo *
ABISysV_tms9900::GetRegisterInfoArray(uint32_t &count) {
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_tms9900::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_tms9900::CreateInstance(lldb::ProcessSP process_sp,
                                const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::tms9900) {
    return ABISP(
        new ABISysV_tms9900(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_tms9900::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                         lldb::addr_t pc, lldb::addr_t ra,
                                         llvm::ArrayRef<addr_t> args) const {
  // No JIT support for TMS9900
  return false;
}

bool ABISysV_tms9900::GetArgumentValues(Thread &thread,
                                        ValueList &values) const {
  return false;
}

Status ABISysV_tms9900::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                             lldb::ValueObjectSP &new_value_sp) {
  return Status();
}

ValueObjectSP ABISysV_tms9900::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

ValueObjectSP ABISysV_tms9900::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

// Called when we are on the first instruction of a new function.
// On TMS9900, BL puts the return address in R11 (not on the stack).
// At function entry: CFA = SP (R10), return address = R11.
bool ABISysV_tms9900::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_r10;
  uint32_t lr_reg_num = dwarf_r11;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // At function entry, nothing has been pushed yet.
  // CFA = SP + 0
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 0);

  // Return address is in R11 (link register), not on the stack.
  row->SetRegisterLocationToInRegister(pc_reg_num, lr_reg_num, true);

  // SP is the CFA value itself.
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("tms9900 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

// Default unwind plan for inside a function (after prologue).
// Standard TMS9900 prologue:
//   DECT R10       ; SP -= 2
//   MOV  R11, *R10 ; push return address
// After prologue: CFA = SP + 2, return address at [CFA - 2].
bool ABISysV_tms9900::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_r10;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // After standard prologue: CFA = SP + 2 (one word pushed)
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 2);

  // Return address saved at [CFA - 2] = [SP]
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -2, true);

  // SP = CFA
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("tms9900 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

bool ABISysV_tms9900::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

// TMS9900 callee-saved registers: R13, R14, R15
// R10 (SP) and R11 (LR) are handled by the unwind mechanism.
bool ABISysV_tms9900::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  if (!reg_info)
    return false;

  // Use DWARF register number to identify
  uint32_t dwarf_reg = reg_info->kinds[eRegisterKindDWARF];
  switch (dwarf_reg) {
  case dwarf_r10:  // SP - always preserved
  case dwarf_r11:  // LR - saved/restored in prologue/epilogue
  case dwarf_r13:  // callee-saved
  case dwarf_r14:  // callee-saved
  case dwarf_r15:  // callee-saved
    return true;
  default:
    return false;
  }
}

void ABISysV_tms9900::Initialize(void) {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for TMS9900 targets", CreateInstance);
}

void ABISysV_tms9900::Terminate(void) {
  PluginManager::UnregisterPlugin(CreateInstance);
}
