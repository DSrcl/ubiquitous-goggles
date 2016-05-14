#ifndef _REPLAY_CLI_H_
#define _REPLAY_CLI_H_

#include <llvm/Target/TargetMachine.h>
#include <llvm/CodeGen/MachineFunction.h>

#include <memory>
#include "replay.h"

// a replay client is responsible for
//  1) establishing connection with the server
//  2) rewrite a rewrite to be tested such that it can be used by the server
//  3) compile the rewrite and send request to the server to run the rewrite
//  4) return a vector of response back from the server
//
// from the perspective of a user, this a "magic" class that calculates distance
// of an arbitrary rewrite from the target
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

#endif
