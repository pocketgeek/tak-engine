// The frame around the canvas: menu bar and menu commands, toolbar, global
// keyboard shortcuts, status bar, minimap, and delayed hover help.

#include "cartographer/editorstate.h"
#include "cartographer/dialog.h"

#include <cstdio>

namespace {
const float kZoomLevels[5] = {1.0f, 0.75f, 0.5f, 0.25f, 0.125f};
}

void cart::Editor::refreshMinimap() {
    const auto& map=mapView.map();
    if(map.minimap.empty()) {minimapTexture.reset();minimapSource=nullptr;return;}
    if(!minimapRefreshNeeded && map.minimap.data()==minimapSource)return;
    auto palette=cart::loadWorldPalette(vfs,world);
    std::vector<uint8_t> rgba(map.minimap.size()*4);
    for(size_t i=0;i<map.minimap.size();++i) {const auto& color=palette.rgba[map.minimap[i]];std::copy(color,color+4,rgba.begin()+i*4);}
    minimapTexture.reset(SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,map.minimapW,map.minimapH));
    if(minimapTexture) {SDL_UpdateTexture(minimapTexture.get(),nullptr,rgba.data(),map.minimapW*4);SDL_SetTextureScaleMode(minimapTexture.get(),SDL_ScaleModeLinear);}
    minimapSource=map.minimap.data();minimapRefreshNeeded=false;
}

void cart::Editor::updateLiveMinimap() {
    if(minimapJob.valid() && minimapJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {
            auto preview=minimapJob.get();
            if(minimapJobRevision==minimapRevision && !preview.rgba.empty()) {
                minimapTexture.reset(SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,preview.width,preview.height));
                if(minimapTexture) {SDL_UpdateTexture(minimapTexture.get(),nullptr,preview.rgba.data(),preview.width*4);SDL_SetTextureScaleMode(minimapTexture.get(),SDL_ScaleModeLinear);}
                minimapError.clear();if(hooks.minimapUpdated)hooks.minimapUpdated(preview.rgba);
            }
        } catch(const std::exception& error) {minimapError=std::string("Minimap: ")+error.what();}
    }
    if(minimapPreviewPending && !minimapJob.valid() && SDL_GetTicks64()>=minimapDue) {
        // Only immutable tile data is needed; avoid copying millions of
        // feature/height cells. The worker owns its decode cache; opening
        // a document joins it before replacing the borrowed asset index.
        const auto& source=mapView.map();tak::tnt::Map snapshot;
        snapshot.blocksX=source.blocksX;snapshot.blocksY=source.blocksY;snapshot.stockTerrain=source.stockTerrain;
        snapshot.tileKeys=source.tileKeys;snapshot.tileCols=source.tileCols;snapshot.tileRows=source.tileRows;
        minimapJobRevision=minimapRevision;minimapPreviewPending=false;minimapCancel=std::make_shared<std::atomic_bool>(false);
        minimapJob=std::async(std::launch::async,[map=std::move(snapshot),&assets=vfs,kingdom=world,stop=minimapCancel]() {
            tak::terrain::Compositor compositor(assets);
            return cart::minimapPreview(map,compositor,cart::loadWorldPalette(assets,kingdom),stop);
        });
    }
}

bool cart::Editor::handleMinimapInput(SDL_Event& e) {
    auto navigateMinimap=[&](int x,int y) {
        const float wx=std::clamp(float(x-miniRect.x)/miniRect.w,0.0f,1.0f)*mapView.map().width*16;
        const float wz=std::clamp(float(y-miniRect.y)/miniRect.h,0.0f,1.0f)*mapView.map().height*16;
        mapView.setOffset(wx-canvasW/(2*mapView.zoom()),wz-canvasH/(2*mapView.zoom()));
    };
    if(e.type==SDL_MOUSEBUTTONDOWN && cart::pointIn(e.button.x,e.button.y,miniRect)) {minimapDrag=true;navigateMinimap(e.button.x,e.button.y);return true;}
    if(e.type==SDL_MOUSEMOTION && minimapDrag) {navigateMinimap(e.motion.x,e.motion.y);return true;}
    if(e.type==SDL_MOUSEBUTTONUP && minimapDrag) {minimapDrag=false;return true;}
    return false;
}

