#include "transform.h"
#include <cstdlib>

using namespace llvm;

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

void Transformation::Swap()
{
  if (NumInstrs == 1) return;

  auto &MBB = *MF->begin();
  auto A = select(MBB.instr_end()),
       B = select(A);
  doSwap(A, B);

  PrevTransformation = SWAP;
}
