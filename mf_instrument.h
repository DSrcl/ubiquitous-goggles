#ifndef _MF_DUMP_REGS_H_
#define _MF_DUMP_REGS_H_

#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/CodeGen/MachineBasicBlock.h> 
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <set>

class Instrumenter {
  std::map<std::string, unsigned> Registers;
  void initRegisters() {
    for (unsigned i = 0; i < MRI->getNumRegs(); i++) {
      Registers[MRI->getName(i)] = i;
    }
  }

protected:
  llvm::TargetMachine *TM;
  const llvm::MCRegisterInfo *MRI;
  const llvm::MCInstrInfo *MII;
  // temporary register whose value users don't care about (e.g. R15 in x86_64)
  unsigned FreeReg;

  std::map<llvm::Module *, llvm::GlobalVariable *> RegData;
  // list of (offset, size)
  std::vector<std::pair<unsigned, unsigned>> RegInfo;

public:
  Instrumenter(llvm::TargetMachine *TheTM) : TM(TheTM) {
    MII = TM->getMCInstrInfo();
    MRI = TM->getMCRegisterInfo();
    initRegisters();
  }

  void calculateRegBufferLayout(llvm::Module &M, const std::vector<unsigned> &Regs);

  unsigned getOpcode(const std::string &Name) const {
    for (unsigned i = 0; i < MII->getNumOpcodes(); i++) {
      if (std::string(MII->getName(i)) == Name) {
        return i;
      }
    }
    llvm_unreachable("unable to find opcode");
  }

  unsigned getRegister(const std::string &Name) const {
    return Registers.at(Name);
  }

  virtual void instrumentToReturn(llvm::MachineFunction &MF,
                                  int64_t JmpbfuAddr) const = 0;

  void dumpRegisters(llvm::Module &M, llvm::MachineBasicBlock &MBB,
                     const std::vector<unsigned> &Regs);

  virtual void
  instrumentToReturnNormally(llvm::MachineFunction &MF,
                             llvm::MachineBasicBlock &MBB) const = 0;

  virtual std::vector<unsigned> getReturnRegs(llvm::FunctionType *) const = 0;

  // make the runtime's frame unaccessible
  virtual void protectRTFrame(llvm::MachineBasicBlock &MBB, int64_t FrameBegin,
                              int64_t FrameSize) const = 0;

  // unprotect runtime's frame
  virtual void unprotectRTFrame(llvm::MachineBasicBlock &MBB,
                                int64_t FrameBegin,
                                int64_t FrameSize) const = 0;

  static unsigned align(unsigned Addr, unsigned Alignment);
};

class X86_64Instrumenter : public Instrumenter {
  // opcodes
  unsigned Callq;
  unsigned Movabsq;
  unsigned Retq;
  unsigned PUSH64r;
  unsigned POP64r;

  // registers
  unsigned RDI, ESI, RAX, EAX, AL, RSI, RDX, RCX, R8, R9;

  void push(llvm::MachineBasicBlock &MBB, unsigned Reg,
            llvm::MachineBasicBlock::instr_iterator InsertPt) const;
  void pop(llvm::MachineBasicBlock &MBB, unsigned Reg,
           llvm::MachineBasicBlock::instr_iterator InsertPt) const;
  void callMprotect(llvm::MachineBasicBlock &MBB, int64_t FrameBegin,
                    int64_t FrameSize, int ProtLevel,
                    llvm::MachineBasicBlock::instr_iterator InsertPt) const;

public:
  X86_64Instrumenter(llvm::TargetMachine *TM);
  void instrumentToReturn(llvm::MachineFunction &MF,
                          int64_t JmpbfuAddr) const override;
  void instrumentToReturnNormally(llvm::MachineFunction &MF,
                                  llvm::MachineBasicBlock &MBB) const override;
  std::vector<unsigned> getReturnRegs(llvm::FunctionType *) const override;
  void protectRTFrame(llvm::MachineBasicBlock &MBB, int64_t FrameBegin,
                      int64_t FrameSize) const override;
  void unprotectRTFrame(llvm::MachineBasicBlock &MBB, int64_t FrameBegin,
                        int64_t FrameSize) const override;
};

Instrumenter *getInstrumenter(llvm::TargetMachine *TM);

#endif
