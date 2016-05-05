#include "transform.h"
#include <iterator>
#include <cstdlib>

#include <llvm/MC/MCInstrInfo.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;
using namespace MCID;

// return if two opcodes are exchangeable
static bool isExchangeable(const MCInstrInfo *MII, unsigned A, unsigned B) {
  const auto &DescA = MII->get(A), &DescB = MII->get(B);
  if (DescA.NumOperands != DescB.NumOperands)
    return false;

  for (unsigned i = 0; i < DescA.NumOperands; i++) {
    const auto &OpA = DescA.OpInfo[i], &OpB = DescB.OpInfo[i];
    if (OpA.OperandType != OpB.OperandType)
      return false;

    if (OpA.OperandType == MCOI::OPERAND_REGISTER &&
        OpA.RegClass != OpB.RegClass)
      return false;
  }

  return true;
}

static unsigned choose(unsigned NumChoice) {
  assert(NumChoice > 0);
  return (unsigned)(rand() % (int)NumChoice);
}


/////// cheat
// check if an opcode is supported
static bool isSupported(const MCInstrInfo *MII, unsigned Opc) {
  std::string Name = MII->getName(Opc);
  return Name.find("ADD32") == 0 ||
    Name.find("ADD64") == 0 ||
    Name.find("SUB32") == 0 ||
    Name.find("SUB64") == 0 ||
    Name.find("DIV32") == 0 ||
    Name.find("DIV64") == 0 ||
    Name.find("MUL32") == 0 ||
    Name.find("MUL64") == 0 ||
    Name == "MOV64mr" ||
    Name == "MOV64rr" ||
    Name == "MOV64rm" ||
    Name == "MOV32mr" ||
    Name == "MOV32rr" ||
    Name == "MOV32rm";
}

const TargetRegisterClass *
Transformation::getRegClass(const MCOperandInfo &Op) {
  assert(Op.RegClass >= 0);
  const TargetRegisterClass *RC;
  if (Op.isLookupPtrRegClass()) {
    RC = TRI->getPointerRegClass(*MF, Op.RegClass);
  } else {
    RC = TRI->getRegClass(Op.RegClass);
  }
  assert(RC && "unable to find regclass");
  return RC;
}

// cleans up memory
void Transformation::Accept() {
  switch(PrevTransformation) {
  case NOP:
  case INSERT:
  case SWAP:
  case MOVE:
    break;
  case MUT_OPCODE:
  case MUT_OPERAND:
  case REPLACE: {
    MF->DeleteMachineInstr(Old);
    break;
  }
  case DELETE: { 
    MF->DeleteMachineInstr(New);
  }
  } 
}

void Transformation::Undo() {
  switch (PrevTransformation) {
  case NOP:
    break;
  case MUT_OPCODE:
  case MUT_OPERAND:
  case REPLACE: {
    auto &MBB = *New->getParent();
    replaceInst(MBB, New, Old, true);
    break;
  }

  case INSERT: {
    New->removeFromParent();
    MF->DeleteMachineInstr(New);
    break;
  }

  case SWAP: {
    doSwap(Swapped1, Swapped2);
    break;
  }

  case MOVE:
    New->removeFromParent();

  // undo MOVE and DELETE by inserting `New` back
  // to its previous position
  case DELETE:
    if (NextLoc == Parent->instr_end()) {
      Parent->push_back(New);
    } else {
      Parent->insert(NextLoc, New);
    }
    break;
  }

  if (PrevTransformation == INSERT) {
    NumInstrs--;
  } else if (PrevTransformation == DELETE) { 
    NumInstrs++;
  }
}

Transformation::InstrIterator
Transformation::select(Transformation::InstrIterator Except, bool IncludeEnd) {
  InstrIterator Selected;
  auto &MBB = *MF->begin();

  assert(NumInstrs >= 1 || MBB.instr_begin() != Except);
  auto Begin = MBB.instr_begin();

  do {
    unsigned Idx = IncludeEnd ?
      choose(NumInstrs+1)
      : choose(NumInstrs);

    Selected = Begin;
    std::advance(Selected, Idx);
  } while (Selected == Except);

  return Selected;
}

void Transformation::doSwap(InstrIterator &A, InstrIterator &B) {
  if (A == B)
    return;

  auto &MBB = *A->getParent();
  assert(A != MBB.instr_end() && B != MBB.instr_end()
         && "can't swap with `MBB.instr_end()`");
  InstrIterator AA = MF->CloneMachineInstr(A),
                BB = MF->CloneMachineInstr(B);
  replaceInst(MBB, A, BB, true);
  replaceInst(MBB, B, AA, true);
  A = AA;
  B = BB;
}

