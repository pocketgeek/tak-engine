#pragma once

#include <cstdint>

namespace tak {

enum class RetailFlightAnimationCall {
    BeginFlight,
    BeginLanding,
    EndTransport,
};

struct RetailFlightAnimationState {
    uint32_t beginFlightSerial = 0;
    uint32_t landingSerial = 0;
    bool airborne = false;
    bool beginFlightCallbackPending = false;
    bool landingCallbackPending = false;
};

// Retail queues BeginFlight when a flight mission starts and queues landing
// callbacks when a landing site is accepted, before the final descent runs.
// Mode transitions are fallbacks only when their native call-ins were absent.
template<class Start>
void updateRetailFlightAnimation(RetailFlightAnimationState& state,
        bool airborne,uint32_t beginFlightSerial,uint32_t landingSerial,
        bool canTransport,Start&& start) {
    const uint32_t beginFlightEvents = beginFlightSerial - state.beginFlightSerial;
    const bool landingEvent = landingSerial != state.landingSerial;
    if (airborne != state.airborne) {
        state.airborne = airborne;
        if (airborne) {
            state.landingCallbackPending = false;
            if (!beginFlightEvents && !state.beginFlightCallbackPending)
                start(RetailFlightAnimationCall::BeginFlight);
            state.beginFlightCallbackPending = false;
        } else {
            if (!landingEvent && !state.landingCallbackPending)
                start(RetailFlightAnimationCall::BeginLanding);
            state.beginFlightCallbackPending = false;
            state.landingCallbackPending = false;
        }
    }
    if (beginFlightEvents) {
        for (uint32_t i=0;i<beginFlightEvents;++i)
            start(RetailFlightAnimationCall::BeginFlight);
        state.beginFlightSerial = beginFlightSerial;
        state.beginFlightCallbackPending = !airborne;
    }
    if (landingEvent) {
        if (canTransport) start(RetailFlightAnimationCall::EndTransport);
        start(RetailFlightAnimationCall::BeginLanding);
        state.landingSerial = landingSerial;
        state.beginFlightCallbackPending = false;
        state.landingCallbackPending = airborne;
    }
}

} // namespace tak
