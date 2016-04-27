#include <llvm/Target/TargetMachine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <fstream>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <string>
#include <cstdio>

#include "mf_instrument.h"
#include "mf_compiler.h"
#include "replay.h"
#include "replay_cli.h"

using namespace llvm;

struct ReplayClient::ClientImpl {
  struct Worker {
    size_t FrameBegin, FrameSize;
    std::string Address;
  };

  std::vector<Worker> Workers;
  size_t JmpbufAddr;
  TargetMachine *TM;

  Instrumenter *Instrumenter_;

  int connectToAddr(const std::string sockpath, sockaddr_un &addr) {
    auto sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
      errs() << "Cannot create socket\n";
      exit(1);
    }

    addr.sun_family = AF_UNIX;
    sockpath.copy(addr.sun_path, sockpath.size());
    addr.sun_path[sockpath.size()] = '\0';

    if (connect(sock, (sockaddr *)(&addr), sizeof(addr)) < 0) {
      close(sock);
      errs() << "Cannot connect to address " << sockpath << "\n";
      exit(1);
    }

    return sock;
  }

  // send `libpath` to worker waiting on `sockpath` and get response
  response runOneTest(std::string sockpath, std::string libpath) {
    response result;
    sockaddr_un addr; 
    auto sock = connectToAddr(sockpath, addr);

    if (send(sock, libpath.c_str(), libpath.size(), 0) < 0) {
      close(sock);
      errs() << "Cannot send a lib path to a server\n";
      exit(1);
    }

    if (recv(sock, &result, sizeof(result), 0) < 0) {
      close(sock);
      errs() << "Cannot receive response from the server\n";
    }

    close(sock);

    return result;
  }

  std::vector<response> runAllTests(std::string libpath) {
    std::vector<response> TestResults;
    // TODO this is embarssinly parallel
    for (const auto &W : Workers) {
      TestResults.push_back(runOneTest(W.Address, libpath));
    }

    return TestResults;
  }

  ClientImpl(TargetMachine *TheTM,
             const std::string &WorkerFilename,
             const std::string &JmpbufFilename) : TM(TheTM) {
    std::string line;
    std::ifstream WorkerFile(WorkerFilename);
    std::ifstream JmpbufFile(JmpbufFilename);

    Worker W;
    std::string Str;
    if (WorkerFile.is_open()) {
      while (std::getline(WorkerFile, line)) {
        std::stringstream fields(line);
        std::getline(fields, W.Address, ',');
        std::getline(fields, Str, ',');
        W.FrameBegin = std::stol(Str);
        std::getline(fields, Str, ',');
        W.FrameSize = std::stol(Str);
        Workers.push_back(W);
      }
    }

    if (JmpbufFile.is_open()) {
      JmpbufFile >> JmpbufAddr;
    }

    Instrumenter_ = getInstrumenter(TM);
  }

  void instrument(Module *M, FunctionType *FnTy, MachineFunction *Rewrite) {
    assert(Rewrite->size() == 1 && "no support for branches yet");

    auto &Ctx = getGlobalContext();
    M->setDataLayout(*TM->getDataLayout());
    M->getOrInsertFunction("rewrite", FnTy);

    auto &MBB = *Rewrite->begin();
    // FIXME actually compile `Rewrite` multiple times for different worker process
    // for now just assume all the worker uses the same stack frame
    const auto &W = Workers[0];
    auto RetRegs = Instrumenter_->getReturnRegs(FnTy);
    Instrumenter_->protectRTFrame(MBB, W.FrameBegin, W.FrameSize);
    Instrumenter_->unprotectRTFrame(MBB, W.FrameBegin, W.FrameSize);
    Instrumenter_->dumpRegisters(*M, MBB, RetRegs);
    Instrumenter_->instrumentToReturn(*Rewrite, JmpbufAddr);
  }

  std::string compile(Module *M, MachineFunction *Rewrite) {
    const std::string RewriteObj = std::tmpnam(nullptr);
    errs() << "compiling rewrite\n";
    compileToObjectFile(*M, *Rewrite, RewriteObj, TM);
    const std::string RewriteLib = std::tmpnam(nullptr);
    errs() << "linking rewrite\n";
    std::system(("cc -shared "+RewriteObj+" -o "+RewriteLib).c_str());
    std::remove(RewriteObj.c_str());
    return RewriteLib;
  }

  void killAllWorkers() {
    sockaddr_un Addr;

    for (const auto &W : Workers) {
      auto sock = connectToAddr(W.Address, Addr); 
      if (send(sock, "", 1, 0) < 0) {
        std::perror("send");
        errs() << "cannot send kill msg\n";
        exit(1);
      }
    }
  }
  
};

ReplayClient::ReplayClient(TargetMachine *TM,
                           const std::string &WorkerFile,
                           const std::string &JmpbufFile)
    : Impl(new ClientImpl(TM, WorkerFile, JmpbufFile)) {}


// kill all the workers
ReplayClient::~ReplayClient()
{
  Impl->killAllWorkers();
}

std::vector<response> ReplayClient::testRewrite(Module *M, FunctionType *FnTy, MachineFunction *Rewrite)
{ 
  // path to the implementation
  Impl->instrument(M, FnTy, Rewrite);
  errs() << "Instrumented Rewrite\n";
  std::string Libpath = Impl->compile(M, Rewrite);
  errs() << "Compiled rewrite to " << Libpath << "\n";
  /////////
  for (int i = 0; i < 1e4; i++) {
    errs() << "!!! " << i << "\n";
    Impl->runAllTests(Libpath); 
  }
  //////////
  auto Result = Impl->runAllTests(Libpath);
  std::remove(Libpath.c_str());
  return Result;
}
