#include "allocation_policy.h"

namespace {
class AllowDesktopAllocations final : public DesktopAllocationPolicy {
public:
  bool allow(DesktopAllocationBoundary) override { return true; }
};
} // namespace

std::shared_ptr<DesktopAllocationPolicy> defaultDesktopAllocationPolicy() {
  static const auto policy = std::make_shared<AllowDesktopAllocations>();
  return policy;
}
