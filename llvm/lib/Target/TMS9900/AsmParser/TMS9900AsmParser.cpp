//===-- TMS9900AsmParser.cpp - Parse TMS9900 assembly to MCInst ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/TMS9900MCTargetDesc.h"
#include "TargetInfo/TMS9900TargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/StringSaver.h"

using namespace llvm;

#define DEBUG_TYPE "tms9900-asm-parser"

namespace {

/// TMS9900Operand - Instances of this class represent a parsed TMS9900
/// machine instruction operand.
class TMS9900Operand : public MCParsedAsmOperand {
public:
  enum KindTy {
    k_Tok,        // Token (mnemonic)
    k_Reg,        // Register (R0-R15)
    k_Imm,        // Immediate value
    k_Mem,        // Memory: @symbol or @offset(Rn)
    k_IndReg,     // Indirect: *Rn
    k_PostIndReg  // Post-increment: *Rn+
  };

private:
  KindTy Kind;
  SMLoc StartLoc, EndLoc;

  struct MemOp {
    unsigned RegNum;
    const MCExpr *Offset;
  };

  union {
    StringRef Tok;
    unsigned RegNum;
    const MCExpr *Imm;
    MemOp Mem;
  };

public:
  TMS9900Operand(KindTy K, SMLoc S, SMLoc E) : Kind(K), StartLoc(S), EndLoc(E) {}

  // Token operand
  static std::unique_ptr<TMS9900Operand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<TMS9900Operand>(k_Tok, S, S);
    Op->Tok = Str;
    return Op;
  }

  // Register operand
  static std::unique_ptr<TMS9900Operand> createReg(unsigned RegNum, SMLoc S,
                                                    SMLoc E) {
    auto Op = std::make_unique<TMS9900Operand>(k_Reg, S, E);
    Op->RegNum = RegNum;
    return Op;
  }

  // Immediate operand
  static std::unique_ptr<TMS9900Operand> createImm(const MCExpr *Val, SMLoc S,
                                                    SMLoc E) {
    auto Op = std::make_unique<TMS9900Operand>(k_Imm, S, E);
    Op->Imm = Val;
    return Op;
  }

  // Memory operand (@symbol or @offset(Rn))
  static std::unique_ptr<TMS9900Operand> createMem(unsigned RegNum,
                                                    const MCExpr *Offset,
                                                    SMLoc S, SMLoc E) {
    auto Op = std::make_unique<TMS9900Operand>(k_Mem, S, E);
    Op->Mem.RegNum = RegNum;
    Op->Mem.Offset = Offset;
    return Op;
  }

  // Indirect register operand (*Rn)
  static std::unique_ptr<TMS9900Operand> createIndReg(unsigned RegNum, SMLoc S,
                                                       SMLoc E) {
    auto Op = std::make_unique<TMS9900Operand>(k_IndReg, S, E);
    Op->RegNum = RegNum;
    return Op;
  }

  // Post-increment indirect operand (*Rn+)
  static std::unique_ptr<TMS9900Operand> createPostIndReg(unsigned RegNum,
                                                           SMLoc S, SMLoc E) {
    auto Op = std::make_unique<TMS9900Operand>(k_PostIndReg, S, E);
    Op->RegNum = RegNum;
    return Op;
  }

  // Accessors
  bool isToken() const override { return Kind == k_Tok; }
  bool isReg() const override { return Kind == k_Reg; }
  bool isImm() const override { return Kind == k_Imm; }
  bool isMem() const override { return Kind == k_Mem; }
  bool isIndReg() const { return Kind == k_IndReg; }
  bool isPostIndReg() const { return Kind == k_PostIndReg; }

  StringRef getToken() const {
    assert(Kind == k_Tok && "Invalid access!");
    return Tok;
  }

  unsigned getReg() const override {
    assert((Kind == k_Reg || Kind == k_IndReg || Kind == k_PostIndReg) &&
           "Invalid access!");
    return RegNum;
  }