static bool hasUnknown(const MCInstrDesc &Desc) {
  for (unsigned i = 0; i < Desc.NumOperands; i++) {
    if (Desc.OpInfo[i].OperandType == MCOI::OPERAND_UNKNOWN ||
        Desc.OpInfo[i].OperandType == MCOI::OPERAND_MEMORY)
      return true;
  }

  return false;
}

void Transformation::buildOpcodeClasses() {
  for (unsigned i = 0; i < TII->getNumOpcodes(); i++) {
    const auto &Opcode = TII->get(i);
    if (hasUnknown(Opcode) || Opcode.isPseudo() || Opcode.isBranch() ||
        Opcode.isReturn() || Opcode.isCall() || Opcode.isVariadic() ||
        !isSupported(TII, i))
      continue;

    OpcodeClasses.insert(i);

    for (auto Itr = OpcodeClasses.begin(), End = OpcodeClasses.end();
         Itr != End; Itr++) {
      unsigned Opc = *OpcodeClasses.findLeader(Itr);
      if (isExchangeable(TII, Opc, i)) {
        OpcodeClasses.unionSets(Opc, i);
        break;
      }
    }
  }
}

bool Transformation::Swap() {
  if (NumInstrs < 2) {
    PrevTransformation = NOP;
    return false;
  }

  auto &MBB = *MF->begin();
  auto A = select(MBB.instr_end()), B = select(A, false);
  doSwap(A, B);

  PrevTransformation = SWAP;
  Swapped1 = A;
  Swapped2 = B;

  return true;
}

bool Transformation::MutateOpcode() {
  if (NumInstrs == 0) {
    PrevTransformation = NOP;
    return false;
  }

  auto &MBB = *MF->begin();
  auto Instr = select(MBB.instr_end());
  unsigned OldOpcode = Instr->getOpcode();

  // select a random but "equivalent" opcode
  errs() << "++++++++ Trying to swap opcode for " << *Instr << "\n";
  auto Opc = OpcodeClasses.member_begin(OpcodeClasses.findValue(
      OpcodeClasses.getLeaderValue(OldOpcode)));
  assert(Opc != OpcodeClasses.member_end());
  unsigned ClassSize = std::distance(Opc, OpcodeClasses.member_end());
  std::advance(Opc, choose(ClassSize));
  unsigned NewOpcode = *Opc;

  // construct new instruction with different opcode
  Old = Instr;
  New = MF->CloneMachineInstr(Instr);
  New->setDesc(TII->get(NewOpcode));
  PrevTransformation = MUT_OPCODE;

  replaceInst(MBB, Old, New);
  return true;
}

bool Transformation::MutateOperand() {
  if (NumInstrs == 0) {
    PrevTransformation = NOP;
    return false;
  }

  auto &MBB = *MF->begin();
  auto Instr = select(MBB.instr_end());

  Old = Instr;
  New = MF->CloneMachineInstr(Instr);
  replaceInst(MBB, Old, New);
  PrevTransformation = MUT_OPERAND;

  const auto &Desc = Instr->getDesc();
  // select which operand to mutate
  if (Desc.NumOperands == 0) {
    PrevTransformation = NOP;
    return false;
  }

  unsigned OpIdx = choose(Desc.NumOperands);
  const auto &OpInfo = Desc.OpInfo[OpIdx];
  auto &Operand = New->getOperand(OpIdx);
  randOperand(Operand, OpInfo);

  // two address code
  if (Desc.getOperandConstraint(0, MCOI::TIED_TO) &&
      New->getOperand(0).isReg() &&
      New->getOperand(1).isReg()) {
    if (OpIdx == 0) {
      New->getOperand(1).setReg(New->getOperand(0).getReg());
    } else if (OpIdx == 1) {
      New->getOperand(0).setReg(New->getOperand(1).getReg());
    }
  }

  return true;
}

void Transformation::randOperand(MachineOperand &Operand,
                                 const MCOperandInfo &OpInfo) {
  if (Operand.isImm()) {
    int64_t NewImm = Immediates[choose(Immediates.size())];
    if (OpInfo.OperandType == MCOI::OPERAND_MEMORY) {
      NewImm = std::abs(NewImm);
    }
    Operand.setImm(NewImm);
  } else {
    assert(Operand.isReg());
    const auto *RC = getRegClass(OpInfo);
    unsigned NewReg = RC->getRegister(choose(RC->getNumRegs()));
    Operand.setReg(NewReg);
  }
}

