/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "cli.h"
#include "argparse/argparse.hpp"
#include "build/projectBuilder.h"
#include "cli/cliCommands.h"
#include "context.h"
#include "project/project.h"
#include "utils/logger.h"

namespace
{
  std::string argProgPath{};
  bool argExperimental{false};
}

const std::string& CLI::getProjectPath()
{
  return argProgPath;
}

bool CLI::isExperimentalEnabled()
{
  return argExperimental;
}

CLI::Result CLI::run(int argc, char** argv)
{
  argparse::ArgumentParser prog{"pyrite64", PYRITE_VERSION};
  prog.add_argument("--cli")
   .help("Run in CLI mode (no GUI)")
   .default_value(false)
   .implicit_value(true);

  // Cmd is a free-form string here (validated against build/clean and the
  // cliCommands.cpp registry below) so adding new sub-commands does not need
  // to touch this file.
  prog.add_argument("--cmd")
    .help("Command to run: build, clean, or one of the asset/prefab tooling commands. See --cmd help for the full list.")
    .default_value(std::string{});

  prog.add_argument("--experimental")
    .help("Enable experimental features (may cause instability / break projects)")
    .default_value(false)
    .implicit_value(true);

  prog.add_argument("project")
    .default_value("")
    .help("Path to project file (.p64proj)")
  ;

  CLI::Commands::registerFlags(prog);

  argProgPath = {};
  try {
    prog.parse_args(argc, argv);
  }
  catch (const std::exception& err) {
    std::cerr << err.what() << std::endl;
    std::cerr << prog;
    CLI::Commands::printExtendedHelp();
    return Result::ERROR;
  }

  argExperimental = prog["--experimental"] == true;
  argProgPath = prog.get<std::string>("project");

  if (prog["--cli"] == false) {
    return Result::GUI;
  }

  auto cmd = prog.get<std::string>("--cmd");

  // Make every CLI write hit the OS immediately. Default Windows stdio is
  // fully-buffered when the handle isn't a console (e.g. `> file` redirect),
  // which means a crash mid-build loses every queued log line and leaves the
  // user with a 0-byte log file. Diagnostics first, performance second.
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);

  Utils::Logger::setOutput([](const std::string &msg) {
    fputs(msg.c_str(), stdout);
  });

  if (cmd == "help" || cmd.empty()) {
    fputs("Pyrite64 - CLI\n", stdout);
    std::cout << prog;
    CLI::Commands::printExtendedHelp();
    return cmd.empty() ? Result::ERROR : Result::SUCCESS;
  }

  printf("Pyrite64 - CLI\n");
  bool res = false;

  if (cmd == "build") {
    if (argProgPath.empty()) { fputs("error: project path required\n", stderr); return Result::ERROR; }
    printf("Building project: %s\n", argProgPath.c_str());
    res = Build::buildProject(argProgPath);
  }
  else if (cmd == "build-tables")
  {
    // Editor-side smoke test: runs every codegen step but skips the final
    // `make` so it doesn't need the N64 toolchain. See scripts/smoke_test.sh.
    if (argProgPath.empty()) { fputs("error: project path required\n", stderr); return Result::ERROR; }
    printf("Building tables only (no make): %s\n", argProgPath.c_str());
    res = Build::buildProject(argProgPath, /*runMake=*/false);
  }
  else if (cmd == "clean")
  {
    if (argProgPath.empty()) { fputs("error: project path required\n", stderr); return Result::ERROR; }
    printf("Cleaning project: %s\n", argProgPath.c_str());
    Project::Project project{argProgPath};
    res = Build::cleanProject(project, {
      .code = true,
      .assets = true,
      .engine = true,
      .engineSrc = true
    });
  }
  else if (CLI::Commands::isExtendedCmd(cmd))
  {
    CLI::Commands::Args cliArgs{};
    cliArgs.cmd = cmd;
    CLI::Commands::readArgs(prog, cliArgs);

    // project-create bootstraps a project from the empty template. It must
    // run before any Project::Project ctor, otherwise opening a not-yet-
    // existent .p64proj throws. The handler does not need a Project ref.
    if (cmd == "project-create" || cmd == "project-templates") {
      int rc = CLI::Commands::dispatchBootstrap(cliArgs);
      res = (rc == 0);
      return res ? Result::SUCCESS : Result::ERROR;
    }

    if (argProgPath.empty()) { fputs("error: project path required\n", stderr); return Result::ERROR; }
    Project::Project project{argProgPath};
    // Components and Prefab::save() reach into ctx.project. The build/clean
    // paths happen to not need it, but the asset-tooling commands often do.
    ctx.project = &project;
    int rc = CLI::Commands::dispatch(cliArgs, project);
    ctx.project = nullptr;
    res = (rc == 0);
  }
  else {
    fprintf(stderr, "error: unknown --cmd '%s'\n", cmd.c_str());
    CLI::Commands::printExtendedHelp();
    res = false;
  }

  return res ? Result::SUCCESS : Result::ERROR;
}
