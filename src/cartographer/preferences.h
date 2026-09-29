#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace cart {
struct EditorPreferences {
    int width=0,height=0,scalePercent=200;
    bool showFeatures=true,showUnits=true,showStarts=true,showRegions=true,showGrid=false;
    bool guideSeen=false;
    std::vector<std::string> recent;
    void remember(const std::string& request);
};
EditorPreferences loadEditorPreferences(const std::filesystem::path& folder);
bool saveEditorPreferences(const std::filesystem::path& folder,const EditorPreferences& preferences,std::string& error);
}
