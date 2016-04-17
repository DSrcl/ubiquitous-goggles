#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

#include <llvm/ADT/EquivalenceClasses.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/CodeGen/MachineInstr.h>
#include <llvm/Target/TargetMachine.h>

class Transformation {
  llvm::MachineFunction *MF;
  llvm::TargetMachine *TM;
  // allows us to quickly select an opcode that's random but "syntactically equivalent" of a given opcode
  llvm::EquivalenceClasses<unsigned> OpcodeClasses;

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

  // build equivalence classes for opcodes
  void buildOpcodeClasses();

public:
  Transformation(llvm::TargetMachine *TheTM, llvm::MachineFunction *TheMF) : MF(TheMF), TM(TheTM) {
    assert(MF->size() == 1 && "no jumps for now");
    NumInstrs = MF->begin()->size();
    buildOpcodeClasses();  
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
