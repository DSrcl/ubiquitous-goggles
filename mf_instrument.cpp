#include <llvm/IR/Module.h>
#include <llvm/IR/Mangler.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/CodeGen/MachineFunctionInitializer.h>
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/CodeGen/MachineInstrBuilder.h>

#include "mf_instrument.h"

using namespace llvm;

Instrumenter *getInstrumenter(TargetMachine *TM)
{
  auto Arch = TM->getTargetTriple().getArch();
  switch (Arch) {
  case Triple::x86_64:
    return new X86_64Instrumenter(TM);
    break;
  default:
    llvm_unreachable("target not supported"); 
  }
}

X86_64Instrumenter::X86_64Instrumenter(TargetMachine *TM) : Instrumenter(TM)
{
  // find out opcodes
  Callq = getOpcode("CALL64pcrel32");
  Movabsq = getOpcode("MOV64ri");
  // find out registers 
  RDI = getRegister("RDI");
  ESI = getRegister("ESI");
}

// assume `MF` only has one basic block
void X86_64Instrumenter::instrumentToReturn(MachineFunction &MF, int64_t JmpbufAddr) const
{
  auto &MBB = MF.front();
  // do this
  //
  // mov `addr`, RDI
  // mov  42,  ESI
  // call _longjmp
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Movabsq))
                .addImm(JmpbufAddr)
                .addReg(RDI));
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Movabsq))
                .addImm(42)
                .addReg(ESI));
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Callq))
                .addExternalSymbol("_longjmp"));
    
}
