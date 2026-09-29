#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace cart {
struct EditorPreferences {
    int width=0,height=0,scalePercent=200;
    std::vector<std::string> recent;
    void remember(const std::string& request);
};
EditorPreferences loadEditorPreferences(const std::filesystem::path& folder);
bool saveEditorPreferences(const std::filesystem::path& folder,const EditorPreferences& preferences,std::string& error);
}
