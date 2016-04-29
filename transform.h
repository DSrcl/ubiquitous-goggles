#ifndef _TRANSFORM_H_
#define _TRANSFORM_H_

#include <llvm/ADT/EquivalenceClasses.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/CodeGen/MachineInstr.h>
#include <llvm/Target/TargetInstrInfo.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/Support/raw_ostream.h>

class Transformation {
  llvm::MachineFunction *MF;
  llvm::TargetMachine *TM;
  const llvm::MCInstrInfo *MII;
  const llvm::MCRegisterInfo *MRI;
  const llvm::TargetRegisterInfo *TRI;
  const llvm::TargetInstrInfo *TII;
  // allows us to quickly select an opcode that's random but "syntactically equivalent" of a given opcode
  llvm::EquivalenceClasses<unsigned> OpcodeClasses;

  unsigned NumInstrs;

  enum Kind {
    NOP,
    MUT_OPCODE,
    MUT_OPERAND,
    SWAP, 
    REPLACE,
    MOVE,
    INSERT,
    DELETE
  } PrevTransformation;

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

  // for delete
  llvm::MachineBasicBlock *Parent;
  // -----------------------

  void doSwap(InstrIterator A, InstrIterator B);

  void doReplace(InstrIterator Orig, llvm::MachineInstr *Rep);

  // build equivalence classes for opcodes
  void buildOpcodeClasses();

  const std::vector<int64_t> Immediates {
    0,
      1, -1, 2, -2, 3, -3, 4, -4,
      5, -5, 6, -6, 7, -7, 8, -8,
      16, -16, 32, -32, 64, -64, 128, -128
  };

  void randOperand(llvm::MachineOperand &Op, const llvm::MCOperandInfo &OpInfo);

  unsigned chooseNonBranchOpcode();

  // replace `Old` with `New`
  static void replaceInst(llvm::MachineBasicBlock &MBB, InstrIterator Old, InstrIterator New) {
    MBB.insert(Old, New);
    Old->removeFromParent();
  }

  llvm::MachineInstr *randInstr();

  const llvm::TargetRegisterClass *getRegClass(const llvm::MCOperandInfo &Op);

public:
  Transformation(llvm::TargetMachine *TheTM, llvm::MachineFunction *TheMF) : MF(TheMF), TM(TheTM) {
    assert(MF->size() == 1 && "no jumps for now");

    NumInstrs = MF->begin()->size();

    MII = TM->getMCInstrInfo();
    MRI = TM->getMCRegisterInfo();
    TII = MF->getSubtarget().getInstrInfo();
    TRI = MF->getSubtarget().getRegisterInfo();

    buildOpcodeClasses();  
  }

  void Undo();

  bool MutateOpcode();
  bool MutateOperand();
  bool Swap();
  bool Replace();
  bool Move();
  bool Insert();
  bool Delete();
};

#endif
