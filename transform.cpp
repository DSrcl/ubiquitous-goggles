#include "transform.h"
#include <iterator>
#include <cstdlib>

#include <llvm/MC/MCInstrInfo.h>

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
  return (unsigned)(rand() % (int)(NumChoice + 1));
}

void Transformation::Undo()
{
  assert(!Undone);
  

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

  Undone = true;
}

Transformation::InstrIterator Transformation::select(Transformation::InstrIterator Except)
{
  InstrIterator Selected;
  auto &MBB = *MF->begin();

  assert(NumInstrs > 1 || MBB.begin() != Except);

  do {
    unsigned Idx = choose(NumInstrs);
    Selected = MBB.begin() + Idx;
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
      if (isExchangeable(MII, Opc, i)) { 
        OpcodeClasses.unionSets(Opc, i);
        break;
      }
    }
  }
}

void Transformation::Swap()
{
  if (NumInstrs == 1) return;

  auto &MBB = *MF->begin();
  auto A = select(MBB.instr_end()),
       B = select(A);
  doSwap(A, B);

  PrevTransformation = SWAP;
  Swapped1 = A;
  Swapped2 = B;
}

void Transformation::MutateOpcode()
{
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

  // insert new instruction into the function
  MBB.insert(InstrIterator(Old), New);
  Old->removeFromParent();
}

void Transformation::MutateOperand()
{
  auto &MBB = *MF->begin();
  auto Instr = select(MBB.instr_end());

  Old = Instr;
  New = MF->CloneMachineInstr(Instr);
  PrevTransformation = MUT_OPERAND;

  const auto &Desc = Instr->getDesc(); 
  // select which operand to mutate
  unsigned OpIdx = choose(Desc.NumOperands);
  const auto &OpInfo = Desc.OpInfo[OpIdx];
  auto &Operand = New->getOperand(OpIdx);
  switch (OpInfo.OperandType) {
  case MCOI::OPERAND_UNKNOWN:
    break; // give up

  case MCOI::OPERAND_PCREL:
  case MCOI::OPERAND_FIRST_TARGET:
  case MCOI::OPERAND_IMMEDIATE: { 
    unsigned NewImm = Immediates[choose(Immediates.size())];
    assert(Operand.isImm());
    Operand.setImm(NewImm);
    break;
  }

  case MCOI::OPERAND_REGISTER: {
    assert(Operand.isReg());
    const auto &RC = MRI->getRegClass(OpInfo.RegClass);
    assert(RC.contains(Operand.getReg()));
    unsigned NewReg = RC.getRegister(choose(RC.getNumRegs()));
    Operand.setReg(NewReg);
    break;
  }
  }
}
