/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "toolchain.h"
#include "logger.h"
#include "proc.h"
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <random>

#include "fs.h"

namespace
{
  std::atomic_bool installing{false};
}

void Utils::Toolchain::scan()
{
  //printf("Scanning for toolchain...\n");
  state = {};
  #if defined(_WIN32)

    // Scan "C:\" directories for anything containing "msys"
    state.mingwPath = fs::path{"C:\\msys64"};
    if(!fs::exists(state.mingwPath)) {
      state.mingwPath.clear();
      return;
    }

    if(state.mingwPath.empty())return;

    const char* n64InstEnv = std::getenv("N64_INST");
    // If N64_INST is defined in the system, the user probably already
    // has a working toolchain installation so try to use it.
    state.toolchainPath = (n64InstEnv != nullptr)?
      fs::path{n64InstEnv} : state.mingwPath / "pyrite64-sdk";

    state.hasToolchain = fs::exists(state.toolchainPath / "bin" / "mips64-elf-gcc.exe")
                       && fs::exists(state.toolchainPath / "bin" / "mips64-elf-g++.exe");

    state.hasLibdragon = fs::exists(state.toolchainPath / "bin" / "n64tool.exe")
                       && fs::exists(state.toolchainPath / "bin" / "mkdfs.exe")
                       && fs::exists(state.toolchainPath / "include" / "n64.mk");

    state.hasTiny3d = fs::exists(state.toolchainPath / "bin" / "gltf_to_t3d.exe")
                    && fs::exists(state.toolchainPath / "include" / "t3d.mk")
                    && fs::exists(state.toolchainPath / "mips64-elf" / "include" / "t3d");

  #else
    char* n64InstEnv = getenv("N64_INST");
    if(n64InstEnv) {
      state.toolchainPath = fs::path{n64InstEnv};
    }
    if(state.toolchainPath.empty())return;

    state.hasToolchain = fs::exists(state.toolchainPath / "bin" / "mips64-elf-gcc");
    if(!state.hasToolchain)return;

    state.hasLibdragon = fs::exists(state.toolchainPath / "bin" / "n64tool")
                       && fs::exists(state.toolchainPath / "bin" / "mkdfs")
                       && fs::exists(state.toolchainPath / "include" / "n64.mk");
    state.hasTiny3d = fs::exists(state.toolchainPath / "bin" / "gltf_to_t3d")
                    && fs::exists(state.toolchainPath / "include" / "t3d.mk")
                    && fs::exists(state.toolchainPath / "mips64-elf" / "include" / "t3d");
  #endif

  if(state.hasLibdragon && state.hasTiny3d)
  {
    auto rspqHeader = FS::loadTextFile(state.toolchainPath / "mips64-elf" / "include" / "rspq.h");
    auto fgeomHeader = FS::loadTextFile(state.toolchainPath / "mips64-elf" / "include" / "fgeom.h");
    auto t3dHeader = FS::loadTextFile(state.toolchainPath / "mips64-elf" / "include" / "t3d" / "t3d.h");

    state.upToDateLibs = true;
    if(!rspqHeader.contains("rspq_block_begin_reuse")) {
      printf("Libdragon out of date, missing 'rspq_block_begin_reuse' in rspq.h\n");
      state.upToDateLibs = false;
    }
    if(!rspqHeader.contains("rspq_block_set_placeholder")) {
      printf("Libdragon out of date, missing 'rspq_block_set_placeholder' in rspq.h\n");
      state.upToDateLibs = false;
    }
    if(!fgeomHeader.contains("operator==(fm_vec3_t")) {
      printf("Libdragon out of date, missing operator '==' for fm_vec3_t in fgeom.h\n");
      state.upToDateLibs = false;
    }
    if(!t3dHeader.contains("t3d_state_set_lighting_mode")) {
      printf("tiny3d out of date, missing 't3d_state_set_lighting_mode' in t3d.h\n");
      state.upToDateLibs = false;
    }
  }
}

