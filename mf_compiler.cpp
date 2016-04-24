#include <llvm/CodeGen/AsmPrinter.h>
#include <llvm/CodeGen/MachineFunction.h>
#include <llvm/CodeGen/MachineFunctionInitializer.h>
#include <llvm/CodeGen/MachineModuleInfo.h>
#include <llvm/IR/LLVMContext.h>
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
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/Target/TargetMachine.h>

#include "mf_compiler.h"
#include "mf_instrument.h"
#include <string>
#include <memory>

using namespace llvm;

// TODO hire someone to clean this up
// magic
AsmPrinter *getAsmPrinter(Module &M, const std::string &OutFilename, TargetMachine *TM)
{
  const auto MII = TM->getMCInstrInfo();
  const auto MRI = TM->getMCRegisterInfo();
  MachineModuleInfo *MMI = new MachineModuleInfo(
    *TM->getMCAsmInfo(), *TM->getMCRegisterInfo(), TM->getObjFileLowering());
  auto Context = &MMI->getContext();
  const auto &STI = *TM->getMCSubtargetInfo();
  const auto &Options = TM->Options;
  std::error_code EC;
  tool_output_file Out(OutFilename, EC, sys::fs::F_None);
  if (EC) return nullptr;
  auto &OS = Out.os();

  MCCodeEmitter *MCE = TM->getTarget().createMCCodeEmitter(*MII, *MRI, *Context);
  MCAsmBackend *MAB = TM->getTarget().createMCAsmBackend(*MRI, TM->getTargetTriple().str(), TM->getTargetCPU());
  if (!MCE || !MAB)
    return nullptr;

  // Don't waste memory on names of temp labels.
  Context->setUseNamesOnTempLabels(false);

  std::unique_ptr<MCStreamer> AsmStreamer;

  Triple T(TM->getTargetTriple().str());
  AsmStreamer.reset(TM->getTarget().createMCObjectStreamer(
      T, *Context, *MAB, OS, MCE, STI, Options.MCOptions.MCRelaxAll,
      /*DWARFMustBeAtTheEnd*/ true));

  auto Printer = TM->getTarget().createAsmPrinter(*TM, std::move(AsmStreamer));
  Printer->MMI = MMI;
  Printer->Mang = new Mangler();
  TM->getObjFileLowering()->Initialize(*Context, *TM);
  Printer->OutStreamer->InitSections(false);

  return Printer;
}

struct CopyMFInitializer : MachineFunctionInitializer {
  MachineFunction *TheMF;
  CopyMFInitializer(MachineFunction &MF) {
    TheMF = &MF;
  }

  bool initializeMachineFunction(MachineFunction &MF) {
    //NewSpawnFn->getBasicBlockList().splice(NewSpawnFn->begin(), SpawnFn->getBasicBlockList());
    // move instructions from `TheMF` to `MF`
    if (MF.getFunction() == TheMF->getFunction()) {
      for (auto &MBB : *TheMF) {
        auto *MBB_ = MF.CreateMachineBasicBlock();
        MF.push_back(MBB_);
        for (auto &MI : MBB) { 
          MBB_->push_back(MF.CloneMachineInstr(&MI));
        }
      }
    }

    return false;
  }
};

// return true if success
bool compileToObjectFile(Module &M, MachineFunction &MF, const std::string &OutFilename, TargetMachine *TM, bool PrintAssemly)
{
  auto *Printer = getAsmPrinter(M, OutFilename, TM);
  if (!Printer) return false;

  std::error_code EC;
  tool_output_file Out(OutFilename, EC, sys::fs::F_None);
  if (EC) return false;
  auto &OS = Out.os();
  
  auto AsmPrinterId = Printer->getPassID();
  
  CopyMFInitializer MFInit(MF);

  auto FileType = PrintAssemly ? LLVMTargetMachine::CGFT_AssemblyFile : LLVMTargetMachine::CGFT_ObjectFile;
  
  legacy::PassManager PM;
  TM->addPassesToEmitFile(PM, OS, FileType, true, AsmPrinterId, nullptr, nullptr, &MFInit);
  PM.run(M);

  Out.keep();

  return true;
}

bool emitDumpRegistersModule(TargetMachine *TM, const std::vector<unsigned> &Regs, const std::string &OutFilename)
{ 
  auto &Ctx = getGlobalContext();
  auto *VoidTy = Type::getVoidTy(Ctx);

  auto *M = new Module("dump_regs", Ctx); 
  M->setDataLayout(*TM->getDataLayout());

  auto *FnTy = FunctionType::get(VoidTy, std::vector<Type *>{}, false);
  auto *F = dyn_cast<Function>(M->getOrInsertFunction("dump_registers", FnTy));
  assert(F);
  // convince the pass manager to do codegen for this function
  BasicBlock::Create(Ctx, "", F);

  MachineModuleInfo *MMI = new MachineModuleInfo(
    *TM->getMCAsmInfo(), *TM->getMCRegisterInfo(), TM->getObjFileLowering());
  MachineFunction MF(F, *TM, 0, *MMI);
  auto *MBB = MF.CreateMachineBasicBlock();
  MF.push_back(MBB);
  auto *Instrumenter = getInstrumenter(TM);
  Instrumenter->dumpRegisters(*M, *MBB, Regs);
  Instrumenter->instrumentToReturnNormally(MF, *MBB);
  return compileToObjectFile(*M, MF, OutFilename, TM);
}
