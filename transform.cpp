#include "transform.h"
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
    unsigned Idx = (unsigned)(rand() % (int)(NumInstrs + 1));
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
  const auto *MII = TM->getMCInstrInfo();

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
