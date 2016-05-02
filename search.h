#ifndef _SEARCH_H_
#define _SEARCH_H_

#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/CodeGen/MachineFunction.h>

#include "transform.h"
#include "replay_cli.h"

class Searcher {
  const unsigned Sigill_penalty {1000000};
  const unsigned Sigsegv_penalty {10000};
  const unsigned Sigbus_penalty {10000};
  const unsigned Sigfpe_penalty {10000};
  const unsigned Unknown_penalty {10000};

  // just read the paper dammit
  const float pc {0.16};
  const float po {0.5};
  const float ps {0.16};
  const float pi {0.16};
  // probability of deletion
  const float pu {0.16};
  const float beta {1.0};

  unsigned calculateCost(std::vector<response> &);
  double rand();

protected:
  llvm::Module *M;
  ReplayClient *Client;
  Transformation Transform;
  llvm::FunctionType *TargetTy;
  
public:
  Searcher(llvm::TargetMachine *TM,
           llvm::Module *MM,
           llvm::MachineFunction *MF,
           llvm::FunctionType *FnTy,
           ReplayClient *Cli);
  virtual llvm::MachineFunction *synthesize();
};

#endif