  // Type checking methods required by TableGen-generated code
  bool isTMS9900Imm() const { return Kind == k_Imm; }
  bool isTMS9900Imm4() const {
    if (Kind != k_Imm)
      return false;
    int64_t Value;
    if (!Imm->evaluateAsAbsolute(Value))
      return false;
    return isUInt<4>(Value);
  }
  bool isTMS9900CRUCount() const {
    if (Kind != k_Imm)
      return false;
    int64_t Value;
    if (!Imm->evaluateAsAbsolute(Value))
      return false;
    return isUInt<4>(Value);
  }
  bool isTMS9900CRUDisp() const {
    if (Kind != k_Imm)
      return false;
    int64_t Value;
    if (!Imm->evaluateAsAbsolute(Value))
      return true;
    return isInt<8>(Value);
  }
  bool isTMS9900Mem() const { return Kind == k_Mem; }
  bool isTMS9900IndReg() const { return Kind == k_IndReg; }
  bool isTMS9900PostIndReg() const { return Kind == k_PostIndReg; }
  bool isTMS9900BrTarget() const { return Kind == k_Imm; }

  const MCExpr *getImm() const {
    assert(Kind == k_Imm && "Invalid access!");
    return Imm;
  }

  unsigned getMemReg() const {
    assert(Kind == k_Mem && "Invalid access!");
    return Mem.RegNum;
  }

  const MCExpr *getMemOffset() const {
    assert(Kind == k_Mem && "Invalid access!");
    return Mem.Offset;
  }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  void print(raw_ostream &OS) const override {
    switch (Kind) {
    case k_Tok:
      OS << "Token: " << Tok;
      break;
    case k_Reg:
      OS << "Reg: " << RegNum;
      break;
    case k_Imm:
      OS << "Imm: " << *Imm;
      break;
    case k_Mem:
      OS << "Mem: @";
      if (Mem.Offset)
        OS << *Mem.Offset;
      if (Mem.RegNum)
        OS << "(R" << Mem.RegNum << ")";
      break;
    case k_IndReg:
      OS << "IndReg: *R" << RegNum;
      break;
    case k_PostIndReg:
      OS << "PostIndReg: *R" << RegNum << "+";
      break;
    }
  }

  // Add operands to MCInst
  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addExprOperand(MCInst &Inst, const MCExpr *Expr) const {
    // Add as immediate when possible
    if (!Expr)
      Inst.addOperand(MCOperand::createImm(0));
    else if (const MCConstantExpr *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    addExprOperand(Inst, getImm());
  }

  void addMemOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getMemReg()));
    addExprOperand(Inst, getMemOffset());
  }

  void addIndRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addPostIndRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }

  void addBrTargetOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands!");
    if (Kind == k_Imm)
      Inst.addOperand(MCOperand::createExpr(getImm()));
    else
      llvm_unreachable("Invalid branch target operand kind");
  }
};

class TMS9900AsmParser : public MCTargetAsmParser {
public:
  // Custom match result for invalid immediate
  enum TMS9900MatchResultTy {
    Match_InvalidImm = FIRST_TARGET_MATCH_RESULT_TY,
  };

private:
  const MCSubtargetInfo &STI;
  MCAsmParser &Parser;
  const MCRegisterInfo *MRI;
  BumpPtrAllocator Allocator;
  StringSaver Saver{Allocator};

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;

  // Custom operand parsing
  ParseStatus parseOperand(OperandVector &Operands, StringRef Mnemonic);
  ParseStatus parseMemOperand(OperandVector &Operands);
  ParseStatus parseIndirectOperand(OperandVector &Operands);
  ParseStatus parsePostIndirectOperand(OperandVector &Operands);
  ParseStatus parseBranchTarget(OperandVector &Operands);

  // Helper methods
  bool parseRegisterName(MCRegister &RegNo);
  bool parseExpression(const MCExpr *&Expr);
  bool isXas99Dialect() const;

  // xas99 directive handlers
  bool parseDirectiveDATA();
  bool parseDirectiveBYTE();
  bool parseDirectiveTEXT();
  bool parseDirectiveBSS();
  bool parseDirectiveDEF();
  bool parseDirectiveREF();
  bool parseDirectiveAORG();
  bool parseDirectiveEQU(SMLoc NameLoc);

