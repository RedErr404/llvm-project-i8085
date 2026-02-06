//===-- ABISysV_tms9900.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_TMS9900_ABISYSV_TMS9900_H
#define LLDB_SOURCE_PLUGINS_ABI_TMS9900_ABISYSV_TMS9900_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

class ABISysV_tms9900 : public lldb_private::RegInfoBasedABI {
public:
  ~ABISysV_tms9900() override = default;

  size_t GetRedZoneSize() const override;

  bool PrepareTrivialCall(lldb_private::Thread &thread, lldb::addr_t sp,
                          lldb::addr_t functionAddress,
                          lldb::addr_t returnAddress,
                          llvm::ArrayRef<lldb::addr_t> args) const override;

  bool GetArgumentValues(lldb_private::Thread &thread,
                         lldb_private::ValueList &values) const override;

  lldb_private::Status
  SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                       lldb::ValueObjectSP &new_value) override;

  lldb::ValueObjectSP
  GetReturnValueObjectImpl(lldb_private::Thread &thread,
                           lldb_private::CompilerType &type) const override;

  bool
  CreateFunctionEntryUnwindPlan(lldb_private::UnwindPlan &unwind_plan) override;

  bool CreateDefaultUnwindPlan(lldb_private::UnwindPlan &unwind_plan) override;

  bool RegisterIsVolatile(const lldb_private::RegisterInfo *reg_info) override;

  bool CallFrameAddressIsValid(lldb::addr_t cfa) override {
    // Stack addresses are word-aligned (2 bytes) and non-zero
    if (cfa & 0x01 || cfa == 0)
      return false;
    return true;
  }

  bool CodeAddressIsValid(lldb::addr_t pc) override {
    // TMS9900 has 16-bit address space, word-aligned code
    return (pc & 0x01) == 0 && pc <= 0xFFFF;
  }

  const lldb_private::RegisterInfo *
  GetRegisterInfoArray(uint32_t &count) override;

  // TMS9900 has 64KB address space, typical stack is small
  uint64_t GetStackFrameSize() override { return 256; }

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------

  static void Initialize();

  static void Terminate();

  static lldb::ABISP CreateInstance(lldb::ProcessSP process_sp,
                                    const lldb_private::ArchSpec &arch);

  static llvm::StringRef GetPluginNameStatic() { return "sysv-tms9900"; }

  // PluginInterface protocol

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  void CreateRegisterMapIfNeeded();

  lldb::ValueObjectSP
  GetReturnValueObjectSimple(lldb_private::Thread &thread,
                             lldb_private::CompilerType &ast_type) const;

  bool RegisterIsCalleeSaved(const lldb_private::RegisterInfo *reg_info);

private:
  using lldb_private::RegInfoBasedABI::RegInfoBasedABI;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_TMS9900_ABISYSV_TMS9900_H
