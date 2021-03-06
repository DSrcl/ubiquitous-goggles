#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/LinkAllAsmWriterComponents.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include <llvm/CodeGen/MachineModuleInfo.h>
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include <llvm/Target/TargetLoweringObjectFile.h>
#include "llvm/Target/TargetSubtargetInfo.h"
#include <llvm/ADT/Triple.h>
#include <llvm/Bitcode/BitcodeWriterPass.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include "mf_compiler.h"
#include "mf_instrument.h"
#include "replay_cli.h"
#include "transform.h"
#include "search.h"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <unistd.h>

using namespace llvm;

cl::opt<std::string> TargetName("f",
                                cl::desc("name of target function to optimize"),
                                cl::value_desc("target function"), cl::Required,
                                cl::Prefix);

cl::opt<std::string>
    TestcaseFilename(cl::Positional, cl::desc("<testcase file>"), cl::Required);

TargetMachine *getTargetMachine(Module *M) {
  InitializeAllTargets();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();

  Triple TheTriple = Triple(M->getTargetTriple());

  if (TheTriple.getTriple().empty())
    TheTriple.setTriple(sys::getDefaultTargetTriple());

  // create target machine
  TargetOptions Options = InitTargetOptionsFromCodeGenFlags();
  std::string CPUStr = getCPUStr(), FeaturesStr = getFeaturesStr();
  std::string Error;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TheTriple.str(), Error);
  assert(TheTarget);
  return TheTarget->createTargetMachine(TheTriple.getTriple(), CPUStr,
                                        FeaturesStr, Options, Reloc::PIC_);
}

int run(const std::string &cmd) {
  errs() << "---------- " << cmd << '\n';
  return std::system(cmd.c_str());
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  cl::ParseCommandLineOptions(argc, argv, "give us an A, please");

  const std::string ServerObj = "server.o";
  const std::string DumpRegsObj = "dump_regs.o";
  const std::string ServerExe = "server";
  const std::string CreateServer = "./create-server";

  // 1. do `llvm-link `testcase` server.bc | llc -filetype=obj -o server.o`
  run("llvm-link server.bc " + TestcaseFilename + " -o - " + "| " +
      CreateServer + " - -o - -f" + TargetName +
      "| opt -O3 -o - | llc -filetype=obj -o " + ServerObj);

  LLVMContext &Context = getGlobalContext();
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(TestcaseFilename, Err, Context);

  std::unique_ptr<TargetMachine> TM(getTargetMachine(M.get()));

  // 2. look at `testcase` and figure out which return registers to dump
  auto *TargetFunction = M->getFunction(TargetName);
  auto *Instrumenter = getInstrumenter(TM.get());
  auto *TargetTy = TargetFunction->getFunctionType();
  const auto RetRegs = Instrumenter->getReturnRegs(TargetTy);

  errs() << "!!! " << TM->getTargetTriple().normalize() << "\n";
  // 3. create `dump_regs.o`
  emitDumpRegistersModule(TM.get(), RetRegs, DumpRegsObj);

  // 4. do `cc malloc.o dump_regs.o server.o -o server`
  run("cc " + DumpRegsObj + " malloc.o " + ServerObj + " -o " + ServerExe);

  // 5. run the server
  run("./" + ServerExe);

  // FIXME synchronize with the server
  sleep(1);
  ReplayClient Client(TM.get(), "worker-data.txt", "jmp_buf.txt");

  MachineModuleInfo *MMI = new MachineModuleInfo(
      *TM->getMCAsmInfo(), *TM->getMCRegisterInfo(), TM->getObjFileLowering());
  MachineFunction MF(TargetFunction, *TM, 0, *MMI);
  auto MBB = MF.CreateMachineBasicBlock();
  MF.push_back(MBB);

  Searcher Synthesizer(TM.get(), M.get(), &MF, TargetTy, &Client);
  Synthesizer.synthesize();
  auto Optimized = std::unique_ptr<MachineFunction>(Synthesizer.optimize(2000));
  errs() << "\n---final optimized rewrite\n";
  for (const auto &MBB : *Optimized) {
    for (const auto &MI : MBB) {
      errs() << MI;
    }
  }
}
