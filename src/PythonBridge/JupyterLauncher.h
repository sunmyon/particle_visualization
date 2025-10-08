#pragma once
#include <string>

struct JupyterInfo { int port=0; std::string token; std::string url; };

bool launchJupyterNotebook(const std::string& workdir, JupyterInfo& out);
