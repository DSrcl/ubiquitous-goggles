#include "transform.h"
#include <iterator>
#include <cstdlib>

#include <llvm/MC/MCInstrInfo.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

// return if two opcodes are exchangeable
static bool isExchangeable(const MCInstrInfo *MII, unsigned A, unsigned B)
{
  const auto &DescA = MII->get(A), &DescB = MII->get(B);
  if (DescA.NumOperands != DescB.NumOperands) return false;
  
  for (unsigned i = 0; i < DescA.NumOperands; i++) {
    const auto &OpA = DescA.OpInfo[i], &OpB = DescB.OpInfo[i];
    if (OpA.OperandType != OpB.OperandType) return false;

    if (OpA.OperandType == MCOI::OPERAND_REGISTER &&
        OpA.RegClass != OpB.RegClass) return false;
  }

  return true;
}

static unsigned choose(unsigned NumChoice)
{
  return (unsigned)(rand() % (int)NumChoice);
}

void Transformation::Undo()
{
  switch (PrevTransformation) {
  case MUT_OPCODE:
  case MUT_OPERAND:
  case REPLACE: {
    auto *MBB = New->getParent();
    MBB->insert(InstrIterator(New), Old);
    New->eraseFromParent();
    break; 
  }

  case INSERT: {
    New->eraseFromParent();
    break;
  }

  case SWAP: {
    doSwap(Swapped1, Swapped2);
    break;
  }

  case MOVE:
    New->removeFromParent();

    // undo MOVE and DELET by inserting the instruction back
    // to its previous position
  case DELETE:
    auto *MBB = New->getParent();
    if (NextLoc == MBB->end()) {
      MBB->push_back(New);
    } else {
      MBB->insert(NextLoc, New);
    }
    break;
  }
}

Transformation::InstrIterator Transformation::select(Transformation::InstrIterator Except)
{
  InstrIterator Selected;
  auto &MBB = *MF->begin();

  assert(NumInstrs >= 1 || MBB.begin() != Except);

  do {
    unsigned Idx = choose(NumInstrs);
    Selected = MBB.instr_begin();
    std::advance(Selected, Idx);
  } while (Selected == Except);

  return Selected;
}

void Transformation::doSwap(InstrIterator A, InstrIterator B)
{
  if (A == B) return;

  // magic?
  std::swap(A, B);
}

void Transformation::buildOpcodeClasses()
{ 
  for (unsigned i = 0; i < MII->getNumOpcodes(); i++) {
    OpcodeClasses.insert(i);

    for (auto Itr = OpcodeClasses.begin(), End = OpcodeClasses.end();
         Itr != End;
         Itr++) {
      unsigned Opc = *OpcodeClasses.findLeader(Itr);
      if (MII->get(Opc).isBranch()) continue;
      if (isExchangeable(MII, Opc, i)) { 
        OpcodeClasses.unionSets(Opc, i);
        break;
      }
    }
  }
}

bool Transformation::Swap()
{
  if (NumInstrs < 2) return false;

  auto &MBB = *MF->begin();
  auto A = select(MBB.instr_end()),
       B = select(A);
  doSwap(A, B);

  PrevTransformation = SWAP;
  Swapped1 = A;
  Swapped2 = B;

  return true;
}

bool Transformation::MutateOpcode()
{
  if (NumInstrs == 0) return false;

  auto &MBB = *MF->begin();
  auto Instr = select(MBB.instr_end());
  unsigned OldOpcode = Instr->getOpcode();

  // select a random but "equivalent" opcode
  auto Opc = OpcodeClasses.member_begin(
    OpcodeClasses.findValue(OldOpcode)); 
  unsigned ClassSize = std::distance(Opc, OpcodeClasses.member_end());
  std::advance(Opc, choose(ClassSize)); 
  unsigned NewOpcode = *Opc;

  // construct new instruction with different opcode
  Old = Instr;
  New = MF->CloneMachineInstr(Instr);
  New->setDesc(MII->get(NewOpcode));
  PrevTransformation = MUT_OPCODE;

  replaceInst(MBB, Old, New);
  return true;
}

