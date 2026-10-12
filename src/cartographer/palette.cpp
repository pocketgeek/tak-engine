// The left browser: section / feature / unit / start lists with search,
// category and faction filters, the thumbnail grid, and its input handling.

#include "cartographer/editorstate.h"
#include "cartographer/dialog.h"

const cart::FeatSprite* cart::Editor::featureSpriteFor(const cart::FeatureRef& ref) {
    return interactive?features.requestSprite(vfs,ref):features.sprite(vfs,ref);
}

SDL_Texture* cart::Editor::featTextureFor(const cart::FeatureRef& r) {
    auto it = featTex.find(r.name);
    if (it != featTex.end()) return it->second;
    SDL_Texture* t = nullptr;
    const cart::FeatSprite* sp = featureSpriteFor(r);
    if(!sp)return nullptr; // Pending work must not become a cached failure.
    if (sp && sp->w > 0) {
        t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                              sp->w, sp->h);
        if (t) {
            SDL_UpdateTexture(t, nullptr, sp->rgba.data(), sp->w * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        }
    }
    featTex[r.name] = t;
    return t;
}

SDL_Texture* cart::Editor::thumbFor(const std::string& path) {return thumbs.get(path);}

const tak::sim::UnitType* cart::Editor::unitInfo(int index) {return index>=0 && index<int(unitTypes.size())?unitRegistry.find(lowerText(unitTypes[size_t(index)])):nullptr;}

std::string cart::Editor::unitCategory(int index) {
    const auto* type=unitInfo(index);if(!type)return "OTHER";
    if(type->onMana)return "MANA";
    if(type->isBuilder)return "BUILDERS";
    if(type->isStructure())return "BUILDINGS";
    if(type->canFly)return "AIR";
    if(type->domain==tak::sim::UnitType::Domain::Water)return "NAVAL";
    return "GROUND";
}

std::string cart::Editor::paletteLabel(int index) {
    if(tool==UNITS) {const auto* info=unitInfo(index);return info?info->name+" ("+unitTypes[size_t(index)]+")":unitTypes[size_t(index)];}
    if(tool==FEATURES)return features.list()[size_t(index)].name;
    if(tool==STARTS)return "PLAYER "+std::to_string(scenario.starts[size_t(index)].number);
    return sections.list()[size_t(index)].name;
}

void cart::Editor::refreshPalette() {
    const auto key=std::to_string(int(tool))+"/"+std::to_string(categoryIndex)+"/"+std::to_string(factionIndex)+"/"+world+"/"+paletteSearch+"/"+std::to_string(scenario.starts.size());
    if(key==paletteKey)return;
    const bool toolChanged=paletteKey.empty() || paletteKey[0]!=key[0];
    paletteKey=key;
    const int count=tool==UNITS?int(unitTypes.size()):tool==FEATURES?int(features.list().size()):tool==STARTS?int(scenario.starts.size()):int(sections.list().size());
    auto category=[&](int i) {return tool==UNITS?unitCategory(i):tool==FEATURES?features.list()[size_t(i)].category:tool==STARTS?std::string("STARTS"):sections.list()[size_t(i)].category;};
    std::set<std::string> categories;for(int i=0;i<count;++i)categories.insert(category(i));
    paletteCategories={"ALL"};paletteCategories.insert(paletteCategories.end(),categories.begin(),categories.end());
    if(toolChanged || categoryIndex>=int(paletteCategories.size()))categoryIndex=0;
    paletteItems.clear();const auto query=lowerText(paletteSearch);
    static const char* sides[]={"","ara","tar","ver","zon","cre"};
    for(int i=0;i<count;++i) {
        if(categoryIndex && category(i)!=paletteCategories[size_t(categoryIndex)])continue;
        if(tool==UNITS && factionIndex && !lowerText(unitTypes[size_t(i)]).starts_with(sides[factionIndex]))continue;
        if(!query.empty() && lowerText(paletteLabel(i)+" "+category(i)).find(query)==std::string::npos)continue;
        paletteItems.push_back(i);
    }
    paletteScroll=0;
}