namespace
{
  void runInstallScript(fs::path mingwPath, bool forceUpdate) {
    // C:\msys64\usr\bin\mintty.exe --hold=error /bin/env MSYSTEM=MINGW64 /bin/bash -l %self_path%mingw_create_env.sh
    auto minttyPath = mingwPath / "usr" / "bin" / "mintty.exe";
    if (!fs::exists(minttyPath)) {
      printf("Error: mintty.exe not found at expected location: %s\n", minttyPath.string().c_str());
      installing.store(false);
      return;
    }

    std::string envVars = "MSYSTEM=MINGW64 ";
    if (forceUpdate) envVars += "FORCE_UPDATE=true ";
    std::string command = minttyPath.string() + " --hold=error /bin/env " + envVars + "/bin/bash -l ";
    
    fs::path scriptPath = Utils::Proc::getAppResourcePath() / "data" / "scripts" / "mingw_create_env.sh";
    command += "\"" + scriptPath.string() + "\"";

    auto res = Utils::Proc::runSync(command);
    printf("Res: %s : %s\n", command.c_str(), res.c_str());
    installing.store(false);
  }
}

void Utils::Toolchain::install()
{
  if (installing.load()) {
    printf("Toolchain installation already in progress.\n");
    return;
  }

  installing.store(true);
  bool isInstalled = state.hasToolchain && state.hasLibdragon && state.hasTiny3d;
  std::thread installThread(runInstallScript, state.mingwPath, isInstalled);
  installThread.detach();
}

bool Utils::Toolchain::isInstalling()
{
  return installing.load();
}

bool Utils::Toolchain::runCmdSyncLogged(const std::string &cmd)
{
  #if defined(_WIN32)
    auto minttyPath = state.mingwPath / "usr" / "bin" / "bash.exe";

    // bash -lc starts in $HOME (login profile cd), which breaks any cmd that
    // uses paths relative to pyrite64.exe's CWD — mksprite/mkasset/audioconv64
    // from the asset builders, etc. We need to (a) cd back to the editor's
    // CWD inside bash, and (b) avoid cmd.exe-level quoting fragility (popen
    // wraps in cmd.exe /c, which doesn't honor single-quotes and splits on
    // bare && at the cmd.exe level).
    //
    // Robust answer: write the payload to a temp .sh and invoke bash -l on
    // it directly. No nested quoting, no cmd.exe operators leaking through.
    auto cwd = fs::current_path().string();
    std::string msysCwd;
    if (cwd.size() >= 2 && cwd[1] == ':') {
      msysCwd = "/";
      msysCwd += (char)std::tolower((unsigned char)cwd[0]);
      msysCwd += cwd.substr(2);
    } else {
      msysCwd = cwd;
    }
    for (char &c : msysCwd) if (c == '\\') c = '/';

    // The caller-built cmd uses Windows-native separators (e.g. mkSprite is
    // `\pyrite64-sdk\bin\mksprite`); convert to forward slashes before writing
    // so bash doesn't interpret the backslashes as escapes.
    std::string cmdNorm = cmd;
    for (char &c : cmdNorm) if (c == '\\') c = '/';

    // Per-invocation temp file so concurrent builds don't clobber each other.
    static std::atomic_uint64_t seq{0};
    uint64_t id = seq.fetch_add(1) ^ (uint64_t)std::random_device{}();
    auto tmpPath = fs::temp_directory_path() / ("pyrite64_cmd_" + std::to_string(id) + ".sh");
    {
      std::ofstream f{tmpPath};
      f << "#!/usr/bin/env bash\n";
      f << "cd \"" << msysCwd << "\" || exit 1\n";
      f << cmdNorm << "\n";
    }

    std::string scriptPath = tmpPath.string();
    for (char &c : scriptPath) if (c == '\\') c = '/';
    std::string command = minttyPath.string() + " -l " + scriptPath;
    for (char &c : command) if (c == '\\') c = '/';

    bool ok = Utils::Proc::runSyncLogged(command);
    std::error_code ec; fs::remove(tmpPath, ec);
    return ok;

  #else
    return Utils::Proc::runSyncLogged(cmd);
  #endif
}
