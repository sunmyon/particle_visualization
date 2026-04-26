#include "JupyterLauncher.h"
#include <random>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>

#include "platform/shell_utils.h"

static std::string rand_hex(size_t n){
  static std::mt19937_64 rng{std::random_device{}()};
  static const char* h="0123456789abcdef";
  std::string s; s.resize(n);
  for(size_t i=0;i<n;i++) s[i]=h[rng()&15];
  return s;
}
static int pick_port(){ return 8888 + (int)(std::random_device{}()%50); }

// jupyter 実行ファイルのパスが怪しい場合、フルパスに置き換える（例: /usr/local/bin/jupyter や $HOME/.pyenv/...）
static const char* JUPYTER = "jupyter";

static bool wait_until_ready(const std::string& logPath, int port, int timeout_ms=15000){
  // ログに "http://127.0.0.1:PORT" が現れるまで待つ（最大 timeout_ms）
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while(std::chrono::steady_clock::now() < deadline){
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::ifstream ifs(logPath);
    if(!ifs) continue;
    std::string line;
    while(std::getline(ifs, line)){
      if(line.find("http://127.0.0.1:"+std::to_string(port)) != std::string::npos) return true;
      if(line.find("http://localhost:"+std::to_string(port)) != std::string::npos) return true;
      // エラー兆候があれば早期リターンしてもよい
      if(line.find("Error") != std::string::npos || line.find("CRITICAL") != std::string::npos) return false;
    }
  }
  return false;
}

bool launchJupyterNotebook(const std::string& workdir, JupyterInfo& out){
  std::filesystem::create_directories(workdir);
  const int port = pick_port();
  const std::string token = rand_hex(24);
  const std::string logPath = workdir + "/jupyter.log";

  // まずは 7系/Server2 系を想定した起動
  std::stringstream cmd1;
  cmd1 << JUPYTER << " server --no-browser"
       << " --ServerApp.ip=127.0.0.1"
       << " --ServerApp.port=" << port
       << " --ServerApp.port_retries=0"
       << " --ServerApp.token=" << token
       << " --ServerApp.root_dir=" << ShellQuote(workdir)
       << " > " << ShellQuote(logPath) << " 2>&1 &";

  int rc = std::system(cmd1.str().c_str());

  // フォールバック: 6系（NotebookApp.*）
  if(rc != 0){
    std::stringstream cmd2;
    cmd2 << JUPYTER << " notebook --no-browser"
         << " --NotebookApp.ip=127.0.0.1"
         << " --NotebookApp.port=" << port
         << " --NotebookApp.port_retries=0"
         << " --NotebookApp.token=" << token
         << " --NotebookApp.notebook_dir=" << ShellQuote(workdir)
         << " > " << ShellQuote(logPath) << " 2>&1 &";
    rc = std::system(cmd2.str().c_str());
    if(rc != 0) return false;
  }

  // 起動完了を待つ
  if(!wait_until_ready(logPath, port, 20000)){ // 20秒まで待つ
    // ここでログを見てユーザに伝えるなら return false の代わりに out にログパスだけ返すなど
    return false;
  }

  std::stringstream url; url << "http://127.0.0.1:" << port << "/tree?token=" << token;

#if defined(__APPLE__)
  std::string openCmd = "open " + ShellQuote(url.str());
#elif defined(__linux__)
  std::string openCmd = "xdg-open " + ShellQuote(url.str());
#else
  std::string openCmd = "";
#endif
  if(!openCmd.empty()) std::system(openCmd.c_str());

  out.port = port; out.token = token; out.url = url.str();
  return true;
}
