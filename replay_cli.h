#include <llvm/Target/TargetMachine.h>
#include <llvm/CodeGen/MachineFunction.h>

#include <memory>
#include "replay.h"

class ReplayClient {
  struct ClientImpl;
  std::unique_ptr<ClientImpl> Impl;

public:
  ReplayClient(llvm::TargetMachine *TM, const std::string &WorkerFile,
               const std::string &JmpbufFile);

  // run an uninstrumented rewrite
  // and report the result
  std::vector<response> testRewrite(llvm::Module *M, llvm::FunctionType *FnTy,
                                    llvm::MachineFunction *Rewrite);
  ~ReplayClient();
};
