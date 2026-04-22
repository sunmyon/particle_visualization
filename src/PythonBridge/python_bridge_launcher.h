#pragma once

class ParticleArray;
struct PythonBridgeState;

void OpenPythonBridge(ParticleArray& particles, PythonBridgeState& py);
void ClosePythonBridge(PythonBridgeState& py);
void OpenPythonBridgeInBrowser(const PythonBridgeState& py);
