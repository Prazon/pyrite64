// Headless asset/prefab tooling for the --cli mode.
// Pairs with src/cli.cpp which routes --cmd here when the requested command
// isn't build/clean. Designed for LLM/script consumption: read commands emit
// JSON to stdout, mutations echo the post-state JSON, and every command
// returns 0 on success / non-zero on error.
#pragma once
#include <string>

namespace argparse { class ArgumentParser; }
namespace Project { class Project; }

namespace CLI::Commands
{
  struct Args
  {
    std::string cmd;
    std::string asset;
    std::string type;
    std::string name;
    std::string dir;
    std::string file;
    std::string dest;
    std::string field;
    std::string value;
    std::string path;
    std::string parent;
    std::string comp;
    std::string func;
    std::string from;
    std::string to;
    std::string restype;
  };

  // Returns true iff `cmd` is one we handle here (not build/clean/empty).
  bool isExtendedCmd(const std::string &cmd);

  void registerFlags(argparse::ArgumentParser &prog);
  void readArgs(argparse::ArgumentParser &prog, Args &args);

  // Caller-owned project; the dispatcher does not save the project itself
  // (each command knows whether it needs to write).
  int dispatch(const Args &args, Project::Project &project);

  // Emits the trailing help block printed alongside argparse's own --help.
  void printExtendedHelp();
}