  // Helper to check if a name is a known instruction mnemonic or directive
  bool isKnownMnemonic(StringRef Name) const;
  bool isKnownDirective(StringRef Name) const;

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

// Auto-generated instruction matching functions
#define GET_ASSEMBLER_HEADER
#include "TMS9900GenAsmMatcher.inc"

public:
  TMS9900AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                    const MCInstrInfo &MII, const MCTargetOptions &Options)
      : MCTargetAsmParser(Options, STI, MII), STI(STI), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    MRI = getContext().getRegisterInfo();
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "TMS9900GenAsmMatcher.inc"

bool TMS9900AsmParser::isXas99Dialect() const {
  // Check if we're using xas99 dialect
  // This would be set via -mllvm -tms9900-asm-dialect=xas99
  return false; // Default to LLVM dialect for now
}

bool TMS9900AsmParser::isKnownDirective(StringRef Name) const {
  return Name.equals_insensitive("DATA") ||
         Name.equals_insensitive("BYTE") ||
         Name.equals_insensitive("TEXT") ||
         Name.equals_insensitive("BSS") ||
         Name.equals_insensitive("DEF") ||
         Name.equals_insensitive("REF") ||
         Name.equals_insensitive("AORG") ||
         Name.equals_insensitive("EQU") ||
         Name.equals_insensitive("END");
}

bool TMS9900AsmParser::isKnownMnemonic(StringRef Name) const {
  // TMS9900 instruction mnemonics (case-insensitive)
  return StringSwitch<bool>(Name.upper())
    // Format 1: Dual operand (MOV, ADD, etc.)
    .Case("A", true)
    .Case("AB", true)
    .Case("C", true)
    .Case("CB", true)
    .Case("S", true)
    .Case("SB", true)
    .Case("SOC", true)
    .Case("SOCB", true)
    .Case("SZC", true)
    .Case("SZCB", true)
    .Case("MOV", true)
    .Case("MOVB", true)
    .Case("COC", true)
    .Case("CZC", true)
    .Case("XOR", true)
    // Format 2: Jump/CRU
    .Case("JMP", true)
    .Case("JLT", true)
    .Case("JLE", true)
    .Case("JEQ", true)
    .Case("JHE", true)
    .Case("JGT", true)
    .Case("JNE", true)
    .Case("JNC", true)
    .Case("JOC", true)
    .Case("JNO", true)
    .Case("JL", true)
    .Case("JH", true)
    .Case("JOP", true)
    .Case("SBO", true)
    .Case("SBZ", true)
    .Case("TB", true)
    // Format 3: Logical
    .Case("XOP", true)
    .Case("LDCR", true)
    .Case("STCR", true)
    .Case("MPY", true)
    .Case("DIV", true)
    // Format 4: CRU multi-bit
    // Format 5: Register shift
    .Case("SRA", true)
    .Case("SRL", true)
    .Case("SLA", true)
    .Case("SRC", true)
    // Format 6: Single operand
    .Case("BLWP", true)
    .Case("B", true)
    .Case("X", true)
    .Case("CLR", true)
    .Case("NEG", true)
    .Case("INV", true)
    .Case("INC", true)
    .Case("INCT", true)
    .Case("DEC", true)
    .Case("DECT", true)
    .Case("BL", true)
    .Case("SWPB", true)
    .Case("SETO", true)
    .Case("ABS", true)
    // Format 7: Control
    .Case("RTWP", true)
    .Case("IDLE", true)
    .Case("RSET", true)
    .Case("CKOF", true)
    .Case("CKON", true)
    .Case("LREX", true)
    // Format 8: Immediate
    .Case("LI", true)
    .Case("AI", true)
    .Case("ANDI", true)
    .Case("ORI", true)
    .Case("CI", true)
    // Format 9: Immediate extended
    .Case("LWPI", true)
    .Case("LIMI", true)
    .Case("STST", true)
    .Case("STWP", true)
    // Pseudo-ops
    .Case("NOP", true)
    .Case("RT", true)
    .Default(false);
}

bool TMS9900AsmParser::parseRegisterName(MCRegister &RegNo) {
  StringRef Name = Parser.getTok().getIdentifier();

  // Try case-insensitive register matching
  // Convert to uppercase since register names are defined as R0, R1, etc.
  RegNo = MatchRegisterName(Name.upper());
  if (RegNo == TMS9900::NoRegister) {
    // Try alternate names
    RegNo = MatchRegisterAltName(Name.upper());
  }

  return RegNo != TMS9900::NoRegister;
}