void cart::Editor::drawMinimap() {
    if(minimapTexture) {
        SDL_RenderCopy(ren,minimapTexture.get(),nullptr,&miniRect);
        SDL_SetRenderDrawColor(ren,235,225,195,255);SDL_RenderDrawRect(ren,&miniRect);
        SDL_RenderSetClipRect(ren,&miniRect);
        const float sx=float(miniRect.w)/(mapView.map().width*16),sz=float(miniRect.h)/(mapView.map().height*16);
        SDL_FRect view{miniRect.x+mapView.offX()*sx,miniRect.y+mapView.offY()*sz,canvasW/mapView.zoom()*sx,canvasH/mapView.zoom()*sz};
        SDL_RenderDrawRectF(ren,&view);
        for(const auto& start:scenario.starts) {SDL_Rect marker{int(miniRect.x+start.xpos*16*sx)-2,int(miniRect.y+start.zpos*16*sz)-2,4,4};SDL_RenderFillRect(ren,&marker);}
        SDL_RenderSetClipRect(ren,nullptr);
    }
}

void cart::Editor::menuAction(int menu,int row) {
    if(menu==0) {
        if(row==5)testMap();
        if(row==0) {if(dirty)openConfirm("NEW MAP","Discard unsaved changes?",[&]{wantNew=true;});else openModal(M_NEW);}
        if(row==1)openModal(M_OPENPATH);
        if(row==2) {if(saveBundle(outDir+"/"+mapName+".kmp"))openMessage("SAVED",outDir+"/"+mapName+".kmp");else openMessage("SAVE FAILED",saveError);}
        if(row==3)openModal(M_SAVEAS);
        if(row==4) {if(saveMap(outDir+"/"+mapName+".tnt"))openMessage("EXPORTED",outDir+"/"+mapName+".tnt");else openMessage("EXPORT FAILED",saveError);}
        if(row==6) {SDL_Event quit{};quit.type=SDL_QUIT;SDL_PushEvent(&quit);}
    } else if(menu==1) {
        if(row<2) {SDL_Event key{};key.type=SDL_KEYDOWN;key.key.keysym.sym=row?SDLK_y:SDLK_z;key.key.keysym.mod=KMOD_CTRL;SDL_PushEvent(&key);}
        else if(row==2) {clearArm=true;editMode=MODE_PLACE;}
        else if(row==3) {stampLayers.objects=!stampLayers.objects;lastStampX=lastStampY=-999999;}
        else if(row==4) {tool=TERRAIN;regionCanvas=false;copyTerrainSelection();}
        else if(row==5)useTerrainClipboard();
    } else if(menu==2) {
        int w,h;SDL_GetRendererOutputSize(ren,&w,&h);w=int(w/kUIScale)-kPaletteW;h=int(h/kUIScale)-kMenuH-kStatusH;
        if(row==17)previewModel();
        if(row==0) {mapView.setZoom(std::min(float(w)/(mapView.map().width*16),float(h)/(mapView.map().height*16)));mapView.setOffset(0,0);}
        if(row==1)mapView.setZoom(1);
        if(row==2) {showGrid=!showGrid;persistPreferences();}
        if(row==3) {showRegions=!showRegions;regionCanvas=false;persistPreferences();}
        if(row>=14 && row<=16) {
            bool& visible=row==14?showFeatures:row==15?showUnits:showStarts;visible=!visible;
            selectionBox=false;selectedFeatures.dragging=false;selectedUnits.dragOrigins.clear();
            draggingUnit=draggingStart=-1;persistPreferences();
        }
        if(row==12 || row==13) {preferences.scalePercent=std::clamp(preferences.scalePercent+(row==12?-25:25),50,200);persistPreferences();}
        if(row>=7 && row<=10)buildOverlay(static_cast<cart::OverlayKind>(row-7));
        if(row==11) {overlayTexture.reset();overlayLegend.clear();}
        if(row==4) {
            bool any=false;float x0=0,z0=0,x1=0,z1=0;
            auto include=[&](float x,float z) {
                if(!any) {x0=x1=x;z0=z1=z;any=true;}
                else {x0=std::min(x0,x);x1=std::max(x1,x);z0=std::min(z0,z);z1=std::max(z1,z);}
            };
            if(tool==FEATURES) {
                for(int i:selectedFeatures.cells)if(cart::FeatureSelection::selectable(mapView.map(),i))include(float(i%mapView.map().width*16),float(i/mapView.map().width*16));
            } else for(int i:selectedUnits.indices)if(i>=0 && i<int(units.size()))include(units[i].x,units[i].z);
            if(any) {
                const float zoom=std::min({1.f,float(w)/std::max(128.f,x1-x0+128),float(h)/std::max(128.f,z1-z0+128)});
                mapView.setZoom(zoom);mapView.setOffset((x0+x1)/2-w/(2*zoom),(z0+z1)/2-h/(2*zoom));
            } else openMessage("FRAME SELECTION","Select one or more units or features first.");
        }
        if(row==5)viewBookmark=ViewBookmark{mapView.offX(),mapView.offY(),mapView.zoom()};
        if(row==6) {
            if(viewBookmark) {mapView.setZoom(viewBookmark->zoom);mapView.setOffset(viewBookmark->x,viewBookmark->z);}
            else openMessage("VIEW BOOKMARK","Store a view bookmark first.");
        }
    } else if(menu==3) {
        if(row==0)openModal(M_SCENARIO);
        if(row==1)openModal(M_RESIZE);
        if(row==2)useOnlyOpen=true;
        if(row==3)checkMap();
        if(row==4)openScripting();
        if(row==6) {regionsOpen=true;regionSelected=scen.regions.empty()?-1:0;}
        if(row==7) {modal=M_ISSUES;issueScroll=0;}
        if(row==8)openGeneratorRecipe();
        if(row==9) {preferences.tracePlaytest=!preferences.tracePlaytest;persistPreferences();}
        if(row==10)openObjects();
        if(row==5) {if(!mapIssues.empty())issueIndex=(issueIndex+1)%mapIssues.size();showMapIssue();}
    } else if(menu==4)symmetryMenu(row);
    else openMessage(row?"ABOUT CARTOGRAPHER":"EDITOR CONTROLS",row?"TAK Engine map and scenario editor. Uses your original game assets.":
        "Choose terrain or objects in the left browser. Left-click uses the chosen Place, Select, Erase or Pan mode. Right-drag always pans. In Select mode, drag a box or move selected units or features. Ctrl+C/X/V copies/cuts/pastes, Ctrl+D duplicates, Delete removes, and Enter opens properties. Tab changes tools. Ctrl+Z undoes; Ctrl+Y redoes. Ctrl+S saves a playable KMP. File offers Open, Save As and loose export. Double-click a unit or rule to edit it. Unsaved maps get recovery copies every minute. "
        "Symmetry: M mirrors placements, moves and erasures live across the chosen axis; Shift+M (Symmetrize) copies one half or quadrant onto the rest, using mirror-twin terrain sections. "
        "Terrain tool + Select: drag a block rectangle, Ctrl+C copies it, Ctrl+V makes it a click-to-paste brush (Esc returns to sections).");
}

