# The multiplayer create screen has no GAME MODS: ON/OFF option any more:
# multiplayer works like single player (the host ticks mods/game packs in the
# room). Run: cmake -DLOBBY=<src/client/gameview_lobby.cpp> -P no_game_mods_toggle.cmake
file(READ "${LOBBY}" text)
foreach(needle "\"GAME MODS: ON\"" "createGameMods")
  string(FIND "${text}" "${needle}" at)
  if(NOT at EQUAL -1)
    message(FATAL_ERROR "${LOBBY} still has the GAME MODS create option (${needle})")
  endif()
endforeach()