bool Transformation::MutateOperand()
{
  if (NumInstrs == 0) return false;

  auto &MBB = *MF->begin();
  auto Instr = select(MBB.instr_end());

  Old = Instr;
  New = MF->CloneMachineInstr(Instr);
  replaceInst(MBB, Old, New);
  PrevTransformation = MUT_OPERAND;

  const auto &Desc = Instr->getDesc(); 
  // select which operand to mutate
  unsigned OpIdx = choose(Desc.NumOperands);
  const auto &OpInfo = Desc.OpInfo[OpIdx];
  auto &Operand = New->getOperand(OpIdx);
  randOperand(Operand, OpInfo);

  return true;
}

void Transformation::randOperand(MachineOperand &Operand, const MCOperandInfo &OpInfo)
{
  if (Operand.isImm()) {
    auto NewImm = Immediates[choose(Immediates.size())];
    Operand.setImm(NewImm);
  } else {
    assert(Operand.isReg());
    const auto &RC = MRI->getRegClass(OpInfo.RegClass);
    unsigned NewReg = RC.getRegister(choose(RC.getNumRegs()));
    Operand.setReg(NewReg);
  }
}

unsigned Transformation::chooseNonBranchOpcode()
{
  const MCInstrDesc *Desc;
  unsigned NumOpcodes = MII->getNumOpcodes();

  do {
    Desc = &MII->get(choose(NumOpcodes));
  } while (Desc->isBranch());

  return Desc->getOpcode();
}

MachineInstr *Transformation::randInstr()
{
  unsigned Opc = chooseNonBranchOpcode();
  const auto &Desc = MII->get(Opc);
  MachineInstr *New = MF->CreateMachineInstr(Desc, DebugLoc());

  // fill the instruction with operands
  for (unsigned i = 0; i < Desc.NumOperands; i++) {
    const auto &OpInfo = Desc.OpInfo[i];
    MachineOperand Op = MachineOperand::CreateImm(42);
    switch (OpInfo.OperandType) {
    case MCOI::OPERAND_PCREL:
    case MCOI::OPERAND_FIRST_TARGET:
    case MCOI::OPERAND_IMMEDIATE: {
      Op = MachineOperand::CreateImm(0);
      break;
    }

    case MCOI::OPERAND_REGISTER: {
      Op = MachineOperand::CreateReg(1, true);
      break;
    }

    case MCOI::OPERAND_MEMORY: {
      break;
      auto UseReg = (bool)choose(2);
      auto CanUseReg = OpInfo.RegClass < MRI->getNumRegClasses();
      Op = UseReg && CanUseReg ?
        MachineOperand::CreateReg(1, true) :
        MachineOperand::CreateImm(0);
      break;
    }

    default:
      llvm_unreachable("unkown operand type");
    }

    randOperand(Op, OpInfo);
    New->addOperand(*MF, Op);
  }

  return New;
}

bool Transformation::Replace()
{
  if (NumInstrs == 0) return false;

  New = randInstr();
  auto &MBB = *MF->begin();
  Old = select(MBB.instr_begin());
  replaceInst(MBB, Old, New);
  PrevTransformation = REPLACE;
  return true;
}

bool Transformation::Move()
{
  if (NumInstrs == 1) return false;

  auto &MBB = *MF->begin();

  auto Instr = select(MBB.instr_begin());
  NextLoc = std::next(Instr, 1);

  // move
  auto NewLoc = select(Instr);
  Instr->removeFromParent();
  MBB.insert(NewLoc, Instr);

  New = Instr;
  PrevTransformation = MOVE;

  return true;
}

bool Transformation::Insert()
{ 
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

bool Transformation::Delete()
{
  if (NumInstrs == 0) return false;

  auto &MBB = *MF->begin();
  New = select(MBB.instr_end());

  PrevTransformation = DELETE;

  NumInstrs--;
  return true;
}
