#include <llvm/Pass.h>
#include <llvm/ADT/Triple.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Type.h>
#include <llvm/Bitcode/BitcodeWriterPass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/ManagedStatic.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/raw_ostream.h>

#include <vector>
#include <map>
#include <string>

using namespace llvm;

cl::opt<std::string> FunctionToRun("f", cl::desc("functions to run"),
                                   cl::value_desc("function"), cl::Required,
                                   cl::Prefix);

cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<input file>"),
                                   cl::Required);

cl::opt<std::string> OutputFilename("o", cl::desc("Specify output file name"),
                                    cl::value_desc("output file"));

static void replaceStubCall(CallInst *Stub, FunctionType *TargetTy,
                            Function::arg_iterator FnArgs) {
  auto *Callee = new BitCastInst(Stub->getArgOperand(0),
                                 TargetTy->getPointerTo(), "", Stub);

  // call with proper arguments
  std::vector<Value *> Args;
  Args.resize(TargetTy->getNumParams());
  for (auto &Arg : Args) {
    Arg = FnArgs++;
  }

  auto Replaced = CallInst::Create(Callee, Args, "", Stub);
  auto IsVoid = TargetTy->getReturnType()->isVoidTy();
  if (!IsVoid) {
    Stub->replaceAllUsesWith(Replaced);
  } else if (!Stub->use_empty()) {
    // change `ret void xxx` to `ret`
    auto Ret = dyn_cast<ReturnInst>(Stub->uses().begin()->getUser());
    assert(Ret);
    auto &Ctx = Ret->getParent()->getParent()->getContext();
    ReturnInst::Create(Ctx, Ret->getParent());
    Ret->eraseFromParent();
  }

  Stub->eraseFromParent();
}

void emitX86_64GetTopOfStack(Function *Main) {
  auto M = Main->getParent();
  auto &Ctx = M->getContext();

  auto FnTy = FunctionType::get(Type::getInt8PtrTy(Ctx), false);
  auto Asm = InlineAsm::get(FnTy, "movq %rbp, $0",
                            "=r,~{dirflag},~{fpsr},~{flags}", false);
  auto Head = &*Main->begin()->begin();
  auto RBP = CallInst::Create(Asm, std::vector<Value *>{}, "x86_64.rbp", Head);
  auto TopOfStack = M->getGlobalVariable("_server_stack_top");
  new StoreInst(RBP, TopOfStack, Head);
}

void emitGetTopOfStack(Module &M) {
  Triple TargetTriple(M.getTargetTriple());
  auto Main = M.getFunction("main");
  auto Arch = TargetTriple.getArch();
  switch (Arch) {
  case Triple::x86_64:
    emitX86_64GetTopOfStack(Main);
    break;
  default:
    llvm_unreachable("target not supported");
  };
}

// replace `_stub_target_call` and `_stub_rewrite_call` with approciate
// instructions
// based on the type of `FnToRun`
//
// return the a version of `SpawnFn`
Function *fixSpawnWrapper(Function *SpawnFn, Function *FnToRun) {
  auto *M = SpawnFn->getParent();

  FunctionType *OldTy = SpawnFn->getFunctionType(),
               *TargetTy = FnToRun->getFunctionType();

  // concatenate arguments ofr `SpawnFn` and `FnToRun`
  std::vector<Type *> Params{};
  Params.insert(Params.end(), OldTy->param_begin(), OldTy->param_end());
  Params.insert(Params.end(), TargetTy->param_begin(), TargetTy->param_end());

  FunctionType *NewTy =
      FunctionType::get(TargetTy->getReturnType(), Params, false);
  Function *NewSpawnFn =
      Function::Create(NewTy, SpawnFn->getLinkage(), "x", SpawnFn->getParent());
  NewSpawnFn->copyAttributesFrom(SpawnFn);
  NewSpawnFn->takeName(SpawnFn);
  NewSpawnFn->getBasicBlockList().splice(NewSpawnFn->begin(),
                                         SpawnFn->getBasicBlockList());

  // transfer over arguments of old function to new function
  for (Function::arg_iterator I = NewSpawnFn->arg_begin(),
                              I2 = SpawnFn->arg_begin(), E = SpawnFn->arg_end();
       I2 != E; I++, I2++) {
    I2->mutateType(I->getType());
    I2->replaceAllUsesWith(&*I);
    I->takeName(&*I2);
  }

  // fix the call to `spawn_impl`
  // arguments used to call `spawn_impl`
  std::vector<Value *> SpawnArgs(NewTy->getNumParams());
  Function::arg_iterator FnArgs = NewSpawnFn->arg_begin();
  for (auto &Arg : SpawnArgs) {
    Arg = FnArgs++;
  }
  auto *SpawnImpl = M->getFunction("spawn_impl");
  auto *OrigCall = dyn_cast<CallInst>(SpawnImpl->uses().begin()->getUser());
  assert(OrigCall);
  auto *NewCall = CallInst::Create(SpawnImpl, SpawnArgs, "", OrigCall);
  auto IsVoid = TargetTy->getReturnType()->isVoidTy();
  if (!IsVoid) {
    OrigCall->replaceAllUsesWith(NewCall);
  } else if (!OrigCall->use_empty()) {
    // change `ret void xxx` to `ret`
    auto Ret = dyn_cast<ReturnInst>(OrigCall->uses().begin()->getUser());
    assert(Ret);
    auto &Ctx = Ret->getParent()->getParent()->getContext();
    ReturnInst::Create(Ctx, Ret->getParent());
    Ret->eraseFromParent();
  }
  OrigCall->eraseFromParent();
  return NewSpawnFn;
}

