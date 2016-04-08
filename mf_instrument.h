#ifndef _MF_DUMP_REGS_H_
#define _MF_DUMP_REGS_H_

#include <llvm/MC/MCRegisterInfo.h>

/*
MachineFunction *emitDumpRegs(Triple::ArchType Arch,
                              const std::vector<unsigned> &Regs,
                              GlobalVariable *RegInfo,
                              GlobalVariable *RegData)
                              */
class Instrumenter {
protected:
  llvm::TargetMachine *TM;
  const llvm::MCRegisterInfo *MRI;
  const llvm::MCInstrInfo *MII;

public:
  Instrumenter(llvm::TargetMachine *TheTM) : TM(TheTM) {
    MII = TM->getMCInstrInfo();
    MRI = TM->getMCRegisterInfo();
  }

  unsigned getOpcode(const std::string &Name) {
    for (unsigned i = 0; i < MII->getNumOpcodes(); i++) { 
      if (std::string(MII->getName(i)) == Name) {
        return i;
      }
    }
    llvm_unreachable("unable to find opcode");
  }
  
  unsigned getRegister(const std::string &Name) {
    for (unsigned i = 0; i < MRI->getNumRegs(); i++) {
      if (Name == MRI->getName(i))  {
        return i;
      }
    }
    llvm_unreachable("unable to find register");
  }

  virtual void instrumentToReturn(llvm::MachineFunction &MF, int64_t JmpbfuAddr) const = 0;
};

class X86_64Instrumenter : public Instrumenter {
  // opcodes 
  unsigned Callq;
  unsigned Movabsq;

  // registers
  unsigned RDI;
  unsigned ESI;

public:
  X86_64Instrumenter(llvm::TargetMachine *TM);
  void instrumentToReturn(llvm::MachineFunction &MF, int64_t JmpbfuAddr) const override;
};

Instrumenter *getInstrumenter(llvm::TargetMachine *TM);

#endif