bool TMS9900AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                      SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return true;
  return false;
}

ParseStatus TMS9900AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                                SMLoc &EndLoc) {
  StartLoc = Parser.getTok().getLoc();

  if (Parser.getTok().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  if (!parseRegisterName(Reg))
    return ParseStatus::NoMatch;

  EndLoc = Parser.getTok().getEndLoc();
  Parser.Lex(); // Consume the register token
  return ParseStatus::Success;
}

bool TMS9900AsmParser::parseExpression(const MCExpr *&Expr) {
  // Check for xas99 hex format: >XXXX
  if (Parser.getTok().is(AsmToken::Greater)) {
    Parser.Lex(); // Consume '>'

    if (Parser.getTok().isNot(AsmToken::Integer) &&
        Parser.getTok().isNot(AsmToken::Identifier)) {
      return Error(Parser.getTok().getLoc(), "expected hex value after '>'");
    }

    // Parse the hex value - handle cases where lexer splits it
    // e.g., ">8C00" becomes tokens: ">", "8", "C00"
    std::string HexStr;

    // First part (could be integer or identifier)
    HexStr = Parser.getTok().getString().str();
    Parser.Lex();

    // Check if there's a continuation (identifier immediately following)
    // This handles cases like "8" followed by "C00"
    while (Parser.getTok().is(AsmToken::Identifier)) {
      // Check if the identifier could be hex continuation (starts with hex digit)
      StringRef NextStr = Parser.getTok().getString();
      if (NextStr.empty())
        break;
      char FirstChar = NextStr[0];
      if (!((FirstChar >= '0' && FirstChar <= '9') ||
            (FirstChar >= 'A' && FirstChar <= 'F') ||
            (FirstChar >= 'a' && FirstChar <= 'f')))
        break;
      HexStr += NextStr.str();
      Parser.Lex();
    }

    uint64_t Value;
    if (StringRef(HexStr).getAsInteger(16, Value)) {
      return Error(Parser.getTok().getLoc(), "invalid hex value");
    }

    Expr = MCConstantExpr::create(Value, getContext());
    return false;
  }

  return getParser().parseExpression(Expr);
}

ParseStatus TMS9900AsmParser::parseOperand(OperandVector &Operands,
                                            StringRef Mnemonic) {
  SMLoc StartLoc = Parser.getTok().getLoc();

  // Check for '*' token (indirect addressing prefix)
  if (Parser.getTok().is(AsmToken::Star)) {
    Operands.push_back(TMS9900Operand::createToken("*", StartLoc));
    Parser.Lex(); // Consume '*'

    // Parse the register that follows
    SMLoc RegStart = Parser.getTok().getLoc();
    MCRegister RegNo;
    SMLoc RegEnd;

    if (!tryParseRegister(RegNo, RegStart, RegEnd).isSuccess()) {
      return Error(RegStart, "expected register after '*'");
    }
    Operands.push_back(TMS9900Operand::createReg(RegNo, RegStart, RegEnd));

    // Check for post-increment '+'
    if (Parser.getTok().is(AsmToken::Plus)) {
      SMLoc PlusLoc = Parser.getTok().getLoc();
      Operands.push_back(TMS9900Operand::createToken("+", PlusLoc));
      Parser.Lex(); // Consume '+'
    }
    return ParseStatus::Success;
  }

  // Check for '@' token (memory addressing prefix)
  if (Parser.getTok().is(AsmToken::At)) {
    Operands.push_back(TMS9900Operand::createToken("@", StartLoc));
    Parser.Lex(); // Consume '@'

    // Parse the offset/symbol
    const MCExpr *Expr;
    if (parseExpression(Expr))
      return ParseStatus::Failure;

    SMLoc ExprEnd = Parser.getTok().getLoc();
    Operands.push_back(TMS9900Operand::createImm(Expr, StartLoc, ExprEnd));

    // Check for indexed addressing: (Rn)
    if (Parser.getTok().is(AsmToken::LParen)) {
      SMLoc LParenLoc = Parser.getTok().getLoc();
      Operands.push_back(TMS9900Operand::createToken("(", LParenLoc));
      Parser.Lex(); // Consume '('

      // Parse register
      SMLoc RegStart = Parser.getTok().getLoc();
      MCRegister RegNo;
      SMLoc RegEnd;

      if (!tryParseRegister(RegNo, RegStart, RegEnd).isSuccess()) {
        return Error(RegStart, "expected register in indexed addressing");
      }
      Operands.push_back(TMS9900Operand::createReg(RegNo, RegStart, RegEnd));

      if (Parser.getTok().isNot(AsmToken::RParen)) {
        return Error(Parser.getTok().getLoc(), "expected ')'");
      }
      SMLoc RParenLoc = Parser.getTok().getLoc();
      Operands.push_back(TMS9900Operand::createToken(")", RParenLoc));
      Parser.Lex(); // Consume ')'
    }
    return ParseStatus::Success;
  }

  // Check for register
  if (Parser.getTok().is(AsmToken::Identifier)) {
    MCRegister RegNo;
    SMLoc EndLoc;

    if (tryParseRegister(RegNo, StartLoc, EndLoc).isSuccess()) {
      Operands.push_back(TMS9900Operand::createReg(RegNo, StartLoc, EndLoc));
      return ParseStatus::Success;
    }
  }

  // Check for xas99 hex format: >XXXX
  if (Parser.getTok().is(AsmToken::Greater)) {
    const MCExpr *Expr;
    if (parseExpression(Expr))
      return ParseStatus::Failure;

    SMLoc EndLoc = Parser.getTok().getLoc();
    Operands.push_back(TMS9900Operand::createImm(Expr, StartLoc, EndLoc));
    return ParseStatus::Success;
  }

  // Try to parse as immediate/expression
  const MCExpr *Expr;
  if (!getParser().parseExpression(Expr)) {
    SMLoc EndLoc = Parser.getTok().getLoc();
    Operands.push_back(TMS9900Operand::createImm(Expr, StartLoc, EndLoc));
    return ParseStatus::Success;
  }

  return ParseStatus::NoMatch;
}

ParseStatus TMS9900AsmParser::parseMemOperand(OperandVector &Operands) {
  SMLoc StartLoc = Parser.getTok().getLoc();

  // Consume '@'
  assert(Parser.getTok().is(AsmToken::At) && "Expected '@'");
  Parser.Lex();

  const MCExpr *Offset = nullptr;
  unsigned RegNo = TMS9900::R0; // Default to R0 for symbolic addressing

  // Parse the offset/symbol
  if (parseExpression(Offset))
    return ParseStatus::Failure;

  // Check for indexed addressing: @offset(Rn)
  if (Parser.getTok().is(AsmToken::LParen)) {
    Parser.Lex(); // Consume '('

    SMLoc RegStartLoc = Parser.getTok().getLoc();
    SMLoc RegEndLoc;
    MCRegister Reg;

    if (!tryParseRegister(Reg, RegStartLoc, RegEndLoc).isSuccess()) {
      return Error(RegStartLoc, "expected register in indexed addressing");
    }

    RegNo = Reg;

    if (Parser.getTok().isNot(AsmToken::RParen)) {
      return Error(Parser.getTok().getLoc(), "expected ')'");
    }
    Parser.Lex(); // Consume ')'
  }

  SMLoc EndLoc = Parser.getTok().getLoc();
  Operands.push_back(TMS9900Operand::createMem(RegNo, Offset, StartLoc, EndLoc));
  return ParseStatus::Success;
}

ParseStatus TMS9900AsmParser::parseIndirectOperand(OperandVector &Operands) {
  SMLoc StartLoc = Parser.getTok().getLoc();

  // Consume '*'
  assert(Parser.getTok().is(AsmToken::Star) && "Expected '*'");
  Parser.Lex();

  // Parse register
  SMLoc RegStartLoc = Parser.getTok().getLoc();
  SMLoc RegEndLoc;
  MCRegister RegNo;

  if (!tryParseRegister(RegNo, RegStartLoc, RegEndLoc).isSuccess()) {
    return Error(RegStartLoc, "expected register after '*'");
  }

  // Check for post-increment: *Rn+
  if (Parser.getTok().is(AsmToken::Plus)) {
    Parser.Lex(); // Consume '+'
    SMLoc EndLoc = Parser.getTok().getLoc();
    Operands.push_back(TMS9900Operand::createPostIndReg(RegNo, StartLoc, EndLoc));
    return ParseStatus::Success;
  }

  SMLoc EndLoc = Parser.getTok().getLoc();
  Operands.push_back(TMS9900Operand::createIndReg(RegNo, StartLoc, EndLoc));
  return ParseStatus::Success;
}

ParseStatus TMS9900AsmParser::parsePostIndirectOperand(OperandVector &Operands) {
  // This is called when we specifically expect post-increment
  // Just delegate to parseIndirectOperand which handles both cases
  return parseIndirectOperand(Operands);
}

ParseStatus TMS9900AsmParser::parseBranchTarget(OperandVector &Operands) {
  SMLoc StartLoc = Parser.getTok().getLoc();

  if (Parser.getTok().is(AsmToken::At)) {
    Operands.push_back(TMS9900Operand::createToken("@", StartLoc));
    Parser.Lex(); // Consume '@'
    StartLoc = Parser.getTok().getLoc();
  }

  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return ParseStatus::Failure;

  SMLoc EndLoc = Parser.getTok().getLoc();
  Operands.push_back(TMS9900Operand::createImm(Expr, StartLoc, EndLoc));
  return ParseStatus::Success;
}

bool TMS9900AsmParser::ParseInstruction(ParseInstructionInfo &Info,
                                         StringRef Name, SMLoc NameLoc,
                                         OperandVector &Operands) {
  // Check for xas99-style directives (no '.' prefix)
  // These look like instructions but are actually assembler directives
  if (Name.equals_insensitive("DATA")) {
    return parseDirectiveDATA();
  }
  if (Name.equals_insensitive("BYTE")) {
    return parseDirectiveBYTE();
  }
  if (Name.equals_insensitive("TEXT")) {
    return parseDirectiveTEXT();
  }
  if (Name.equals_insensitive("BSS")) {
    return parseDirectiveBSS();
  }
  if (Name.equals_insensitive("DEF")) {
    return parseDirectiveDEF();
  }
  if (Name.equals_insensitive("REF")) {
    return parseDirectiveREF();
  }
  if (Name.equals_insensitive("AORG")) {
    return parseDirectiveAORG();
  }
  if (Name.equals_insensitive("EQU")) {
    return parseDirectiveEQU(NameLoc);
  }
  if (Name.equals_insensitive("END")) {
    // END directive - just ignore the rest of the file
    return false;
  }

  // xas99-style labels without colons:
  // If Name is not a known mnemonic or directive, treat it as a label
  // Then parse the rest of the line as the actual instruction
  if (!isKnownMnemonic(Name) && !isKnownDirective(Name)) {
    // This looks like a label without a colon (xas99 style)
    // Emit the label with uppercase name for xas99 case-insensitivity
    StringRef LabelName = Saver.save(Name.upper());
    MCSymbol *Sym = getContext().getOrCreateSymbol(LabelName);
    getStreamer().emitLabel(Sym);

    // If there's nothing more on the line, we're done (label-only line)
    if (Parser.getTok().is(AsmToken::EndOfStatement))
      return false;

    // Get the actual mnemonic/directive that follows the label
    if (Parser.getTok().isNot(AsmToken::Identifier)) {
      return Error(Parser.getTok().getLoc(),
                   "expected instruction or directive after label");
    }

    // Get and lowercase the mnemonic to match LLVM's convention
    StringRef RawName = Parser.getTok().getIdentifier();
    Name = Saver.save(RawName.lower());
    NameLoc = Parser.getTok().getLoc();
    Parser.Lex(); // Consume the actual mnemonic

    // Now check if this new Name is a directive
    if (Name.equals_insensitive("DATA")) {
      return parseDirectiveDATA();
    }
    if (Name.equals_insensitive("BYTE")) {
      return parseDirectiveBYTE();
    }
    if (Name.equals_insensitive("TEXT")) {
      return parseDirectiveTEXT();
    }
    if (Name.equals_insensitive("BSS")) {
      return parseDirectiveBSS();
    }
    if (Name.equals_insensitive("DEF")) {
      return parseDirectiveDEF();
    }
    if (Name.equals_insensitive("REF")) {
      return parseDirectiveREF();
    }
    if (Name.equals_insensitive("AORG")) {
      return parseDirectiveAORG();
    }
    if (Name.equals_insensitive("EQU")) {
      // For EQU after a label, we handle it specially
      // The label we just emitted is the symbol being defined
      const MCExpr *Expr;
      if (parseExpression(Expr))
        return true;

      // Reassign the symbol's value
      int64_t Value;
      if (!Expr->evaluateAsAbsolute(Value)) {
        return Error(NameLoc, "EQU value must be absolute");
      }
      Sym->setVariableValue(MCConstantExpr::create(Value, getContext()));
      return false;
    }
    if (Name.equals_insensitive("END")) {
      return false;
    }
    // Continue with instruction parsing below
  }

  // First operand is the mnemonic itself
  Operands.push_back(TMS9900Operand::createToken(Name, NameLoc));

  // If there are no operands, we're done
  if (Parser.getTok().is(AsmToken::EndOfStatement))
    return false;

  // Parse first operand
  if (!parseOperand(Operands, Name).isSuccess()) {
    return Error(Parser.getTok().getLoc(), "unexpected token in operand");
  }

  // Parse additional operands separated by comma
  while (Parser.getTok().is(AsmToken::Comma)) {
    Parser.Lex(); // Consume comma

    if (!parseOperand(Operands, Name).isSuccess()) {
      return Error(Parser.getTok().getLoc(), "unexpected token in operand");
    }
  }

  if (Parser.getTok().isNot(AsmToken::EndOfStatement)) {
    return Error(Parser.getTok().getLoc(),
                 "unexpected token, expected end of statement");
  }

  return false;
}

bool TMS9900AsmParser::MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                                OperandVector &Operands,
                                                MCStreamer &Out,
                                                uint64_t &ErrorInfo,
                                                bool MatchingInlineAsm) {
  // If Operands is empty, a directive was already handled in ParseInstruction
  if (Operands.empty())
    return false;

  MCInst Inst;

  unsigned MatchResult =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (MatchResult) {
  case Match_Success:
    Out.emitInstruction(Inst, STI);
    return false;
  case Match_MnemonicFail:
    return Error(IDLoc, "unrecognized instruction mnemonic");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");

      ErrorLoc = ((TMS9900Operand &)*Operands[ErrorInfo]).getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_MissingFeature:
    return Error(IDLoc, "instruction requires a CPU feature not currently enabled");
  default:
    return Error(IDLoc, "unknown error matching instruction");
  }
}

