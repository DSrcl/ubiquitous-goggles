#ifndef _MF_DUMP_REGS_H_
#define _MF_DUMP_REGS_H_

#include <llvm/MC/MCRegisterInfo.h>


/*
MachineInstr *storeRegToAlignedAddr(MachineFunction &MF, unsigned SrcReg,
                                    MachineOperand &Addr,
                                    const TargetRegisterClass *RC) const override;
                                    */

class Instrumenter {
protected:
  llvm::TargetMachine *TM;
  const llvm::MCRegisterInfo *MRI;
  const llvm::MCInstrInfo *MII;
  // temporary register whose value users don't care about (e.g. R15 in x86_64)
  unsigned FreeReg;

public:
  Instrumenter(llvm::TargetMachine *TheTM) : TM(TheTM) {
    MII = TM->getMCInstrInfo();
    MRI = TM->getMCRegisterInfo();
  }

  unsigned getOpcode(const std::string &Name) const {
    for (unsigned i = 0; i < MII->getNumOpcodes(); i++) { 
      if (std::string(MII->getName(i)) == Name) {
        return i;
      }
    }
    llvm_unreachable("unable to find opcode");
  }
  
  unsigned getRegister(const std::string &Name) const {
    for (unsigned i = 0; i < MRI->getNumRegs(); i++) {
      if (Name == MRI->getName(i))  {
        return i;
      }
    }
    llvm_unreachable("unable to find register");
  }

  virtual void instrumentToReturn(llvm::MachineFunction &MF, int64_t JmpbfuAddr) const = 0;

  void dumpRegisters(llvm::Module &M, llvm::MachineBasicBlock &MB, const std::vector<unsigned> &Regs);

  virtual void instrumentToReturnNormally(llvm::MachineFunction &MF, llvm::MachineBasicBlock &MBB) const = 0;

  virtual std::vector<unsigned> getReturnRegs(llvm::Function *) const = 0;

  static unsigned align(unsigned Addr, unsigned Alignment);
};

class X86_64Instrumenter : public Instrumenter {
  // opcodes 
  unsigned Callq;
  unsigned Movabsq;
  unsigned Retq;

  // registers
  unsigned RDI;
  unsigned ESI;
  unsigned RAX;
  unsigned EAX;
  unsigned AL;

public:
  X86_64Instrumenter(llvm::TargetMachine *TM);
  void instrumentToReturn(llvm::MachineFunction &MF, int64_t JmpbfuAddr) const override;
  void instrumentToReturnNormally(llvm::MachineFunction &MF, llvm::MachineBasicBlock &MBB) const override;
  std::vector<unsigned> getReturnRegs(llvm::Function *) const override;
};

Instrumenter *getInstrumenter(llvm::TargetMachine *TM);

#endif
