#include "PythonBridge/python_bridge_launcher.h"

#include <cstdlib>
#include <string>

#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "data/particle_array.h"

void OpenPythonBridge(ParticleArray& particles, PythonBridgeState& py)
{
  if (py.ptr) {
    return;
  }

  py.ptr.reset(CreatePythonBridge());
  if (!py.ptr) {
    py.launched = false;
    return;
  }

  const uint64_t N = static_cast<uint64_t>(particles.particleBlock.particles.size());
  if (!py.ptr->init(N, true, "cppvis_pos")) {
    py.ptr.reset();
    py.launched = false;
    return;
  }

  bridge::loadInitialFromAoS(*py.ptr, particles, sizeof(ParticleData));
  py.launched = py.ptr->launchNotebook("./jupyter_work");
}

void ClosePythonBridge(PythonBridgeState& py)
{
  if (!py.ptr) {
    return;
  }

  py.ptr->shutdown();
  py.ptr.reset();
  py.launched = false;
  py.needUploadPos = false;
}

void OpenPythonBridgeInBrowser(const PythonBridgeState& py)
{
  if (!py.ptr || !py.launched) {
    return;
  }

  const auto& info = py.ptr->notebookInfo();

#if defined(__APPLE__)
  std::string cmd = "open \"" + info.url + "\"";
  std::system(cmd.c_str());
#elif defined(__linux__)
  std::string cmd = "xdg-open \"" + info.url + "\"";
  std::system(cmd.c_str());
#elif defined(_WIN32)
  std::string cmd = "cmd /c start \"\" \"" + info.url + "\"";
  std::system(cmd.c_str());
#endif
}