unsigned TMS9900AsmParser::validateTargetOperandClass(MCParsedAsmOperand &Op,
                                                       unsigned Kind) {
  TMS9900Operand &Operand = (TMS9900Operand &)Op;

  // Handle register class matching
  if (Operand.isReg()) {
    // All our registers are GR16
    return Match_Success;
  }

  return Match_InvalidOperand;
}

ParseStatus TMS9900AsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef IDVal = DirectiveID.getIdentifier();

  // Handle xas99 directives
  if (IDVal.equals_insensitive("DATA"))
    return parseDirectiveDATA() ? ParseStatus::Failure : ParseStatus::Success;
  if (IDVal.equals_insensitive("BYTE"))
    return parseDirectiveBYTE() ? ParseStatus::Failure : ParseStatus::Success;
  if (IDVal.equals_insensitive("BSS"))
    return parseDirectiveBSS() ? ParseStatus::Failure : ParseStatus::Success;
  if (IDVal.equals_insensitive("DEF"))
    return parseDirectiveDEF() ? ParseStatus::Failure : ParseStatus::Success;
  if (IDVal.equals_insensitive("AORG"))
    return parseDirectiveAORG() ? ParseStatus::Failure : ParseStatus::Success;

  // .word is target-dependent; on TMS9900 it emits 16-bit values (same as DATA)
  if (IDVal == ".word")
    return parseDirectiveDATA() ? ParseStatus::Failure : ParseStatus::Success;

  return ParseStatus::NoMatch;
}

