#include "JupyterLauncher.h"
#include <random>
#include <sstream>
#include <cstdlib>
#include <filesystem>

static std::string rand_hex(size_t n){
  static std::mt19937_64 rng{std::random_device{}()};
  static const char* h="0123456789abcdef";
  std::string s; s.resize(n);
  for(size_t i=0;i<n;i++) s[i]=h[rng()&15];
  return s;
}
static int pick_port(){ return 8888 + (int)(std::random_device{}()%50); }

bool launchJupyterNotebook(const std::string& workdir, JupyterInfo& out){
  std::filesystem::create_directories(workdir);
  int port = pick_port(); std::string token = rand_hex(24);
  std::stringstream cmd;
  cmd << "jupyter notebook --no-browser"
      << " --NotebookApp.port=" << port
      << " --NotebookApp.port_retries=0"
      << " --NotebookApp.token=" << token
      << " --NotebookApp.notebook_dir=\"" << workdir << "\""
      << " > \"" << workdir << "/jupyter.log\" 2>&1 &";
  int rc = std::system(cmd.str().c_str());
  if(rc!=0) return false;
  std::stringstream url; url << "http://127.0.0.1:" << port << "/tree?token=" << token;
#if defined(__APPLE__)
  std::string openCmd = "open \"" + url.str() + "\"";
#elif defined(__linux__)
  std::string openCmd = "xdg-open \"" + url.str() + "\"";
#else
  std::string openCmd = ""; // Windowsは後で対応
#endif
  if(!openCmd.empty()) std::system(openCmd.c_str());
  out.port=port; out.token=token; out.url=url.str();
  return true;
}
