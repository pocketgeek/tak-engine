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
# The 64-pack limit counts the always-enabled packs, and still allows 64 in all;
# a click on the GAME list keeps a saved tick that an always-enabled namesake hides.
foreach(needle "selection.size()+locked+1<=64" "[this,alwaysGame](std::vector<std::string> selection)")
  string(FIND "${text}" "${needle}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "${LOBBY} lacks the game pack list rule (${needle})")
  endif()
endforeach()