bool TMS9900AsmParser::parseDirectiveDATA() {
  // DATA value[,value...]
  // Emit 16-bit words
  do {
    const MCExpr *Expr;
    if (parseExpression(Expr))
      return true;

    getStreamer().emitValue(Expr, 2); // 2 bytes = 16-bit word

  } while (Parser.getTok().is(AsmToken::Comma) && (Parser.Lex(), true));

  return false;
}

bool TMS9900AsmParser::parseDirectiveBYTE() {
  // BYTE value[,value...]
  // Emit 8-bit bytes
  do {
    const MCExpr *Expr;
    if (parseExpression(Expr))
      return true;

    getStreamer().emitValue(Expr, 1); // 1 byte

  } while (Parser.getTok().is(AsmToken::Comma) && (Parser.Lex(), true));

  return false;
}

bool TMS9900AsmParser::parseDirectiveBSS() {
  // BSS size
  // Reserve size bytes of zero-filled space
  const MCExpr *Expr;
  if (parseExpression(Expr))
    return true;

  int64_t Size;
  if (!Expr->evaluateAsAbsolute(Size)) {
    return Error(Parser.getTok().getLoc(), "BSS size must be absolute");
  }

  getStreamer().emitZeros(Size);
  return false;
}

bool TMS9900AsmParser::parseDirectiveDEF() {
  // DEF symbol[,symbol...]
  // Export symbols (make them global)
  do {
    if (Parser.getTok().isNot(AsmToken::Identifier))
      return Error(Parser.getTok().getLoc(), "expected symbol name");

    StringRef Name = Parser.getTok().getIdentifier();
    MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
    getStreamer().emitSymbolAttribute(Sym, MCSA_Global);

    Parser.Lex(); // Consume symbol

  } while (Parser.getTok().is(AsmToken::Comma) && (Parser.Lex(), true));

  return false;
}