int cart::Editor::paletteRowAt(int x,int y) const {
    return tool==UNITS || tool==STARTS?(y-kPaletteTop+paletteScroll)/26:
        ((y-kPaletteTop+paletteScroll)/kCellH)*cols+(x-4)/(kThumb+4);
}

std::string cart::Editor::paletteHint(int y) const {
    if(y<kMenuH+30)return "Search this browser by name or identifier (Ctrl+F). Escape returns keyboard focus to the map.";
    if(y<kMenuH+53)return "Click to cycle browser categories or unit roles. Search and filters work together.";
    if(tool==UNITS)return "Click to cycle faction filters. Internal unit identifiers remain available in the browser details.";
    return {};
}

// Search focus, Ctrl+F and the filter buttons above the list.
bool cart::Editor::handlePaletteFocus(SDL_Event& e) {
    if(e.type==SDL_KEYDOWN && (e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI)) && e.key.keysym.sym==SDLK_f) {
        paletteFocus=true;paletteEditor.focus(paletteSearch);SDL_StartTextInput();return true;
    }
    if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
        if(e.button.x<kPaletteW && e.button.y>=kMenuH && e.button.y<kPaletteTop) {
            if(e.button.y<kMenuH+30) {paletteFocus=true;paletteEditor.focus(paletteSearch);SDL_StartTextInput();}
            else if(e.button.y<kMenuH+53) {categoryIndex=(categoryIndex+1)%int(paletteCategories.size());refreshPalette();}
            else if(tool==UNITS) {factionIndex=(factionIndex+1)%6;refreshPalette();}
            return true;
        }
        if(paletteFocus) {paletteFocus=false;SDL_StopTextInput();}
    }
    if(paletteFocus && (e.type==SDL_TEXTINPUT || e.type==SDL_KEYDOWN)) {
        if(e.type==SDL_KEYDOWN && (e.key.keysym.sym==SDLK_ESCAPE || e.key.keysym.sym==SDLK_RETURN)) {paletteFocus=false;SDL_StopTextInput();}
        else {paletteEditor.input(e,paletteSearch);refreshPalette();}
        return true;
    }
    return false;
}

bool cart::Editor::handlePaletteClick(SDL_Event& e) {
    if(!(e.type == SDL_MOUSEBUTTONDOWN &&
         e.button.button == SDL_BUTTON_LEFT && e.button.x < kPaletteW &&
         e.button.y >= kPaletteTop && e.button.y < h - kStatusH))return false;
    const int row=paletteRowAt(e.button.x,e.button.y);
    if(row>=0 && row<int(paletteItems.size())) {
        regionCanvas=false;
        revealTool();const int index=paletteItems[size_t(row)];
        if(tool==UNITS)selectedType=index;
        else if(tool==FEATURES)selectedFeat=index;
        else if(tool==STARTS)mapView.setOffset(scenario.starts[size_t(index)].xpos*16-canvasW/(2*mapView.zoom()),scenario.starts[size_t(index)].zpos*16-canvasH/(2*mapView.zoom()));
        else selected=index;
    }
    return true;
}

bool cart::Editor::handlePaletteWheel(SDL_Event& e) {
    if(e.type != SDL_MOUSEWHEEL)return false;
    int mxp, myp; SDL_GetMouseState(&mxp, &myp); mxp /= kUIScale; myp /= kUIScale;
    if (mxp >= kPaletteW)return false;   // the canvas zooms instead
    // Scroll the palette (tool-dependent count).
    const int contentH=h-kStatusH-kPaletteTop;
    const int total=tool==UNITS || tool==STARTS?int(paletteItems.size())*26:((int(paletteItems.size())+cols-1)/cols)*kCellH;
    paletteScroll=std::clamp(paletteScroll-e.wheel.y*40,0,std::max(0,total-contentH));
    return true;
}

