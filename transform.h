#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/CodeGen/MachineInstr.h>

class Transformation {
  llvm::MachineFunction *MF;

  unsigned NumInstrs;

  enum Kind {
    MUT_OPCODE,
    MUT_OPERAND,
    SWAP, 
    REPLACE,
    MOVE,
    INSERT,
    DELETE
  } PrevTransformation;

  // purely for debugging purpose because you can't undo more than once
  bool Undone;

  typedef llvm::MachineBasicBlock::instr_iterator InstrIterator;

  // ------- states for undo ------------
  // identifying previous position of an instruction,
  // used for move and delete
  InstrIterator NextLoc;

  // for mutate* and replace, and partially for insert
  llvm::MachineInstr *Old, *New;

  // for swap
  InstrIterator Swapped1, Swapped2;

  // select a random instruction
  InstrIterator select(InstrIterator Except);
  // -----------------------

  void doSwap(InstrIterator A, InstrIterator B);

  void doReplace(InstrIterator Orig, llvm::MachineInstr *Rep);

public:
  Transformation(llvm::MachineFunction *TheMF) : MF(TheMF) {
    assert(MF->size() == 1 && "no jumps for now");
    NumInstrs = MF->begin()->size();
  }

  void Undo();

  void MutateOpcode();
  void MutateOperand();
  void Swap();
  void Replace();
  void Move();
  void Insert();
  void Delete();
};

#endif
