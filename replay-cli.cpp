#include <sys/socket.h>
#include <sys/un.h>
#include <fstream>
#include <string>

#include "replay.h"

// send `libpath` to worker waiting on `sockpath` and get response
response *runOneTest(std::string sockpath, std::string libpath)
{
}

std::vector<response> runAllTests(std::vector<std::string> tests, std::string libpath)
{
}

std:vector<std::string> getTests(char *workerFile)
{
}

int main(int argc, char **argv)
{
  auto tests = getTests("worker-data.txt");
  auto responses = runAllTests(tests);
}