bool cart::Editor::handleMenuInput(SDL_Event& e) {
    if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE && menuOpen>=0) {menuOpen=-1;return true;}
    if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
        const int mx=e.button.x,my=e.button.y;
        if(my>=25 && my<41 && mx>=756 && mx<872) {testMap();return true;}
        if(my<22 && mx>=96 && mx<96+int(menuNames.size())*72) {const int item=(mx-96)/72;menuOpen=menuOpen==item?-1:item;return true;}
        if(menuOpen>=0) {
            const int column=menuOpen;menuOpen=-1;
            const int x=96+column*72,row=(my-22)/20;
            if(mx>=x && mx<x+240 && my>=22 && row<int(menuRows[size_t(column)].size()))menuAction(column,row);
            return true;
        }
    }
    if(menuOpen>=0)return true;
    return false;
}

// Place / Select / Erase / Pan mode buttons and the REGIONS toggle.
bool cart::Editor::handleToolbarModes(SDL_Event& e) {
    if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && e.button.y>=22 && e.button.y<kMenuH && e.button.x>=400 && e.button.x<656) {
        editMode=EditMode((e.button.x-400)/64);return true;
    }
    if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT && e.button.y>=25 && e.button.y<41 && e.button.x>=668 && e.button.x<748) {
        regionCanvas=!regionCanvas;if(regionCanvas) {showRegions=true;editMode=MODE_PLACE;clearArm=false;}return true;
    }
    return false;
}

