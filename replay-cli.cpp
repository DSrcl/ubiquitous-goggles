#include <fstream>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "replay.h"

// send `libpath` to worker waiting on `sockpath` and get response
response runOneTest(std::string sockpath, std::string libpath)
{
  response result;
  sockaddr_un addr;

  auto sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0) {
    throw std::logic_error("Cannot create socket");
  }

  addr.sun_family = AF_UNIX;
  sockpath.copy(addr.sun_path, sockpath.size());
  addr.sun_path[sockpath.size()] = '\0';

  if (connect(sock, (sockaddr *)(&addr), sizeof(addr)) < 0) {
    close(sock);
    throw std::logic_error("Cannot connect to the address");
  }

  if (send(sock, libpath.c_str(), libpath.size(), 0) < 0) {
    close(sock);
    throw std::logic_error("Cannot send a lib path to a server");
  }

  if (recv(sock, &result, sizeof(result), 0) < 0) {
    close(sock);
    throw std::logic_error("Cannot receive response from the server");
  }

  close(sock);

  return result;
}

std::vector<response> runAllTests(std::vector<std::string> tests, std::string libpath)
{
  std::vector<response> testResults;
  for (const auto& sock : tests) {
    try {
      testResults.push_back(runOneTest(sock, libpath));
    }
    catch(const std::logic_error& err) {
      std::cerr << err.what() << std::endl;
    }
  }

  return testResults;
}

std::vector<std::string> getTests(const std::string& workerFile)
{
  std::vector<std::string> sockets;
  std::string line;
  std::ifstream socketsFile(workerFile.c_str());

  if (socketsFile.is_open()) {
    while (std::getline(socketsFile, line)) {
      sockets.push_back(line);
    }
  }

  return sockets;
}

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::cerr << "Libpath argument is missing" << std::endl;
    return 1;
  }

  auto tests = getTests("worker-data.txt");

  auto libname = argv[1];
  auto responses = runAllTests(tests, libname);

  for (const auto& resp : responses) {
    std::cout << "===========================================" << std::endl;
    std::cout << resp.msg << std::endl;
    std::cout << resp.dist << std::endl;
    std::cout << resp.success << std::endl;
  }
}
