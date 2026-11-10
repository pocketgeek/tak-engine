#pragma once

// What a guest is shown when the host changes the game room: which lines flash and the
// short note under the controls ("HOST CHANGED FOG OF WAR -- READY AGAIN"). Pure (no SDL),
// so tools/room_changes_test.cpp can drive it; the room screen (gameview_room.cpp) diffs
// each new LobbyState against the last one it saw and draws the result.

#include <cstdint>
#include <string>
#include <vector>

namespace tak::net { struct RoomView; }

namespace tak {

// One line of the room a change can light: the map and its MAP SCENARIO, the game's name,
// each cell of the options block, the slot table, the mods line.
enum class RoomField : uint8_t { Map, Scenario, Name, Crusades, DoubleSight, UnitCap, Monarch,
    SpeedUnlock, Fog, StartLocations, Pathfinding, PowerCodes, AllowSpectators, Players, Mods, kCount };
const char* roomFieldCaption(RoomField);   // "FOG OF WAR", ... (for the notes)

// What changed between two LobbyStates of one room.
struct RoomChanges {
    uint8_t bits = 0;                  // the server's changeBits for a new roomRev (0: none)
    bool name = false, password = false;
    std::vector<RoomField> options;    // option cells whose value changed
    std::vector<int> seats;            // seats whose record changed (Players: rows to flash)
    int aiRemoved = 0;                 // AIs gone from seats at or past a lowered capacity
    bool unreadied = false;            // my READY went 1 -> 0 with a new roomRev
    int movedTo = -1;                  // the server moved me (mySlot changed without my MoveSeat)
};
RoomChanges roomChanges(const net::RoomView& before, const net::RoomView& after);
std::string roomChangeNote(const RoomChanges&, bool host);   // "" for none

}  // namespace tak
