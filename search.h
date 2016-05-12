#ifndef _SEARCH_H_
#define _SEARCH_H_

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/CodeGen/MachineFunction.h>

#include "transform.h"
#include "replay_cli.h"

class Searcher {
  const unsigned Signal_penalty {1000000};

  // just read the paper dammit
  const float pc {0.16};
  const float po {0.5};
  const float ps {0.16};
  const float pi {0.16};
  // probability of deletion
  const float pu {0.16};
  const float beta {4.0};

  unsigned calculateCost(std::vector<response> &);
  double rand();

protected:
  llvm::Module *M;
  ReplayClient *Client;
  std::unique_ptr<Transformation> Transform;
  llvm::FunctionType *TargetTy;
  void transformRewrite();
  llvm::MachineFunction *copyFunction(llvm::MachineFunction *MF);
  unsigned calculateLatency(llvm::MachineFunction *MF);
  
public:
  Searcher(llvm::TargetMachine *TM,
           llvm::Module *MM,
           llvm::MachineFunction *MF,
           llvm::FunctionType *FnTy,
           ReplayClient *Cli);
  virtual llvm::MachineFunction *synthesize();
  virtual llvm::MachineFunction *optimize(int MaxItrs);
};

#endif