unsigned Transformation::chooseNonBranchOpcode() {
  const MCInstrDesc *Desc;
  unsigned NumOpcodes = MII->getNumOpcodes();

  do {
    Desc = &MII->get(choose(NumOpcodes));

  } while (Desc->isBranch() || hasUnknown(*Desc) || Desc->isPseudo() ||
           Desc->isReturn() || Desc->isCall() || Desc->isVariadic() ||
           !isSupported(MII, Desc->Opcode));

  return Desc->getOpcode();
}

MachineInstr *Transformation::randInstr() {
  unsigned Opc = chooseNonBranchOpcode();
  const auto &Desc = MII->get(Opc);
  MachineInstr *New = MF->CreateMachineInstr(Desc, DebugLoc());

  errs() << "Creating random instruction: `" << MII->getName(Opc) << " ";
  // fill the instruction with operands
  for (unsigned i = 0; i < Desc.NumOperands; i++) {
    const auto &OpInfo = Desc.OpInfo[i];
    auto IsDef = i < Desc.NumDefs;
    MachineOperand Op = MachineOperand::CreateImm(42);
    switch (OpInfo.OperandType) {
    case MCOI::OPERAND_PCREL:
    case MCOI::OPERAND_FIRST_TARGET:
    case MCOI::OPERAND_IMMEDIATE: {
      errs() << "imm, ";
      Op = MachineOperand::CreateImm(0);
      break;
    }

    case MCOI::OPERAND_REGISTER: {
      errs() << "register, ";
      Op = MachineOperand::CreateReg(1, IsDef);
      break;
    }

    case MCOI::OPERAND_MEMORY: {
      errs() << "memory";
      errs() << "(";
      if (OpInfo.RegClass < 0) {
        errs() << "imm), ";
      } else {
        errs() << "reg), ";
      }
      Op = OpInfo.RegClass < 0 ? MachineOperand::CreateImm(0)
                               : MachineOperand::CreateReg(1, IsDef);
      break;
    }

    default:
      llvm_unreachable("unkown operand type");
    }

    randOperand(Op, OpInfo);
    New->addOperand(*MF, Op);
  }

  // two address code
  if (Desc.getOperandConstraint(0, MCOI::TIED_TO)) {
    auto &Dest = New->getOperand(0), 
         &Src = New->getOperand(1);
    if (Dest.isReg() && Src.isReg())
      Src.setReg(Dest.getReg());
  }

  errs() << "`\n";

  return New;
}

bool Transformation::Replace() {
  if (NumInstrs == 0) {
    PrevTransformation = NOP;
    return false;
  }

  New = randInstr();
  auto &MBB = *MF->begin();
  Old = select(MBB.instr_end());
  replaceInst(MBB, Old, New);
  PrevTransformation = REPLACE;
  return true;
}

bool Transformation::Move() { 
  if (NumInstrs == 1) {
    PrevTransformation = NOP;
    return false;
  }

  auto &MBB = *MF->begin();

  auto Instr = select(MBB.instr_end());
  NextLoc = Instr->getNextNode();
  
  // move
  InstrIterator NewLoc;
  do {
    NewLoc = select(Instr->getNextNode());
  } while (NewLoc == Instr);

  Instr->removeFromParent();
  if (NewLoc != MBB.instr_end()) {
    MBB.insert(NewLoc, Instr);
  } else {
    MBB.push_back(Instr);
  }

  New = Instr;
  Parent = New->getParent();
  PrevTransformation = MOVE;

  return true;
}

bool Transformation::Insert() {
  New = randInstr();
  auto &MBB = *MF->begin();

  if (NumInstrs == 0) {
    MBB.push_back(New);
  } else {
    auto InsertPt = select(MBB.instr_end());
    MBB.insert(InsertPt, New);
  }

  PrevTransformation = INSERT;

  NumInstrs++;
  return true;
}

bool Transformation::Delete() {
  if (NumInstrs == 0) {
    PrevTransformation = NOP;
    return false;
  }

  auto &MBB = *MF->begin();
  New = select(MBB.instr_end());

  NextLoc = New;
  ++NextLoc;
  Parent = New->getParent();
  New->removeFromParent();
  PrevTransformation = DELETE;

  NumInstrs--;
  return true;
}
