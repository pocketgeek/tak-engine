#pragma once

// Standalone 3DO model viewer (the `takclient model` mode): loads a model + its
// textures + an optional COB script and draws it spinning. Extracted from
// client/main.cpp; kept at global scope so its unqualified use sites there are
// unchanged.

#include <SDL.h>

#include "client/modelmath.h"   // Tri / Xform (tris_ is std::vector<Tri>)
#include "cob/vm.h"             // std::unique_ptr<tak::cob::Vm> member
#include "tdo/tdo.h"            // tak::tdo::Model (by-value member) / Object

#include <map>
#include <memory>
#include <string>
#include <vector>

class ModelView {
public:
    ModelView(SDL_Renderer* ren, const std::string& path, const std::string& texDir,
              const std::string& palettePath, const std::string& cobPath,
              const std::string& anim);

    void input(const SDL_Event& e);
    void draw(int winW, int winH, float dt);
    void advance(float seconds);

private:
    void loadTextures(const std::string& texDir, const std::string& palettePath);
    void project(float x, float y, float z, SDL_FPoint& out, float& depth) const;
    const tak::cob::PieceState* pieceFor(const std::string& objName) const;
    void walk(const tak::tdo::Object& o, const Xform& parent);

    SDL_Renderer* ren_;
    tak::tdo::Model model_;
    std::unique_ptr<tak::cob::Vm> vm_;
    std::vector<std::string> pieceNames_;
    std::map<std::string, SDL_Texture*> textures_;
    std::vector<Tri> tris_;
    float yaw_ = 0.7f, pitch_ = 0.4f, zoom_ = 1.0f, fit_ = 1.0f;
    bool spin_ = true, fitted_ = false;
};
