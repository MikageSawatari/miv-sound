#pragma once

#include "engine/automation_binding.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace effetune::vst::plugin {

#if defined(EFFETUNE_AUTOMATION_HOST_GATE_FIXTURE)
inline const Steinberg::FUID kProcessorId(0x8A41B5DC, 0x7E734D04, 0xB8DB87F7,
                                         0x017CB379);
#else
inline const Steinberg::FUID kProcessorId(0x1ED3877F, 0xD2CE46D6, 0x896338DD,
                                         0xBBE03E72);
#endif
inline constexpr Steinberg::Vst::ParamID kBypassParameterId = 0;
inline constexpr Steinberg::Vst::ParamID kFirstAutomationParameterId = 0x00010000u;
inline constexpr Steinberg::Vst::ParamID kLastAutomationParameterId =
    kFirstAutomationParameterId + kAutomationSlotCount - 1u;

[[nodiscard]] inline constexpr Steinberg::Vst::ParamID
automationParameterId(const std::uint32_t slot) noexcept {
  return kFirstAutomationParameterId + slot;
}

} // namespace effetune::vst::plugin