// replace `_stub_target_call` and `_stub_rewrite_call` with approciate
// instructions
// based on the type of `FnToRun`
//
// return the a version of `SpawnFn`
Function *fixSpawnImpl(Function *SpawnFn, Function *FnToRun) {
  FunctionType *OldTy = SpawnFn->getFunctionType(),
               *TargetTy = FnToRun->getFunctionType();

  // concatenate arguments ofr `SpawnFn` and `FnToRun`
  std::vector<Type *> Params{};
  Params.insert(Params.end(), OldTy->param_begin(), OldTy->param_end());
  Params.insert(Params.end(), TargetTy->param_begin(), TargetTy->param_end());

  FunctionType *NewTy =
      FunctionType::get(TargetTy->getReturnType(), Params, false);
  Function *NewSpawnFn =
      Function::Create(NewTy, SpawnFn->getLinkage(), "x", SpawnFn->getParent());
  NewSpawnFn->copyAttributesFrom(SpawnFn);
  NewSpawnFn->takeName(SpawnFn);
  NewSpawnFn->getBasicBlockList().splice(NewSpawnFn->begin(),
                                         SpawnFn->getBasicBlockList());

  // transfer over arguments of old function to new function
  for (Function::arg_iterator I = NewSpawnFn->arg_begin(),
                              I2 = SpawnFn->arg_begin(), E = SpawnFn->arg_end();
       I2 != E; I++, I2++) {
    I2->mutateType(I->getType());
    I2->replaceAllUsesWith(&*I);
    I->takeName(&*I2);
  }

  // arguments used to call `FnToRun`
  Function::arg_iterator FnArgs = NewSpawnFn->arg_begin();
  for (unsigned i = 0, e = OldTy->getNumParams(); i != e; i++) {
    ++FnArgs;
  }

  auto *RewriteCall = dyn_cast<CallInst>(FnToRun->getParent()
                                             ->getFunction("_stub_rewrite_call")
                                             ->uses()
                                             .begin()
                                             ->getUser());
  replaceStubCall(RewriteCall, TargetTy, FnArgs);

  auto *TargetCall = dyn_cast<CallInst>(FnToRun->getParent()
                                            ->getFunction("_stub_target_call")
                                            ->uses()
                                            .begin()
                                            ->getUser());
  replaceStubCall(TargetCall, TargetTy, FnArgs);

  return NewSpawnFn;
}

// pre-condition: `M` has been linked with "server.bc", which contains
// the template implementation of the `_server_spawn_worker` function
//
// replace call to `FunctionToRun` with a call to an appropriate version of
// `_server_spawn_worker`
void createServer(Module &M) {
  Function *Target = M.getFunction(FunctionToRun);
  auto *OrigSpawnImpl = M.getFunction("spawn_impl");
  Function *SpawnImpl = fixSpawnImpl(OrigSpawnImpl, Target);
  OrigSpawnImpl->mutateType(SpawnImpl->getType());
  OrigSpawnImpl->replaceAllUsesWith(SpawnImpl);
  OrigSpawnImpl->eraseFromParent();
  Function *SpawnFn =
      fixSpawnWrapper(M.getFunction("_server_spawn_worker"), Target);

  auto &Ctx = M.getContext();

  // declare global variable that refers to target function's name
  Constant *Str = ConstantDataArray::getString(Ctx, FunctionToRun);
  GlobalVariable *GV = new GlobalVariable(
      M, Str->getType(), true, GlobalValue::PrivateLinkage, Str,
      "server.fn-name", nullptr, GlobalVariable::NotThreadLocal, 0);
  Constant *Zero = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
  std::vector<Constant *> Idxs = {Zero, Zero};
  Constant *TargetName =
      ConstantExpr::getInBoundsGetElementPtr(Str->getType(), GV, Idxs);

  Type *GenericFnTy = SpawnFn->getFunctionType()->params()[0];

  // FIXME this doesn't work if target is alled indirectly
  for (auto &U : Target->uses()) {
    auto *Call = dyn_cast<CallInst>(U.getUser());
    if (!Call)
      continue;

    // cast type of target to `uint32_t (*)(void)`
    auto TargetCasted = new BitCastInst(Target, GenericFnTy, "", Call);
    std::vector<Value *> Args{TargetCasted, TargetName};
    Args.insert(Args.end(), Call->arg_operands().begin(),
                Call->arg_operands().end());

    // replace a call to `Target` with a corresponding call to `SpawnFn`
    auto *Replaced = CallInst::Create(SpawnFn, Args, "", Call);
    Call->replaceAllUsesWith(Replaced);
    Call->eraseFromParent();
  }
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  cl::ParseCommandLineOptions(argc, argv, "create server");

  LLVMContext &Context = getGlobalContext();
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);

  std::error_code EC;
  tool_output_file Out(OutputFilename, EC, sys::fs::F_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  emitGetTopOfStack(*M.get());
  createServer(*M.get());

  legacy::PassManager PM;
  PM.add(createBitcodeWriterPass(Out.os(), true));
  PM.run(*M.get());

  Out.keep();
}