void cart::Editor::drawPalette() {
    fillRect(ren, 0, kMenuH, kPaletteW, canvasH, 30, 32, 40);
    SDL_Rect palClip{0,kPaletteTop,kPaletteW,h-kStatusH-kPaletteTop};
    SDL_RenderSetClipRect(ren,&palClip);
    if(tool==UNITS || tool==STARTS) {
        for(int row=0;row<int(paletteItems.size());++row) {
            const int i=paletteItems[size_t(row)],y=kPaletteTop+row*26-paletteScroll;
            if(y+24<kPaletteTop || y>h-kStatusH)continue;
            if(tool==UNITS && i==selectedType)fillRect(ren,0,y,kPaletteW,25,70,66,40);
            if(tool==UNITS) {
                const auto* info=unitInfo(i);
                if(auto* portrait=unitPortraits.get("anims/buildpic/"+lowerText(unitTypes[size_t(i)])+".jpg")) {
                    int pw=0,ph=0;SDL_QueryTexture(portrait,nullptr,nullptr,&pw,&ph);
                    if(pw>0 && ph>0) {
                        const float scale=std::min(32.f/pw,24.f/ph);
                        SDL_Rect dst{4+(32-int(pw*scale))/2,y+1+(24-int(ph*scale))/2,int(pw*scale),int(ph*scale)};
                        SDL_RenderCopy(ren,portrait,nullptr,&dst);
                    }
                }
                cart::drawText(ren,info?info->name:unitTypes[size_t(i)],42,y+3,1,235,230,200);
                cart::drawText(ren,unitTypes[size_t(i)]+" - "+unitCategory(i),42,y+14,1,160,175,195);
            } else cart::drawText(ren,paletteLabel(i),6,y+7,1,235,210,100);
        }
    } else {
        const int selection=tool==FEATURES?selectedFeat:selected;
        for(int row=0;row<int(paletteItems.size());++row) {
            const int i=paletteItems[size_t(row)],x=4+(row%cols)*(kThumb+4),y=kPaletteTop+4+(row/cols)*kCellH-paletteScroll;
            if(y+kCellH<kPaletteTop || y>h-kStatusH)continue;
            SDL_Rect cell{x,y,kThumb,kThumb};fillRect(ren,x,y,kThumb,kThumb,44,46,54);
            auto* texture=tool==FEATURES?featTextureFor(features.list()[size_t(i)]):thumbFor(sections.list()[size_t(i)].path);
            if(texture) {
                if(tool==FEATURES) {
                    const auto* sprite=featureSpriteFor(features.list()[size_t(i)]);
                    const float scale=std::min(float(kThumb)/std::max(sprite->w,1),float(kThumb)/std::max(sprite->h,1));
                    SDL_Rect fit{x+(kThumb-int(sprite->w*scale))/2,y+(kThumb-int(sprite->h*scale))/2,int(sprite->w*scale),int(sprite->h*scale)};
                    SDL_RenderCopy(ren,texture,nullptr,&fit);
                } else SDL_RenderCopy(ren,texture,nullptr,&cell);
            }
            if(i==selection) {SDL_SetRenderDrawColor(ren,255,210,90,255);SDL_RenderDrawRect(ren,&cell);}
            cart::drawText(ren,paletteLabel(i).substr(0,14),x,y+kThumb+4,1,210,210,215);
        }
    }
    SDL_RenderSetClipRect(ren,nullptr);
    cart::drawField(ren,4,kMenuH+3,kPaletteW-8,"SEARCH (CTRL+F)",paletteSearch,paletteFocus,paletteFocus?&paletteEditor:nullptr);
    cart::drawButton(ren,4,kMenuH+33,kPaletteW-8,18,paletteCategories[size_t(categoryIndex)].substr(0,28),false);
    if(tool==UNITS) {
        static const char* factionNames[]={"ALL FACTIONS","ARAMON","TAROS","VERUNA","ZHON","CREON"};
        cart::drawButton(ren,4,kMenuH+54,kPaletteW-8,18,factionNames[factionIndex],false);
    } else cart::drawText(ren,std::to_string(paletteItems.size())+" MATCHES",6,kMenuH+59,1,180,190,200);
}
