#include <llvm/IR/Module.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/Constants.h>
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
#include <llvm/Target/TargetSubtargetInfo.h>
#include <llvm/Target/TargetInstrInfo.h>
#include <llvm/CodeGen/MachineInstrBuilder.h>
#include <llvm/CodeGen/MachineOperand.h>

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

unsigned Instrumenter::align(unsigned Addr, unsigned Alignment)
{
  unsigned Aligned = (Addr+Alignment-1) / Alignment * Alignment;
  assert(Aligned % Alignment == 0 && "failed to align address");
  return Aligned;
}

// * declare `reg_data(uint8[])` in `M`
// * declare `reg_info(struct{ size_t offset, size}[])` in `M`
// * emit code to dump `Regs` at the end of `MBB`
void Instrumenter::dumpRegisters(llvm::Module &M, llvm::MachineBasicBlock &MBB, std::vector<unsigned> &Regs)
{

  auto &Ctx = M.getContext();
 
  // list of (offset, size)
  std::vector<std::pair<unsigned, unsigned>> RegInfo(Regs.size());

  unsigned CurOffset = 0;
  for (unsigned i = 0, e = Regs.size(); i != e; i++) {
    unsigned Reg = Regs[i];
    const auto &RC = MRI->getRegClass(Reg);
    unsigned RegSize = RC.getSize();
    CurOffset = align(CurOffset, RC.getAlignment());
    RegInfo[i].first = CurOffset;
    RegInfo[i].second = RegSize;
    CurOffset += RegSize;
    errs() << "!!! size of register " << MRI->getName(Reg) << " : " << RegSize << "\n";
  }

  errs() << "!!! size of reg data: " << CurOffset << "\n";

  // declare `reg_data`
  auto *Int8Ty = Type::getInt8Ty(Ctx);
  auto *RegDataTy = ArrayType::get(Int8Ty, CurOffset);
  auto *RegData = new GlobalVariable(M, RegDataTy, false,
                                     GlobalVariable::ExternalLinkage,
                                     ConstantAggregateZero::get(RegDataTy),
                                     "reg_data");

  auto *MF = MBB.getParent();
  auto &Subtarget = MF->getSubtarget();
  auto *TII = Subtarget.getInstrInfo();
  auto *TRI = Subtarget.getRegisterInfo();
  for (unsigned i = 0, e = Regs.size(); i != e; i++) {
    unsigned Reg = Regs[i];
    unsigned Offset = RegInfo[i].first;
    auto Addr = MachineOperand::CreateGA(RegData, Offset);
    auto *Store = TII->storeRegToAlignedAddr(*MF, Reg, Addr, TRI->getMinimalPhysRegClass(Reg));
    MBB.push_back(Store);
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
  // emit code to do this
  //
  // mov `addr`, RDI
  // mov  42,  ESI
  // call _longjmp
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Movabsq))
                .addReg(RDI)
                .addImm(JmpbufAddr));
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Movabsq))
                .addReg(ESI)
                .addImm(42));
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Callq))
                .addExternalSymbol("longjmp"));
    
}
