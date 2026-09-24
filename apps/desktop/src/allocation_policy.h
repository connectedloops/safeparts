#pragma once

#include <memory>

enum class DesktopAllocationBoundary {
  EditorUtf8,
  FileAcquisition,
  RecoveryTransport,
  GeneratedPresentation,
  AccessibilityPresentation,
  RecoveredPresentation,
  ClipboardHandoff,
};

class DesktopAllocationPolicy {
public:
  virtual ~DesktopAllocationPolicy() = default;
  [[nodiscard]] virtual bool allow(DesktopAllocationBoundary boundary) = 0;
};

std::shared_ptr<DesktopAllocationPolicy> defaultDesktopAllocationPolicy();
