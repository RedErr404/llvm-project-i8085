//===-- I8085RegisterInfo.td - I8085 Register defs -------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the I8085 register file
//===----------------------------------------------------------------------===//

// 8-bit General purpose register definition.
class I8085Reg<bits<16> num, string name, list<Register> subregs = [],
             list<string> altNames = []> : RegisterWithSubRegs<name, subregs> {
  field bits<16> Num = num;

  let HWEncoding = num;
  let Namespace = "I8085";
  let SubRegs = subregs;
  let AltNames = altNames;
}

// Subregister indices.
let Namespace = "I8085" in {
  def sub_lo : SubRegIndex<8>;
  def sub_hi : SubRegIndex<8, 8>;
}

let Namespace = "I8085" in { def ptr : RegAltNameIndex; }

//===----------------------------------------------------------------------===//
//  8-bit general purpose registers
//===----------------------------------------------------------------------===//

def A : I8085Reg<7, "A">, DwarfRegNum<[7]>;
def B : I8085Reg<0, "B">, DwarfRegNum<[0]>;
def C : I8085Reg<1, "C">, DwarfRegNum<[1]>;
def D : I8085Reg<2, "D">, DwarfRegNum<[2]>;
def E : I8085Reg<3, "E">, DwarfRegNum<[3]>;
def H : I8085Reg<4, "H">, DwarfRegNum<[4]>;
def L : I8085Reg<5, "L">, DwarfRegNum<[5]>;

def M : I8085Reg<6, "M">, DwarfRegNum<[6]>;

def SP : I8085Reg<7, "SP">, DwarfRegNum<[8]>;
def PC : I8085Reg<11, "PC">, DwarfRegNum<[11]>;

let SubRegIndices = [sub_lo, sub_hi], CoveredBySubRegs = 1 in {
def BC : I8085Reg<0, "BC", [C, B]>, DwarfRegNum<[12]>;
def DE : I8085Reg<1, "DE", [E, D]>, DwarfRegNum<[13]>;
def HL : I8085Reg<2, "HL", [L, H]>, DwarfRegNum<[14]>;
}

def GPRSP : RegisterClass<"I8085", [i16], 8, (add SP)>;

def SREG : I8085Reg<8, "FLAGS">, DwarfRegNum<[9]>;

def CCR : RegisterClass<"I8085", [i8], 8, (add SREG)> {
  let CopyCost = -1; // Don't allow copying of status registers
}

let SubRegIndices = [sub_lo, sub_hi], CoveredBySubRegs = 1 in {
def PSW : I8085Reg<3, "PSW", [SREG, A]>, DwarfRegNum<[10]>;
}

//===----------------------------------------------------------------------===//
// 32-bit imaginary registers, backed by per-function scratch storage.
//===----------------------------------------------------------------------===//

def IAX : I8085Reg<9, "IAX">, DwarfRegNum<[15]>;
def IBX : I8085Reg<10, "IBX">, DwarfRegNum<[16]>;

def GR8 : RegisterClass<"I8085", [i8], 8,
                        (
                            add A,B,C,D,E,H,L,M
                            )>; 

// 8-bit GPRs that are safe to use when HL is needed as an address register.
def GR8NoHL : RegisterClass<"I8085", [i8], 8,
                        (
                            add A,B,C,D,E
                            )>;

// 8-bit GPRs that avoid A/HL for condition values across GR32 pseudos.
def GR8NoAHL : RegisterClass<"I8085", [i8], 8,
                        (
                            add B,C,D,E
                            )>;

def GR16 : RegisterClass<"I8085", [i16], 8,
                        (
                            add BC,DE,HL,SP
                        )>;

// The addressable 16-bit pairs only (no SP). Because every member has sub_lo /
// sub_hi sub-registers, this class supports byte extraction via subreg COPY,
// which the full GR16 (which includes SP) does not. Used for fused i16 compares.
def GR16Pair : RegisterClass<"I8085", [i16], 8,
                        (
                            add BC,DE,HL
                        )>;

def GR16BD : RegisterClass<"I8085", [i16], 8,
                        (
                            add BC,DE
                        )>;

def GR16BDSP : RegisterClass<"I8085", [i16], 8,
                        (
                            add BC,DE,SP
                        )>;

def GR16PP : RegisterClass<"I8085", [i16], 8,
                        (
                            add BC,DE,HL,PSW
                        )>;

def GR32 : RegisterClass<"I8085", [i32], 8,
                        (
                            add IAX, IBX
                        )>;                                              
