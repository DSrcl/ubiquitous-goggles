#include <llvm/IR/Module.h>
#include <llvm/IR/Mangler.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Codegen/MachineFunction.h>
#include <llvm/Codegen/AsmPrinter.h>
#include <llvm/Codegen/MachineModuleInfo.h>
#include <llvm/Codegen/MachineFunctionInitializer.h>
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCStreamer.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/IR/LegacyPassManager.h>


#include <string>
#include <memory>

using namespace llvm;

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
    MF.splice(MF.begin(), TheMF->begin());
    return false;
  }
};

// return true if success
bool compileToObjectFile(Module &M, MachineFunction &MF, const std::string &OutFilename, TargetMachine *TM)
{
  auto Printer = getAsmPrinter(M, OutFilename, TM);
  if (!Printer) return false;

  std::error_code EC;
  tool_output_file Out(OutFilename, EC, sys::fs::F_None);
  if (EC) return false;
  auto &OS = Out.os();
  
  auto AsmPrinterId = Printer->getPassID();
  
  CopyMFInitializer MFInit(MF);
  
  legacy::PassManager PM;
  TM->addPassesToEmitFile(PM, OS, LLVMTargetMachine::CGFT_AssemblyFile, true, AsmPrinterId, nullptr, AsmPrinterId, &MFInit);
  PM.run(M);

  Out.keep();

  return true;
}