bool TMS9900AsmParser::parseDirectiveAORG() {
  // AORG address
  // Set assembly origin (absolute origin)
  const MCExpr *Expr;
  if (parseExpression(Expr))
    return true;

  int64_t Address;
  if (!Expr->evaluateAsAbsolute(Address)) {
    return Error(Parser.getTok().getLoc(), "AORG address must be absolute");
  }

  // Create an absolute section at this address
  // Note: This is a simplified implementation
  // Full support would require section management

  return false;
}

bool TMS9900AsmParser::parseDirectiveTEXT() {
  // TEXT 'string' or TEXT "string"
  // Emit ASCII text bytes
  if (Parser.getTok().isNot(AsmToken::String)) {
    return Error(Parser.getTok().getLoc(), "expected string after TEXT");
  }

  StringRef Str = Parser.getTok().getStringContents();
  Parser.Lex(); // Consume string

  // Emit each character as a byte
  for (char C : Str) {
    getStreamer().emitInt8(C);
  }

  return false;
}

bool TMS9900AsmParser::parseDirectiveREF() {
  // REF symbol[,symbol...]
  // Import external symbols (like .extern)
  do {
    if (Parser.getTok().isNot(AsmToken::Identifier)) {
      return Error(Parser.getTok().getLoc(), "expected symbol name after REF");
    }

    StringRef SymName = Parser.getTok().getIdentifier();
    MCSymbol *Sym = getContext().getOrCreateSymbol(SymName);
    // Mark as external/undefined - the linker will resolve it
    (void)Sym; // Symbol is created, linker will handle it
    Parser.Lex(); // Consume symbol

  } while (Parser.getTok().is(AsmToken::Comma) && (Parser.Lex(), true));

  return false;
}

bool TMS9900AsmParser::parseDirectiveEQU(SMLoc NameLoc) {
  // label EQU value
  // Note: The label was already parsed before we got here
  // This is called when we see "EQU" as the "instruction"
  // The label should have been parsed by the main parser loop

  // Parse the value
  const MCExpr *Expr;
  if (parseExpression(Expr))
    return true;

  // For EQU, we need the label that preceded this directive
  // Unfortunately, the label is handled by the main parser before ParseInstruction
  // We'll emit a warning - proper EQU support requires parser integration
  return Error(NameLoc, "EQU directive requires label (use: LABEL EQU value)");
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeTMS9900AsmParser() {
  RegisterMCAsmParser<TMS9900AsmParser> X(getTheTMS9900Target());
}
