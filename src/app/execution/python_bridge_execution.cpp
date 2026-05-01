#ifdef PYTHON_BRIDGE
#include <cstdlib>
#include <string>

#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/BridgeAdapter.h"
#include "app/state/runtime_state.h"
#include "app/app_services.h"
#include "data/simulation_dataset.h"
#include "platform/shell_utils.h"

namespace {
void SyncPythonBridgeView(const PythonBridgeState& service,
                          PythonBridgeViewState& view)
{
  view.available = (service.ptr != nullptr);
  view.launched  = service.launched;

  if (!service.ptr) {
    view.port = -1;
    view.url.clear();
    view.token.clear();
    return;
  }

  const auto& info = service.ptr->notebookInfo();
  view.port  = info.port;
  view.url   = info.url;
  view.token = info.token;
}

void OpenPythonBridgeInBrowser(const PythonBridgeViewState& view)
{
  if (!view.launched || view.url.empty())
    return;

#if defined(__APPLE__)
  std::string cmd = "open " + ShellQuote(view.url);
  std::system(cmd.c_str());
#elif defined(__linux__)
  std::string cmd = "xdg-open " + ShellQuote(view.url);
  std::system(cmd.c_str());
#elif defined(_WIN32)
  std::string cmd = "cmd /c start \"\" \"" + view.url + "\"";
  std::system(cmd.c_str());
#endif
}
} // namespace

void ExecutePythonBridgeRequests(SimulationDataset& particles,
                                 PythonBridgeState& service,
                                 PythonBridgeRequestState& request,
                                 PythonBridgeViewState& view)
{
  if (request.shutdownRequested) {
    if (service.ptr) {
      service.ptr->shutdown();
      service.ptr.reset();
    }
    service.launched = false;
    service.needUploadPos = false;
    view.lastError.clear();
    request.shutdownRequested = false;
  }

  if (request.launchRequested) {
    view.lastError.clear();

    if (!service.ptr) {
      service.ptr.reset(CreatePythonBridge());
      if (!service.ptr) {
        view.lastError = "Bridge creation failed";
      } else {
        const uint64_t N =
          static_cast<uint64_t>(particles.simulationBlock.particles.size());

        if (!service.ptr->init(N, /*withB=*/true, "cppvis_pos")) {
          view.lastError = service.ptr->lastError().empty()
                             ? "Bridge init failed"
                             : service.ptr->lastError();
          service.ptr.reset();
        } else {
          bridge::loadInitialFromAoS(*service.ptr, particles);
          service.launched = service.ptr->launchNotebook("./jupyter_work");
          if (!service.launched) {
            view.lastError = service.ptr->lastError().empty()
                               ? "Notebook launch failed"
                               : service.ptr->lastError();
          }
        }
      }
    }

    request.launchRequested = false;
  }

  SyncPythonBridgeView(service, view);

  if (request.openBrowserRequested) {
    OpenPythonBridgeInBrowser(view);
    request.openBrowserRequested = false;
  }
}
#endif