bool cart::Editor::handleShortcuts(SDL_Event& e) {
    if(e.type==SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
       (e.key.keysym.sym==SDLK_z || e.key.keysym.sym==SDLK_y)) {
        commitHistory();
        const bool redo=e.key.keysym.sym==SDLK_y || (e.key.keysym.mod & KMOD_SHIFT);
        restoreHistory(redo?history.redo():history.undo());return true;
    }
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (dirty) {
            // Close any transient overlay so the quit prompt gets input.
            useOnlyOpen = false; scriptOpen = false; pickOpen = false;regionsOpen=false;
            modal = M_QUITSAVE; SDL_StopTextInput();
        } else running = false;
    } else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
             e.key.keysym.sym == SDLK_s) {
        if(e.key.keysym.mod & KMOD_SHIFT) {openModal(M_SAVEAS);return true;}
        const bool loose = false;
        const bool ok=loose ? saveMap(outDir+"/"+mapName+".tnt") : saveBundle(outDir+"/"+mapName+".kmp");
        if(ok) {dirty=false;openMessage("SAVED",outDir+"/"+mapName+(loose?".tnt (loose export)":".kmp"));}
        else openMessage("SAVE FAILED",saveError);
    } else if(e.type==SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) && e.key.keysym.sym==SDLK_o) {
        openModal(M_OPENPATH);
    } else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
               e.key.keysym.sym == SDLK_b) {
        // Ctrl+B: save the finished map as a single .kmp bundle.
        bool ok = saveBundle(outDir + "/" + mapName + ".kmp");
        if (ok) dirty = false;
        openMessage("SAVE BUNDLE", ok ? ("Saved " + mapName + ".kmp")
                                      : saveError);
    } else if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL|KMOD_GUI)) &&
               e.key.keysym.sym == SDLK_l) {
        // Land Lasso: toggle between land (terrain-stamp) and object mode.
        tool = tool == TERRAIN ? FEATURES : TERRAIN;regionCanvas=false;revealTool();
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_k) {
        clearArm = !clearArm;editMode=MODE_PLACE;regionCanvas=false;    // Edit -> Clear Area (drag a box)
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_n) {
        // File -> New Map. Guard unsaved edits (New replaces the whole map).
        if (dirty) openConfirm("NEW MAP", "Discard unsaved changes and start "
                               "a new map?", [&]() { wantNew = true; });
        else openModal(M_NEW);
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_p) {
        openModal(M_SCENARIO);   // Scenario -> Properties
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_r) {
        openModal(M_RESIZE);     // Scenario -> Resize
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_u) {
        useOnlyOpen = true;      // Scenario -> Use Only (unit restriction)
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F6) {
        previewModel();
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F5) {
        testMap();
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_c) {
        checkMap();              // Scenario -> Check Map
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_t) {
        openScripting();         // Scenario -> Scripting (triggers)
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_LEFTBRACKET) {
        currentPlayer = (currentPlayer + 7) % 8;   // UNITS: pick placement player
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RIGHTBRACKET) {
        currentPlayer = (currentPlayer + 1) % 8;
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_m) {
        if(e.key.keysym.mod & KMOD_SHIFT) openModal(M_SYMMETRIZE);   // Symmetry -> Symmetrize
        else toggleLiveSymmetry();                                    // Symmetry -> Live symmetry
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_g) {
        showGrid = !showGrid;persistPreferences();   // View -> Grid
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB) {
        regionCanvas=false;
        tool = Tool((int(tool) + 1) % 4);revealTool();   // cycle TERRAIN->FEATURES->UNITS->STARTS
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 &&
               e.key.keysym.sym <= SDLK_5) {
        // Zoom levels 1..5 = 100/75/50/25/12.5% (retail's five steps).
        mapView.setZoom(kZoomLevels[e.key.keysym.sym - SDLK_1]);
    } else return false;
    return true;
}

bool cart::Editor::handleToolbarTools(SDL_Event& e) {
    if(!(e.type == SDL_MOUSEBUTTONDOWN &&
         e.button.button == SDL_BUTTON_LEFT && e.button.y >=22 && e.button.y < kMenuH))return false;
    // Toolbar buttons: TERRAIN | FEATURES | STARTS (each 72px).
    int bi = (e.button.x - 96) / 72;
    if (bi >= 0 && bi < 4) {tool=Tool(bi);regionCanvas=false;revealTool();}
    return true;
}

