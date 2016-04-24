#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineFunctionInitializer.h>
#include <llvm/CodeGen/MachineInstrBuilder.h>
#include <llvm/CodeGen/MachineMemOperand.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/CodeGen/MachineOperand.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Mangler.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetInstrInfo.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetSubtargetInfo.h>

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
// * declare `num_regs`
// * emit code to dump `Regs` at the end of `MBB`
void Instrumenter::dumpRegisters(llvm::Module &M, llvm::MachineBasicBlock &MBB, const std::vector<unsigned> &Regs)
{
  assert(FreeReg && "FreeReg uninitialized");

  auto &Ctx = M.getContext();
 
  // list of (offset, size)
  std::vector<std::pair<unsigned, unsigned>> RegInfo(Regs.size());
  std::vector<Constant *> RegInfoInitializer(Regs.size());

  auto *Int64Ty = Type::getInt64Ty(Ctx);
  auto *RegInfoTy = StructType::get(Ctx, std::vector<Type *> {Int64Ty, Int64Ty});

  unsigned CurOffset = 0;
  for (unsigned i = 0, e = Regs.size(); i != e; i++) {
    unsigned Reg = Regs[i];
    const auto &RC = MRI->getRegClass(Reg);
    unsigned RegSize = RC.getSize();
    CurOffset = align(CurOffset, RC.getAlignment());
    RegInfo[i].first = CurOffset;
    RegInfo[i].second = RegSize;
    RegInfoInitializer[i] = ConstantStruct::get(RegInfoTy,
                                                std::vector<Constant *> {
                                                ConstantInt::get(Int64Ty, CurOffset),
                                                ConstantInt::get(Int64Ty, RegSize) });
    CurOffset += RegSize;
  }

  // declare `reg_data`
  auto *Int8Ty = Type::getInt8Ty(Ctx);
  auto *RegDataTy = ArrayType::get(Int8Ty, CurOffset);
  auto *RegData = new GlobalVariable(M, RegDataTy, false,
                                     GlobalVariable::ExternalLinkage,
                                     ConstantAggregateZero::get(RegDataTy),
                                     "_ug_reg_data");

  auto *RegInfoArrTy = ArrayType::get(RegInfoTy, Regs.size());
  // declare `reg_info`
  new GlobalVariable(M, RegInfoArrTy, true /* constant */,
                     GlobalVariable::ExternalLinkage,
                     ConstantArray::get(RegInfoArrTy, RegInfoInitializer),
                     "_ug_reg_info");

  // declare `num_regs`
  new GlobalVariable(M, Int64Ty, true,
                     GlobalVariable::ExternalLinkage,
                     ConstantInt::get(Int64Ty, Regs.size()),
                     "_ug_num_regs");


  
  auto *MF = MBB.getParent();

  auto &Subtarget = MF->getSubtarget();
  auto *TII = Subtarget.getInstrInfo();
  auto *TRI = Subtarget.getRegisterInfo();
  // load address of `reg_data` into `FreeReg`
  auto *LoadAddr = TII->getGlobalPICAddr(*MF, FreeReg, &MF->getTarget(), RegData);
  MBB.push_back(LoadAddr);
  for (unsigned i = 0, e = Regs.size(); i != e; i++) {
    unsigned Reg = Regs[i];
    unsigned Offset = RegInfo[i].first;
    auto *RC = TRI->getMinimalPhysRegClass(Reg);
    auto *Store = TII->storeReg(*MF, Reg, FreeReg, Offset, RC);
    MBB.push_back(Store);
  }
}

X86_64Instrumenter::X86_64Instrumenter(TargetMachine *TM) : Instrumenter(TM)
{
  FreeReg = getRegister("R15");
  // find out opcodes
  Callq = getOpcode("CALL64pcrel32");
  Movabsq = getOpcode("MOV64ri");
  Retq = getOpcode("RETQ");
  // find out registers 
  RDI = getRegister("RDI");
  ESI = getRegister("ESI");
  EAX = getRegister("EAX");
  RAX = getRegister("RAX");
  AL = getRegister("AL");
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

// assume `MF` only has one basic block
void X86_64Instrumenter::instrumentToReturnNormally(MachineFunction &MF, MachineBasicBlock &MBB) const
{
  MBB.push_back(BuildMI(MF, DebugLoc(), MII->get(Retq)));
}

std::vector<unsigned> X86_64Instrumenter::getReturnRegs(llvm::Function *F) const 
{
  auto *RetType = F->getFunctionType()->getReturnType();

  // everything below is hack 
  IntegerType *IntTy;
  if ((IntTy=dyn_cast<IntegerType>(RetType)) != nullptr) { 
    if (IntTy->getBitWidth() == 64) {
      return { RAX };
    } else if (IntTy->getBitWidth() == 32) {
      return { EAX };
    } else {
      return { AL };
    }
  }
  
  if (RetType->isPointerTy()) {
    return { RAX };
  }

  if (RetType->isVoidTy()) {
    return {};
  }

  llvm_unreachable("don't need this for an A");
}

/*
 * The first six integer or pointer arguments are passed in registers
 * RDI, RSI, RDX, RCX (R10 in the Linux kernel interface[16]:124), R8, and R9,
 * while XMM0, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6 and XMM7 are used for certain floating point arguments
 */
void X86_64Instrumenter::mprotect() const
{
}

