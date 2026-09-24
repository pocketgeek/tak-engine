#pragma once

#include <cstdint>

namespace tak {

enum class RetailFlightAnimationCall {
    BeginFlight,
    BeginLanding,
    EndTransport,
};

struct RetailFlightAnimationState {
    uint32_t landingSerial = 0;
    bool airborne = false;
    bool landingCallbackPending = false;
};

// Retail queues these COB callbacks when its landing mission accepts a site,
// before the final descent controller runs. The mode transition at touchdown
// is only a fallback for transitions without that mission callback.
template<class Start>
void updateRetailFlightAnimation(RetailFlightAnimationState& state,
        bool airborne,uint32_t landingSerial,bool canTransport,Start&& start) {
    const bool landingEvent = landingSerial != state.landingSerial;
    if (airborne != state.airborne) {
        state.airborne = airborne;
        if (airborne) {
            state.landingCallbackPending = false;
            start(RetailFlightAnimationCall::BeginFlight);
        } else {
            if (!landingEvent && !state.landingCallbackPending)
                start(RetailFlightAnimationCall::BeginLanding);
            state.landingCallbackPending = false;
        }
    }
    if (landingEvent) {
        if (canTransport) start(RetailFlightAnimationCall::EndTransport);
        start(RetailFlightAnimationCall::BeginLanding);
        state.landingSerial = landingSerial;
        state.landingCallbackPending = airborne;
    }
}

} // namespace tak