void cart::Editor::drawChrome() {
    // Chrome strips.
    fillRect(ren, 0, 0, w, kMenuH, 46, 48, 58);
    fillRect(ren, 0, kMenuH - 1, w, 1, 12, 12, 16);
    fillRect(ren, kPaletteW - 1, kMenuH, 1, canvasH, 12, 12, 16);
    fillRect(ren, 0, h - kStatusH, w, kStatusH, 38, 40, 50);
    fillRect(ren, 0, h - kStatusH, w, 1, 12, 12, 16);

    // Menu strip: title + tool buttons (active one highlighted).
    cart::drawText(ren, "CARTOGRAPHER", 6, 7, 1, 200, 200, 210);
    const char* names[4] = {"TERRAIN", "FEATURES", "UNITS", "STARTS"};
    for (int t = 0; t < 4; ++t) {
        int bx = 96 + t * 72;
        bool active = !regionCanvas && int(tool) == t;
        fillRect(ren, bx, 25, 68, 16, active ? 90 : 60, active ? 80 : 62,
                 active ? 40 : 74);
        cart::drawText(ren, names[t], bx + 8, 29, 1, active ? 255 : 190,
                       active ? 220 : 190, active ? 120 : 200);
    }

    static const char* modes[]={"PLACE","SELECT","ERASE","PAN"};
    for(int i=0;i<4;++i)cart::drawButton(ren,400+i*64,25,60,16,modes[i],int(editMode)==i);
    cart::drawButton(ren,668,25,80,16,"REGIONS",regionCanvas);
    cart::drawButton(ren,756,25,116,16,playtest.running()?"TEST RUNNING":"TEST MAP (F5)",false);

    for(size_t i=0;i<menuNames.size();++i) {
        const int x=96+int(i)*72;
        fillRect(ren,x,2,68,18,menuOpen==int(i)?90:46,menuOpen==int(i)?80:48,menuOpen==int(i)?40:58);
        cart::drawText(ren,menuNames[i],x+6,7,1,230,230,240);
    }
    if(menuOpen>=0) {
        const int x=96+menuOpen*72;const auto& rows=menuRows[size_t(menuOpen)];
        fillRect(ren,x,22,240,int(rows.size())*20,46,48,58);
        for(size_t i=0;i<rows.size();++i) {
            std::string label=rows[i];
            if(menuOpen==1 && i==3)label=std::string(stampLayers.objects?"[ ] ":"[X] ")+"Brush: protect objects";
            if(menuOpen==3 && i==9)label=std::string(preferences.tracePlaytest?"[X] ":"[ ] ")+"Log Test Map triggers";
            if(menuOpen==4)label=symmetryMenuLabel(i);
            if(menuOpen==2 && (i==2 || i==3 || (i>=14 && i<=16))) {
                const bool shown=i==2?showGrid:i==3?showRegions:i==14?showFeatures:i==15?showUnits:showStarts;
                label=std::string(shown?"[X] ":"[ ] ")+label;
            }
            cart::drawText(ren,label,x+8,28+int(i)*20,1,235,235,240);
        }
    }

    // Status bar: cursor cell, tool, zoom, start count.
    int mxg, myg; SDL_GetMouseState(&mxg, &myg); mxg /= kUIScale; myg /= kUIScale;
    int ccx, ccz;
    std::string coord = mouseCell(mxg, myg, ccx, ccz)
        ? "( " + std::to_string(ccx) + ", " + std::to_string(ccz) + " )" : "";
    char zbuf[16];
    std::snprintf(zbuf, sizeof zbuf, "%d%%", int(mapView.zoom() * 100 + 0.5f));
    std::string status = coord + "   TOOL: " + names[int(tool)] + "   ZOOM: " + zbuf;
    if(tool==TERRAIN)status+=stampLayers.objects?"   BRUSH: TERRAIN + OBJECTS":"   BRUSH: OBJECTS PROTECTED";
    if (tool == UNITS)
        status += "   PLAYER: " + std::to_string(currentPlayer) +
                  "   UNITS: " + std::to_string(units.size())+"   SELECTED: "+std::to_string(tool==FEATURES?selectedFeatures.cells.size():selectedUnits.indices.size());
    else
        status += "   STARTS: " + std::to_string(scenario.starts.size());
    status += dirty?"   UNSAVED":"   SAVED";
    if (!useOnly.empty()) status += "   USEONLY: " + std::to_string(useOnly.size());
    if(!minimapError.empty())status+="   "+minimapError;
    else if(minimapJob.valid() || minimapPreviewPending)status+="   Updating minimap...";
    if(!recoveryStatus.empty()) status+="   "+recoveryStatus;
    if (clearArm) status += "   CLEAR AREA: drag a box (K cancels)";
    status += symmetryStatusText();
    // A recent symmetry / clipboard result leads so it is not cut off.
    if(!symmetryStatus.empty() && SDL_GetTicks64()-symmetryStatusAt<8000)status=coord+"   "+symmetryStatus+"   "+status.substr(coord.size());
    if(mxg<kPaletteW && myg>=kPaletteTop && myg<h-kStatusH) {
        const int row=paletteRowAt(mxg,myg);
        if(row>=0 && row<int(paletteItems.size()))status=paletteLabel(paletteItems[size_t(row)]);
    }
    if(!regionCanvas && !toolVisible())status="LAYER HIDDEN: choose its toolbar or palette to show it before editing.";
    if(regionCanvas)status=coord+"   REGIONS: PLACE DRAWS; SELECT MOVES/RESIZES; ENTER NAMES; ESC CANCELS"+(dirty?std::string("   UNSAVED"):std::string("   SAVED"));
    cart::drawText(ren, status, 6, h - kStatusH + 7, 1, 200, 205, 215);
}

void cart::Editor::drawHints() {
    // Delayed hover help stays out of the way during gestures and dialogs.
    std::string hint;
    int hx,hy;const auto mouseButtons=SDL_GetMouseState(&hx,&hy);hx=int(hx/kUIScale);hy=int(hy/kUIScale);
    if(modal==M_NONE && menuOpen<0 && !scriptOpen && !regionsOpen && !useOnlyOpen && !mouseButtons) {
        if(hy>=25 && hy<41) {
            if(hx>=96 && hx<384) {
                const char* hints[]={
                    "Terrain: place authored sections. Tab cycles tools. Right-drag pans; mouse wheel zooms around the pointer.",
                    "Features: place trees, rocks and other map objects. Tab cycles tools. Select mode supports box selection and group editing.",
                    "Units: place units for the current owner. Enter edits a selected unit. Ctrl+C/V copies/pastes; Delete removes selected units.",
                    "Starts: place or move player start positions. Check Map (C) reports numbering, overlap and placement issues."};
                hint=hints[(hx-96)/72];
            } else if(hx>=400 && hx<656) {
                const char* hints[]={
                    "Place: click the map to place the selected brush or object. Terrain brushes can protect objects through the Edit menu.",
                    "Select: click or drag a box to select units/features; drag selected objects to move them. Shift extends selection. Enter opens properties. With Terrain, drag a block rectangle; Ctrl+C copies it and Ctrl+V pastes it as a brush.",
                    "Erase: remove objects under the pointer. Terrain removal uses Clear Area (K). Changes can be undone with Ctrl+Z.",
                    "Pan: drag to move the view. Right-drag pans in any mode. Mouse wheel zooms around the pointer."};
                hint=hints[(hx-400)/64];
            } else if(hx>=756 && hx<872)hint="Test Map (F5): open a temporary snapshot in the game's private lobby without saving or closing this document.";
            else if(hx>=668 && hx<748)hint="Regions: draw trigger areas in Place mode; select, move or resize them in Select mode. Enter edits the selected region.";
        } else if(hy<22 && hx>=96+4*72 && hx<96+5*72)
            hint="Symmetry: M mirrors placements, moves and erasures of units, features and starts as you edit, and stamps mirror-twin terrain sections. "
                 "Shift+M copies a chosen half or quadrant onto the rest in one undo step. Terrain is never flipped: blocks without a twin section are reported.";
        else if(hx<kPaletteW && hy>=kMenuH && hy<kPaletteTop)hint=paletteHint(hy);
        else if(cart::pointIn(hx,hy,miniRect))hint="Minimap: click or drag to navigate. View offers Fit map, Frame selection and a stored view bookmark.";
    }
    if(hint!=hoveredHint) {hoveredHint=hint;hintSince=SDL_GetTicks64();}
    if(!hint.empty() && SDL_GetTicks64()-hintSince>=650)cart::drawTooltip(ren,w,h,hx,hy,hint);
}
