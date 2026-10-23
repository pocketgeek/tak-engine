#include <ctime>
#include <type_traits>
#include "client/guiart.h"
#include "client/gameview.h"
#include "client/gamespeed.h"
#include "client/artscale.h"
#include "client/cursorrange.h"
#include "client/reclaimpick.h"
#include "client/gpuvram.h"
#include "client/renderdriver.h"
#include "client/uitext.h"
#include "client/chatbox.h"
#include "client/buildrow.h"
#include "client/statsfit.h"
#include "net/protocol.h"
#include "sim/retailtransport.h"
#include "sim/retailheight.h"
#include "util/procmetrics.h"

// Out-of-line GameView method definitions (hud concern), split from the
// class body in gameview.h so editing a body recompiles only this translation
// unit. Trivial getters, ctors, static, template, constexpr and default-arg
// methods stay inline in the header. Grouping is by name heuristic.

namespace {
    int32_t unloadDestinationHeight(const tak::tnt::Map& map,float x,float z) {
        const int height=map.heights.empty() ? 0 : tak::sim::retailTerrainHeight(
            tak::sim::Fixed::fromFloat(x).v,tak::sim::Fixed::fromFloat(z).v,
            map.width,map.height,[&](int cx,int cz) {
                return map.heights[size_t(cz)*size_t(map.width)+size_t(cx)];
            });
        return std::max(height,int(uint8_t(map.seaLevel)))*65536;
    }
}

    // The reclaim target under a world point, read from the render snapshot: the cursor
    // asks every frame, and the two click sites (right-click, armed CLEAR) issue what it
    // returns. No simMutex_ anywhere -- the snapshot can be a frame stale, and the sim
    // validates the target when it applies the Reclaim (a gone feature is dropped).
    std::optional<int> GameView::reclaimTargetAt(float wx,float wz) const {
        tak::client::ReclaimPick pick{wx,wz};
        for (const auto& f:features_) {
            if (!f.hasSim || !f.aliveVis || !f.reclaimable || !canPickPoint(f.x,f.z)) continue;
            pick.offer(f.simId,f.x,f.z,f.fx,f.fz);
        }
        // Corpses / statues / rubble (negative id), sized by their FEATURE footprint.
        for (const auto* u:front().live) {
            if (u->alive() || !u->corpsePhase || !u->corpseReclaimable || !canPickPoint(u->x,u->z)) continue;
            pick.offer(-u->id,u->x,u->z,u->corpseFootX,u->corpseFootZ);
        }
        return pick.id;
    }

    bool GameView::canAssistSite(const UnitR& b,const UnitR& site) const {
        if (b.id==site.id || !b.alive() || b.embarked() || b.underConstruction ||
            !b.type || !b.type->isBuilder || b.type->isStructure() || b.repeatType ||
            !site.alive() || !site.type || !site.underConstruction ||
            b.player!=site.player) return false;
        const auto& menu=registry_.buildable(b.type->id);
        return !b.type->builderLimited || std::find(menu.begin(),menu.end(),site.type->id)!=menu.end();
    }

    bool GameView::canLoadPassenger(const UnitR& u,const UnitR& t) const {
        if(u.id==t.id || !u.alive() || !t.alive() || u.embarked() || t.embarked() ||
           !u.type || !t.type || u.underConstruction || t.underConstruction ||
           u.paralyzedFor>0 || t.paralyzedFor>0) return false;
        if(u.type->isStructure() || u.type->canFly || u.type->cantBeTransported ||
           !t.type->canTransport || u.player!=t.player ||
           (!t.type->canFly && !u.type->transportLandEligible)) return false;
        if(int64_t(u.worldPosition[1])+u.type->modelTop<=
           int64_t(uint8_t(mapView_.map().seaLevel))*65536) return false;
        uint32_t count=0,used=0;
        for(int id:t.cargo) {
            const auto* c=frameUnitP(id);
            if(c && c->alive() && c->inTransport==t.id && c->type) {
                ++count;used+=uint16_t(c->type->transportSize);
            }
        }
        return tak::sim::retailTransportCapacity(uint16_t(u.type->transportSize),
            uint16_t(t.type->maxTransportSize),count,uint16_t(t.type->transportCap),
            used,uint16_t(t.type->transportSizeCap));
    }

    void GameView::rightClickOrder(float wx, float wz, bool queue) {
        ClickScope click(*this);   // one click: a Legion net game sends it as explicit orders
        if (selection_.empty()) return;
        const auto* first = frameUnitP(selection_.front());
        // Selected transport with cargo: right-click = sail + disembark.
            if (first && first->type && first->type->canTransport &&
                !first->cargo.empty()) {
                tak::net::Command c;
                c.kind = tak::net::Cmd::Unload;
                c.targetId = unloadDestinationHeight(mapView_.map(),wx,wz);
                c.unitId = first->id;
                c.x = wx;
                c.z = wz;
                c.queue = queue ? 1 : 0;
                issue(c);
                return;
            }
            // Clicking a friendly transport = board it.
            int friendlyTransport = -1;
            float bestT = 1e30f;
            for (const UnitR* _up : front().live) {
                const UnitR& u = *_up;
                if (!canPickUnit(u)) continue;
                if (!u.alive() || !first || u.player != first->player || !u.type ||
                    !u.type->canTransport)
                    continue;
                float d=0;
                if (unitUnderCursor(u,mouseX_,mouseY_,&d) && d<bestT &&
                    std::any_of(selection_.begin(),selection_.end(),[&](int id) {
                        const auto* passenger=frameUnitP(id);
                        return passenger && canLoadPassenger(*passenger,u);
                    })) {bestT=d;friendlyTransport=u.id;}
            }
            if (friendlyTransport >= 0) {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Load;
                    c.unitId = id;
                    c.targetId = friendlyTransport;
                    c.queue = queue ? 1 : 0;
                    issue(c);
                }
                return;
            }
            // Right-clicking a friendly unit is contextual, never a move:
            //  - a conjure-in-progress: selected builders that can build that type
            //    resume/assist it (revives a decaying site); other units do nothing.
            //  - any other friendly unit: selected units that can attack guard it;
            //    the rest do nothing.
            // Either case consumes the click (no move fallthrough).
            {
                int siteId = -1;   float bestSite = 1e18f;
                int allyId = -1;   float bestAlly = 1e30f;
                for (const UnitR* _up : front().live) {
                    const UnitR& u = *_up;
                    if (!canPickUnit(u)) continue;
                    if (!u.alive() || u.embarked() || !u.type || !first ||
                        !worldTables().allied(u.player, first->player)) continue;
                    float d=0;
                    if (!unitUnderCursor(u,mouseX_,mouseY_,&d)) continue;
                    if (u.underConstruction) {
                        if (d < bestSite) { bestSite = d; siteId = u.id; }
                    } else if (d < bestAlly) { bestAlly = d; allyId = u.id; }
                }
                if (siteId >= 0) {
                    const auto* st = frameUnitP(siteId);
                    bool any = false;
                    for (int id : selection_) {
                        const auto* bu = frameUnitP(id);
                        if (!bu || !canAssistSite(*bu,*st)) continue;
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Assist;
                        c.unitId = id; c.targetId = siteId; c.queue = queue;
                        issue(c);
                        any = true;
                    }
                    if (any) voice(selection_.front(), "move");
                    return;   // non-builders / can't-build-it: nothing happens
                }
                if (allyId >= 0) {
                    const auto* target=frameUnitP(allyId);
                    bool repaired=false;
                    if (target && target->hp<target->type->maxHp) for (int id:selection_) {
                        const auto* b=frameUnitP(id);
                        if (!b || !b->type || !b->type->isBuilder || b->type->isStructure() ||
                            b->underConstruction || b->id==allyId || b->repeatType) continue;
                        tak::net::Command c;c.kind=tak::net::Cmd::Repair;c.unitId=id;
                        c.targetId=allyId;c.queue=queue;issue(c);repaired=true;
                    }
                    if (repaired) return;
                    bool any = false;
                    for (int id : selection_) {
                        const auto* gu = frameUnitP(id);
                        if (!gu || !gu->type || gu->type->weapon.damage <= 0) continue;
                        tak::net::Command c;
                        c.kind = tak::net::Cmd::Guard;
                        c.unitId = id; c.targetId = allyId; c.queue = queue;
                        issue(c);
                        any = true;
                    }
                    if (any) voice(selection_.front(), "move");
                    return;   // non-attackers: nothing happens
                }
            }
            // Clicking near an enemy = attack; else formation move. (Allies are
            // not enemies -- clicking one falls through to a move, not an attack.)
            int enemy = -1;
            float best = 1e30f;
            for (const UnitR* _up : front().live) {
                const UnitR& u = *_up;
                if (!canPickUnit(u)) continue;
                if (!u.alive() || u.embarked() || !first ||
                    worldTables().allied(u.player, first->player)) continue;
                float d=0;
                if (unitUnderCursor(u,mouseX_,mouseY_,&d) && d<best) {best=d;enemy=u.id;}
            }
            // A reclaimer clicking directly on a reclaimable feature (with no enemy
            // there) reclaims just that one -- retail's single Reclaim.
            if (enemy < 0 && haveReclaimer()) {
                if (const auto fid = reclaimTargetAt(wx, wz)) {
                    int builderId = firstReclaimer();
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Reclaim;
                    c.unitId = builderId;
                    c.targetId = *fid;
                    c.queue = uint8_t(queue ? 1 : 0);
                    issue(c);
                    voice(builderId, "move");
                    return;
                }
            }
            // Every selected unit gets the Attack order, whatever the cursor
            // shows: retail's order code (0x5214b0) never consults the cursor
            // selector 0x4dd780. The sim sorts the units out -- a production
            // building rallies its output at the target, an out-of-range tower
            // keeps the order and fires once the target arrives, and a unit
            // with no weapon drops it (World::attack).
            if (enemy >= 0) {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Attack;
                    c.unitId = id;
                    c.targetId = enemy;
                    c.queue = queue;
                    issue(c);
                }
                voice(selection_.front(), "attack");
            } else {
                voice(selection_.front(), "move");
                // In a Legion net game the selection goes as ONE explicit order,
                // OrderShape::Offsets at the click: the sim gives the members the
                // same goals as the loop below, from their positions at activation.
                setClickMove(wx, wz);
                // Legion packs one shared destination into arrival slots for the
                // surface movers it plans (ground units, boats, hovercraft).
                const bool legion=tak::sim::isLegionPathfinding(worldTables().pathfindingMode());
                float cx = 0, cz = 0;
                int n = 0;
                for (int id : selection_)
                    if (const auto* u = frameUnitP(id)) { cx += u->x; cz += u->z; ++n; }
                if (n) { cx /= float(n); cz /= float(n); }
                for (int id : selection_) {
                    const auto* u = frameUnitP(id);
                    if (!u) continue;
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Move;
                    c.unitId = id;
                    // Legion owns footprint-aware group spreading. Per-unit click
                    // offsets would split one selection into unrelated goals.
                    // Flying units retain their independent flight controller.
                    // Legion plans footprints up to 8 in every surface domain.
                    // A Shift-queued leg shares the point too: once the legs
                    // ahead of it are done it is a plain Move, and the legs one
                    // click queued share its issue tick, so the selection plans
                    // and packs it as one group when it becomes current.
                    const bool shared=u->type && legion&&
                        tak::sim::legionSharedClick(u->type->canFly,u->type->footX,u->type->footZ);
                    c.x = shared ? wx : wx + std::clamp(u->x - cx, -60.0f, 60.0f);
                    c.z = shared ? wz : wz + std::clamp(u->z - cz, -60.0f, 60.0f);
                    c.queue = queue;
                    issue(c);
                }
            }
        }

    void GameView::drawCursorOverlay(bool forceSoftware, bool uiOverlay) {
        if (!cursorsInit_) { cursorsInit_ = true; cursors_.load(ren_, vfs_, settings_); }
        // A benchmark RUN is hands-off: hide the cursor entirely (OS + software). It
        // returns for the stats screen (benchStatsShown_) so DONE is clickable.
        if (benchmarkMode_ && !benchStatsShown_ && !uiOverlay) {
            if (cursorMode_ != 0) { cursors_.releaseHardware(); SDL_ShowCursor(SDL_DISABLE); cursorMode_ = 0; }
            return;
        }
        if (!cursors_.ok()) {   // no cursor art -> just keep the OS arrow
            if (cursorMode_ != 1) { SDL_ShowCursor(SDL_ENABLE); cursorMode_ = 1; }
            return;
        }
        bool fightTint = false;
        tak::CursorId c = uiOverlay ? tak::CursorId::Normal : desiredCursor(fightTint);
        int sc = settings_ ? settings_->cursorScale : 1;
        SDL_Color tint = fightTint ? kFightMoveTint : SDL_Color{255, 255, 255, 255};

        // HARDWARE cursor: hand the sprite to the OS, which tracks the pointer position
        // itself -- so it stays smooth even when a heavy frame stalls our render loop.
        if (settings_ && settings_->hardwareCursor && !forceSoftware && !hwCursorFailed_) {
            if (cursorMode_ != 1) { SDL_ShowCursor(SDL_ENABLE); cursorMode_ = 1; }
            if (cursors_.applyHardware(c, sc, tint)) return;
            hwCursorFailed_ = true;   // platform rejected it (size cap?) -> software from here on
            std::fprintf(stderr, "cursor: hardware cursor unavailable -- using software\n");
        }

        // SOFTWARE cursor: hide the OS arrow and draw our own into the frame.
        if (cursorMode_ != 0) { cursors_.releaseHardware(); SDL_ShowCursor(SDL_DISABLE); cursorMode_ = 0; }
        // Pointer position in renderer-output pixels (the space mouse events are mapped
        // into). Before the first motion, sample the OS position and map it the same way.
        int mx, my;
        if (!uiOverlay && mouseX_ >= 0) { mx = int(mouseX_); my = int(mouseY_); }
        else {
            int wx, wy; SDL_GetMouseState(&wx, &wy);
            float lx, ly; SDL_RenderWindowToLogical(ren_, wx, wy, &lx, &ly);
            mx = int(lx); my = int(ly);
        }
        cursors_.draw(ren_, c, mx, my, sc, tint);
    }

    tak::CursorId GameView::desiredCursor(bool& fightTint) {
        fightTint = false;
        // Overlays / lobby: a plain arrow for clicking UI.
        if (inLobbyPhase() || exitMenu_ || giveUnitsMenu_ || options_)
            return tak::CursorId::Normal;
        // Build icons and their strip cover the map. Do not classify world
        // targets behind them, even with a placement or command armed.
        if (overBuildMenu(mouseX_, mouseY_)) return tak::CursorId::Normal;
        // Native action mode 14 (armed by selecting a build item) returns
        // FindSite for a selected builder; placement validity is shown by the
        // ghost and does not change this cursor slot.
        if (placing_ && mouseX_ >= 0) {
            const bool selectedBuilder=std::any_of(selection_.begin(),selection_.end(),[&](int id) {
                const UnitR* unit=frameUnitP(id);
                return unit && unit->alive() && !unit->embarked() && !unit->underConstruction &&
                       unit->type && unit->type->isBuilder &&
                       !registry_.buildable(unit->type->id).empty();
            });
            return tak::cursorForBuildPlacement(true,selectedBuilder);
        }
        // Right-drag "clear this area": show the broom only once the box is big enough
        // to be an area command (the test that tells a right-CLICK from a box on
        // release). Before that, keep the ordinary hover cursor.
        if (reclaimDrag_ && areaBoxAt(rdAnchor_, mouseX_, mouseY_))
            return tak::CursorId::Reclaim;
        if (dragging_) return tak::CursorId::Normal;       // box-select drag
        if (pendingCmd_)  {
            fightTint = (pendingCmd_ == 'f');
            if (pendingCmd_ == 'a') {
                bool anySelected=false,hasAirstrike=false,allAirstrike=!selection_.empty();
                for(int id:selection_) {
                    const UnitR* unit=frameUnitP(id);
                    if(!unit || !unit->alive() || unit->embarked() || unit->underConstruction || !unit->type ||
                       !unit->type->hasPrimaryWeaponBlock || (unit->repeatType && !unit->type->isStructure()))continue;
                    anySelected=true;
                    const bool airstrike=tak::client::unitHasAirstrikeCursor(
                        *unit->type,unit->weaponSlot);
                    hasAirstrike|=airstrike;allAirstrike&=airstrike;
                }
                if (!anySelected) return tak::CursorId::Normal;
                allAirstrike&=anySelected;
                tak::CursorId ordinaryCursor=tak::CursorId::Normal;
                if(mouseX_>=0 && !selection_.empty()) {
                    float wx,wz;pickWorld(mouseX_,mouseY_,wx,wz);
                    ordinaryCursor=hoverCursor(wx,wz,true);
                }
                return tak::cursorForArmedAttack(ordinaryCursor,hasAirstrike,allAirstrike);
            }
            const UnitR* loadTransport=nullptr;
            int transportCount=0;
            bool hasLoadTarget=false;
            if(pendingCmd_=='l') {
                for(int id:selection_) {
                    const auto* unit=frameUnitP(id);
                    if(unit && unit->alive() && !unit->embarked() && unit->type &&
                       unit->type->canTransport) {
                        ++transportCount;
                        loadTransport=unit;
                    }
                }
                // Native 520b60 requires exactly one selected carrier.
                if(transportCount==1 && mouseX_>=0) {
                    for(const UnitR* unit:front().live) {
                        if(canPickUnit(*unit) && unitUnderCursor(*unit,mouseX_,mouseY_) &&
                           canLoadPassenger(*unit,*loadTransport)) {
                            hasLoadTarget=true;
                            break;
                        }
                    }
                }
            }
            const bool capable=std::any_of(selection_.begin(),selection_.end(),[&](int id) {
                const auto* u=frameUnitP(id);
                if (!u || !u->alive() || u->embarked() || u->underConstruction || !u->type) return false;
                switch (pendingCmd_) {
                    case 'm': return u->type->canMove;
                    case 'p': return u->type->canPatrol;
                    case 'g': return u->type->canGuard;
                    case 'c': return u->type->canReclaim;
                    case 'r': return u->type->isBuilder && !u->type->isStructure();
                    case 'u': return u->type->canTransport;
                    default: return true;
                }
            });
            if (!capable) return tak::CursorId::Normal;
            if (pendingCmd_=='c') {
                float wx,wz;pickWorld(mouseX_,mouseY_,wx,wz);
                return reclaimTargetAt(wx,wz) ? tak::CursorId::Reclaim : tak::CursorId::Normal;
            }
            // Repair and guard are target-dependent in the native selector.
            if (pendingCmd_=='r' || pendingCmd_=='g') {
                bool eligible=false;
                for (const auto* target:front().live) {
                    if (!canPickUnit(*target) || !target->alive() || target->embarked() ||
                        !target->type || !unitUnderCursor(*target,mouseX_,mouseY_)) continue;
                    for (int id:selection_) {
                        const auto* u=frameUnitP(id);
                        if (!u || !u->alive() || u->embarked() || u->underConstruction ||
                            !u->type || u->id==target->id || !worldTables().allied(u->player,target->player)) continue;
                        eligible|=pendingCmd_=='r'
                            ? canAssistSite(*u,*target) || (u->type->isBuilder && !u->type->isStructure() &&
                                !target->underConstruction && target->hp<target->type->maxHp)
                            : u->type->canGuard;
                    }
                }
                if (!eligible) return tak::CursorId::Normal;
            }
            return tak::cursorForArmedCommand(pendingCmd_,transportCount==1,hasLoadTarget);
        }
        if (mouseX_ < 0)  return tak::CursorId::Normal;
        float wx, wz; pickWorld(mouseX_, mouseY_, wx, wz);
        return hoverCursor(wx, wz);
    }

    // A selected unit that takes part in the enemy-hover cursor: retail's
    // `canattack` (WEAPON1 block present) on a live, finished, unembarked unit.
    // A factory's build repeat suppresses it, as a structure's does not.
    bool GameView::isCursorAttacker(const UnitR& u) const {
        return u.alive() && !u.embarked() && !u.underConstruction && u.type &&
               u.type->hasPrimaryWeaponBlock && !(u.repeatType && !u.type->isStructure());
    }

    // The per-unit Attack/TooFar decision over an enemy (cursorCanAttackEnemy).
    // Cursor only: the right-click order still goes to every selected unit,
    // as retail's order code never consults this decision.
    bool GameView::attackerCanHitEnemy(const UnitR& attacker,const UnitR& target,
                                       bool ignoreAirstrikeWeapons) const {
        return isCursorAttacker(attacker) && target.type &&
               tak::client::cursorCanAttackEnemy(*attacker.type,attacker.weaponSlot,
                   attacker.worldPosition,*target.type,target.worldPosition,
                   target.flightGroundMode,ignoreAirstrikeWeapons);
    }

    tak::CursorId GameView::hoverCursor(float wx, float wz,bool ignoreAirstrikeWeapons) {
        const auto* first = selection_.empty() ? nullptr : frameUnitP(selection_.front());

        // Selected transport carrying cargo -> unload cursor anywhere.
        if (first && first->type && first->type->canTransport && !first->cargo.empty())
            return tak::CursorId::Unload;

        if (first) {
            // One SCREEN-SPACE pass over the drawn sprites (unitUnderCursor: the
            // projected model bounds + lift + flyer altitude -- the exact region
            // a click selects), categorised; overlaps resolve to the nearest
            // sprite centre. The old world-space 20-22px centre radii ignored
            // footprints entirely: most of a keep hovered as bare ground.
            int loadId = -1, siteId = -1, ownId = -1, allyId = -1, enemy = -1;
            float bLoad = 1e30f, bSite = 1e30f, bOwn = 1e30f, bAlly = 1e30f,
                  bEnemy = 1e30f;
            for (const UnitR* _up : front().live) { const UnitR& u = *_up;
                if (!canPickUnit(u)) continue;
                if (!u.alive() || u.embarked() || !u.type) continue;
                float d = 0;
                if (!unitUnderCursor(u, mouseX_, mouseY_, &d)) continue;
                bool ally = worldTables().allied(u.player, first->player);
                if (ally && u.type->canTransport && u.player == first->player &&
                    !u.underConstruction) {
                    if (d < bLoad) { bLoad = d; loadId = u.id; }
                } else if (ally && u.underConstruction) {
                    if (d < bSite) { bSite = d; siteId = u.id; }
                } else if (ally && u.player == localPlayer_) {
                    if (d < bOwn) { bOwn = d; ownId = u.id; }
                } else if (ally) {
                    if (d < bAlly) { bAlly = d; allyId = u.id; }
                } else {
                    if (d < bEnemy) { bEnemy = d; enemy = u.id; }
                }
            }
            if (loadId >= 0) {
                const auto* transport=frameUnitP(loadId);
                for (int id:selection_) {
                    const auto* passenger=frameUnitP(id);
                    if (passenger && canLoadPassenger(*passenger,*transport)) return tak::CursorId::Load;
                }
                return tak::CursorId::Select;
            }
            if (siteId >= 0) {
                const auto* site=frameUnitP(siteId);
                for (int id:selection_) {
                    const auto* builder=frameUnitP(id);
                    if (builder && canAssistSite(*builder,*site)) return tak::CursorId::Repair;
                }
                return tak::CursorId::Green;
            }
            if (ownId>=0 || allyId>=0) {
                const auto* target=frameUnitP(ownId>=0 ? ownId : allyId);
                if (target && target->hp<target->type->maxHp) for (int id:selection_) {
                    const auto* builder=frameUnitP(id);
                    if (builder && builder->alive() && !builder->embarked() && !builder->underConstruction &&
                        builder->id!=target->id && builder->type && builder->type->isBuilder &&
                        builder->type->buildMovementCode && !builder->type->isStructure() && !builder->repeatType)
                        return tak::CursorId::Repair;
                }
            }
            if (ownId  >= 0) return tak::CursorId::Select;
            if (allyId >= 0) return tak::CursorId::Green;
            if (enemy >= 0) {
                const UnitR* target = frameUnitP(enemy);
                if (!target || !target->type) return tak::CursorId::TooFar;
                // Native 0x521cd0 keeps the lowest per-unit slot: any selected
                // unit that can attack shows Attack, else TooFar when some
                // selected unit is an attacker at all.
                bool canAttack=false;
                for (int id : selection_) {
                    const UnitR* attacker = frameUnitP(id);
                    if (!attacker || !isCursorAttacker(*attacker)) continue;
                    canAttack=true;
                    if (attackerCanHitEnemy(*attacker,*target,ignoreAirstrikeWeapons))
                        return tak::CursorId::Attack;
                }
                return canAttack ? tak::CursorId::TooFar : tak::CursorId::Normal;
            }

            for (const auto* corpse:front().live) {
                if (corpse->alive() || !corpse->corpsePhase || corpse->corpseFeat<0 ||
                    !canPickPoint(corpse->x,corpse->z)) continue;
                if (!worldTables().featureTypes().at(size_t(corpse->corpseFeat)).resurrectable) continue;
                tak::client::ReclaimPick pick{wx,wz};   // same corpse pick circle as reclaim
                pick.offer(-corpse->id,corpse->x,corpse->z,corpse->corpseFootX,corpse->corpseFootZ);
                if (!pick.id) continue;
                for (int id:selection_) {
                    const auto* caster=frameUnitP(id);
                    if (!caster || !caster->alive() || caster->embarked() || caster->underConstruction || !caster->type) continue;
                    if ((caster->type->canResurrect && caster->player==corpse->player) ||
                        (caster->type->canAnimate && caster->type->animateType)) return tak::CursorId::Revive;
                }
            }
            if (haveReclaimer() && reclaimTargetAt(wx,wz)) return tak::CursorId::Reclaim;

            // Empty ground: plain arrow. The Move cursor shows ONLY when the move order
            // is armed (Move button / hotkey), not merely from having a unit selected.
            return tak::CursorId::Normal;
        }

        // Nothing selected: highlight your own unit under the pointer, else the arrow.
        for (const UnitR* _up : front().live) { const UnitR& u = *_up;
            if (!canPickUnit(u)) continue;
            if (!u.alive() || u.embarked() || !u.type || u.player != localPlayer_) continue;
            if (!u.underConstruction && unitUnderCursor(u,mouseX_,mouseY_)) return tak::CursorId::Select;
        }
        return tak::CursorId::Normal;
    }

    void GameView::postNotice(std::string msg, float t) {
        if (std::this_thread::get_id() == mainThreadId_) { notice_ = std::move(msg); noticeTimer_ = t; return; }
        std::lock_guard<std::mutex> lk(noticeMutex_);
        pendingNotice_ = std::move(msg); pendingNoticeTimer_ = t; pendingNoticeSet_ = true;
    }

    void GameView::drainPendingNotice() {   // main thread only
        std::lock_guard<std::mutex> lk(noticeMutex_);
        if (pendingNoticeSet_) { notice_ = std::move(pendingNotice_); noticeTimer_ = pendingNoticeTimer_; pendingNoticeSet_ = false; }
    }

    void GameView::resetMinimap() {
        if (miniThread_.joinable()) miniThread_.join();
        miniBuilding_ = miniReady_ = false;
        miniPix_.clear();
        if (miniTex_) { gpuvram::destroy(miniTex_); miniTex_ = nullptr; }
    }

    int GameView::cmdPanelW() const {
        if (gui_.gadgets.empty()) return panelW();
        return std::max(miniSize() + 12, int(128 * guiS()) + 8);
    }

    SDL_FRect GameView::guiCmdRect(const tak::gui::Gadget& g) const {
        float s = guiS();
        // 640-space y=480 (screen bottom in retail) maps just above our info bar so
        // the command panel and the existing bottom bar don't overlap.
        float baseY = winH_ - barH();
        return {winW_ - (640 - g.x) * s, baseY - (480 - g.y) * s, g.w * s, g.h * s};
    }

    SDL_FRect GameView::guiBarRect(const tak::gui::Gadget& g) const {
        float vs = float(barH()) / 49.0f;   // 49-tall retail bar -> barH() px
        float barTop = winH_ - barH();
        return {g.x * vs, barTop + (g.y - 431) * vs, g.w * vs, g.h * vs};
    }

    SDL_FRect GameView::minimapRect(int winW, int winH) const {
        float aspect = float(mapView_.map().blocksY) / float(mapView_.map().blocksX);
        if (fsRadar_) {
            // Retail sets the rect to exactly the world viewport (0, 0,
            // screenW-128, screenH-48) so the command panel and the bottom bar
            // stay visible. We fit the map's aspect INSIDE that viewport and
            // centre it rather than stretching to fill: retail's corner box is
            // square while ours already honours the map's shape, and stretching a
            // 2:1 map to a 16:9 viewport would distort every distance on it.
            // Spectators have no bottom HUD: use the entire available height.
            float vw = float(mapViewW(winW)), vh = float(winH) - (spectating_ ? 0 : barH());
            float w = vw, h = vw * aspect;
            if (h > vh) { h = vh; w = vh / aspect; }
            return {(vw - w) * 0.5f, (vh - h) * 0.5f, w, h};
        }
        return {float(winW) - miniSize() - 10, 10, float(miniSize()), float(miniSize()) * aspect};
    }

    void GameView::buildMinimap() {
        if (miniBuilding_) {
            std::lock_guard<std::mutex> lk(miniMu_);
            if (!miniReady_) return;    // still crunching
            int bw = mapView_.map().blocksX, bh = mapView_.map().blocksY;
            miniTex_ = gpuvram::create(ren_, SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC, bw, bh);
            SDL_UpdateTexture(miniTex_, nullptr, miniPix_.data(), bw * 4);
            SDL_SetTextureScaleMode(miniTex_, SDL_ScaleModeLinear);
            miniPix_.clear();
            miniBuilding_ = false;
            if (miniThread_.joinable()) miniThread_.join();
            return;
        }
        miniBuilding_ = true;
        miniReady_ = false;
        if (miniThread_.joinable()) miniThread_.join();   // stale thread from a reload
        miniThread_ = std::thread([this] {
            int bw = mapView_.map().blocksX, bh = mapView_.map().blocksY;
            std::vector<uint8_t> pix(size_t(bw) * bh * 4);
            std::vector<uint8_t> block(32 * 32 * 4);
            for (int bz = 0; bz < bh; ++bz)
                for (int bx = 0; bx < bw; ++bx) {
                    mapView_.compositor().renderBlock(mapView_.map(), bx, bz, block, 32, 0, 0);
                    uint32_t r = 0, g = 0, b = 0;
                    for (size_t i = 0; i < block.size(); i += 4) {
                        r += block[i]; g += block[i + 1]; b += block[i + 2];
                    }
                    size_t n = block.size() / 4;
                    uint8_t* p = &pix[(size_t(bz) * bw + bx) * 4];
                    p[0] = uint8_t(r / n); p[1] = uint8_t(g / n); p[2] = uint8_t(b / n);
                    p[3] = 255;
                }
            std::lock_guard<std::mutex> lk(miniMu_);
            miniPix_ = std::move(pix);
            miniReady_ = true;
        });
    }

    void GameView::drawStatsPanel(int winW, int winH) {
        if (!statsPanel_ || !hudFont_.ok()) return;

        // WHERE THE GAP IS. The right strip holds the minimap at the top and the command
        // panel at the bottom; everything between them is the black space this fills.
        // Both edges move: the minimap is as tall as the map's aspect makes it, and the
        // command panel is anchored to the bottom bar and scaled by guiS(). So measure
        // them rather than assuming a layout -- a hard-coded gap would overlap the
        // command panel on a tall map, or float in mid-strip on a wide one.
        const float pad = std::max(4.0f, 6.0f * uiScale_);
        float x0 = float(mapViewW(winW)) + pad;
        float x1 = float(winW) - pad;
        // TAB (full-screen radar) moves the minimap out of the strip entirely and over
        // the world viewport, so the strip is free all the way to the top.
        float top = pad;
        if (!fsRadar_) {
            SDL_FRect mr = minimapRect(winW, winH);
            top = mr.y + mr.h + 2 + pad;
        }
        // Bottom edge: the top of the command panel art when a GUI is loaded, else the
        // bottom bar. A spectator gets neither, so the strip runs to the window edge.
        float bot = spectating_ ? float(winH) - pad : float(winH) - barH() - pad;
        if (!spectating_) {
            int mi = guiIdx("UnitMenu");
            if (mi >= 0 && mi < int(gui_.gadgets.size()))
                bot = guiCmdRect(gui_.gadgets[size_t(mi)]).y - pad;
        }
        float availW = x1 - x0, availH = bot - top;
        if (availW <= 8 || availH <= 4) return;

        // WHAT TO SHOW, most useful first. Rows are dropped from the END of this list,
        // so the ordering is the priority ordering: frame rate before link quality before
        // counts before process stats.
        //
        // Labels are kept to the width budget below (<=10 chars) and values to <=7, which
        // is what lets the type size stay put instead of resizing when a value gains a
        // digit.
        struct Row { const char* label; std::string value; };
        std::vector<Row> rows;
        char b[48];

        std::snprintf(b, sizeof b, "%.0f", fps_ + 0.5f);
        rows.push_back({"FPS", b});

        if (mp_) {
            std::snprintf(b, sizeof b, "%.0f MS", netRttMs());
            rows.push_back({"PING", b});
        }
        if (actualSpeed_ > 0) {
            // The measured speed as retail's level (NORMAL, +5, -3), not a multiplier.
            rows.push_back({"SIM", tak::speedLevelText(tak::speedLevelOf(actualSpeed_))});
        }

        // Living units owned by the player, including embarked units. Spectators
        // have no player army and retain the match-wide living count.
        int units = 0;
        for (const UnitR* up : front().live)
            if (up->alive() && up->type && (spectating_ || up->player == localPlayer_)) ++units;
        std::snprintf(b, sizeof b, "%d", units);
        rows.push_back({"UNITS", b});

        const std::time_t clockNow = std::time(nullptr);
        std::tm localClock{};
#ifdef _WIN32
        const bool haveClock = localtime_s(&localClock, &clockNow) == 0;
#else
        const bool haveClock = localtime_r(&clockNow, &localClock) != nullptr;
#endif
        if (haveClock) std::strftime(b, sizeof b, "%H:%M", &localClock);
        else std::snprintf(b, sizeof b, "--:--");
        rows.push_back({"CLOCK", b});

        if (!spectating_) {
            std::snprintf(b, sizeof b, "%d", framePlayer(localPlayer_).kills);
            rows.push_back({"KILLS", b});
            rows.push_back({"SCORE", std::to_string(framePlayer(localPlayer_).score)});
        }
        const uint64_t wallNow = SDL_GetTicks64();
        const uint64_t elapsed = gameStartMs_ && wallNow >= gameStartMs_
            ? (wallNow - gameStartMs_) / 1000 : 0;
        std::snprintf(b, sizeof b, "%llu:%02llu",
                      (unsigned long long)(elapsed / 60), (unsigned long long)(elapsed % 60));
        rows.push_back({"REAL TIME", b});
        const uint32_t gameSecs = front().gameTick / uint32_t(tak::net::kServerHz);
        std::snprintf(b, sizeof b, "%u:%02u", gameSecs / 60, gameSecs % 60);
        rows.push_back({"GAME TIME", b});

        const uint64_t now = SDL_GetTicks64();
        if (!statsSampleAt_ || now - statsSampleAt_ >= 1000) {
            const auto client = tak::proc::sample(0);
            statsCpuPct_ = tak::proc::processCpuPercent(statsProcess_, client,
                statsSampleAt_ ? double(now-statsSampleAt_)/1000.0 : 0.0, tak::proc::numCpus());
            statsProcess_ = client;
            if(localServerPid_>0) {
                const auto server=tak::proc::sample(localServerPid_);
                statsServerCpuPct_=tak::proc::processCpuPercent(statsServer_,server,
                    statsSampleAt_ ? double(now-statsSampleAt_)/1000.0 : 0.0,tak::proc::numCpus());
                statsServer_=server;
            }
            statsSampleAt_ = now;
        }
        // GPU driver queries can take time. Never wait for them in a
        // frame: keep one background request in flight and reuse the last result.
        if (statsGpuPending_.valid() &&
            statsGpuPending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            statsGpu_ = statsGpuPending_.get();
        if (!statsGpuPending_.valid() && (!statsGpuAt_ || now - statsGpuAt_ >= 2000)) {
            statsGpuAt_ = now;
            statsGpuPending_ = std::async(std::launch::async, [] { return tak::proc::gpuSample(); });
        }
        if (statsCpuPct_ >= 0) std::snprintf(b, sizeof b, "%.0f%%", statsCpuPct_);
        else std::snprintf(b, sizeof b, "N/A");
        rows.push_back({"CLIENT CPU", b});   // client share of total machine CPU capacity, 0–100%
        if(localServerPid_>0) {
            if(statsServerCpuPct_>=0)std::snprintf(b,sizeof b,"%.0f%%",statsServerCpuPct_);
            else std::snprintf(b,sizeof b,"N/A");
            rows.push_back({"SERVER CPU",b});
        }
        const double gpuPct=tak::proc::systemGpuPercent(statsGpu_);
        if (gpuPct >= 0)
            std::snprintf(b, sizeof b, "%.0f%%", gpuPct);
        else std::snprintf(b, sizeof b, "N/A");
        rows.push_back({"GPU", b});

        if (statsProcess_.ok && statsProcess_.rssBytes) {
            std::snprintf(b, sizeof b, "%zu MB", statsProcess_.rssBytes >> 20);
            rows.push_back({"MEM", b});
        }
        std::snprintf(b, sizeof b, "%zu MB", gpuvram::bytes() >> 20);
        rows.push_back({"VID", b});
        rows.push_back({"RENDERER", tak::renderdriver::shortLabel(tak::renderdriver::active().created)});

        // The face the mana readout uses (client/uitext.h), so the panel matches the HUD
        // it sits in rather than introducing a second face.
        //
        // Fixed size follows UI scale. Reserve ten label characters (SERVER CPU),
        // one separator and seven value characters without overlapping columns: the
        // per-character budget is the block cell (6), or the UI font's average over
        // such a row when that is wider, so the columns never meet in either face.
        constexpr int kColBudget = 18;
        const float px = 1.7f * uiScale_;
        const float glyphW = std::max(6.0f, blockWidth("SERVER CPU 9999 MB", 1.0f) / float(kColBudget));
        const tak::hud::StatsFit fit =
            tak::hud::fitStats(int(rows.size()), availW, availH, kColBudget,
                               glyphW, /*glyphH=*/7.0f, px,
                               // Breathing room between rows. The gap under the minimap
                               // runs to hundreds of pixels while nine rows need barely a
                               // hundred, so there is room to spare -- and this only ever
                               // feeds the HEIGHT fit, never the type size.
                               /*rowPad=*/6.0f * uiScale_);
        if (!fit.visible) return;
        rows.resize(size_t(fit.rows));

        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        const SDL_Color mc{200, 215, 255, 255};   // the mana readout's colour
        float y = top;
        for (const auto& r : rows) {
            blockText(r.label, x0, y, px, mc);
            // Values right-align against the strip edge so the digits form a column and a
            // changing number does not shuffle the ones above it sideways.
            blockText(r.value, x1 - blockWidth(r.value, px), y, px, mc);
            y += fit.rowH;
        }
    }

    void GameView::drawMinimap(int winW, int winH) {
        (void)winW;
        if (!miniTex_) buildMinimap();
        SDL_FRect r = minimapRect(winW, winH);
        // Full-screen: black out the WHOLE world viewport first. Retail's radar
        // rect simply is the viewport, so no world shows around it; ours fits the
        // map's aspect inside, which would leave the terrain peeking through the
        // letterbox margins and read as a window rather than a map.
        SDL_FRect frame = fsRadar_
            ? SDL_FRect{0, 0, float(mapViewW(winW)), float(winH) - (spectating_ ? 0 : barH())}
            : SDL_FRect{r.x - 2, r.y - 2, r.w + 4, r.h + 4};
        SDL_SetRenderDrawColor(ren_, 30, 30, 40, 255);
        SDL_RenderFillRectF(ren_, &frame);
        if (miniTex_) SDL_RenderCopyF(ren_, miniTex_, nullptr, &r);
        if (fogTex_) SDL_RenderCopyF(ren_, fogTex_, nullptr, &r);

        float mapW = float(mapView_.map().blocksX) * 32;
        float mapH = float(mapView_.map().blocksY) * 32;
        auto toMini = [&](float wx, float wz) {
            return SDL_FPoint{r.x + wx / mapW * r.w, r.y + wz / mapH * r.h};
        };
        // All unit dots batched into one draw call (per-unit FillRect + colour
        // set was thousands of state changes a frame at large unit counts).
        overlayBatch_.clear();
        for (const UnitR* _up : front().live) { const UnitR& u = *_up;
            // A spectator (noFog_) sees every unit on the radar; a player sees only
            // allied units and enemies currently in view. (Shared with Tactical Dots.)
            // The +Radar power code shows them all here, on the minimap only (retail's
            // one reader of the flag is the minimap dot pass, 0x4f9561).
            if (radarCode_ ? !u.alive() || u.embarked() || !u.type : !radarVisible(u)) continue;
            SDL_FPoint p = toMini(u.x, u.z);
            SDL_Color tc = playerColor(u.player);
            pushQuad(overlayBatch_, p.x - 1.5f, p.y - 1.5f, 3, 3, tc);
        }
        if (!overlayBatch_.empty())
            SDL_RenderGeometry(ren_, nullptr, overlayBatch_.data(),
                               int(overlayBatch_.size()), nullptr, 0);
        // Camera view rectangle.
        float zm = mapView_.zoom();
        SDL_FPoint a = toMini(mapView_.offX(), mapView_.offY());
        SDL_FRect view{a.x, a.y, winW / zm / mapW * r.w,
                       (winH - barH()) / zm / mapH * r.h};
        SDL_SetRenderDrawColor(ren_, 240, 240, 240, 200);
        SDL_RenderDrawRectF(ren_, &view);
    }

    bool GameView::minimapToWorld(float mx, float my, int winW, int winH, float& wx, float& wz) {
        SDL_FRect r = minimapRect(winW, winH);
        if (mx < r.x || my < r.y || mx > r.x + r.w || my > r.y + r.h) return false;
        wx = (mx - r.x) / r.w * float(mapView_.map().blocksX) * 32;
        wz = (my - r.y) / r.h * float(mapView_.map().blocksY) * 32;
        return true;
    }

    bool GameView::minimapClick(float mx, float my, int winW, int winH) {
        float wx, wz;
        if (!minimapToWorld(mx, my, winW, winH, wx, wz)) return false;
        float zm = mapView_.zoom();   // centre the clicked point in the map viewport
        mapView_.setOffset(wx - mapViewW(winW) / zm / 2,
                           wz - (winH - int(barH())) / zm / 2);
        return true;
    }

    bool GameView::minimapOrder(float mx, float my, int winW, int winH, bool queue) {
        float wx, wz;
        if (selection_.empty() || !minimapToWorld(mx, my, winW, winH, wx, wz)) return false;
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (!u || u->player != localPlayer_) continue;
            tak::net::Command c;
            c.kind = tak::net::Cmd::Move;
            c.unitId = id;
            c.x = wx;
            c.z = wz;
            c.queue = queue ? 1 : 0;
            issue(c);
        }
        if (const auto* u = frameUnitP(selection_.front()); u && u->player == localPlayer_)
            voice(selection_.front(), "move");
        return true;
    }

    void GameView::issueArmedOrder(char cmd, float wx, float wz, bool queue, bool precise) {
        ClickScope click(*this);   // one click: a Legion net game sends it as explicit orders
        if (selection_.empty()) return;
        if (cmd == 'c') {   // clear/reclaim: reclaim the feature under the cursor
            if (!haveReclaimer()) return;
            const auto fid = reclaimTargetAt(wx, wz);
            if (!fid) return;
            int builderId = firstReclaimer();
            tak::net::Command c;
            c.kind = tak::net::Cmd::Reclaim;
            c.unitId = builderId;
            c.targetId = *fid;
            c.queue = queue ? 1 : 0;
            issue(c);
            voice(builderId, "move");
            return;
        }
        if (cmd == 'r') { // repair or resume construction, using the cursor's eligibility
            int worker=-1,targetId=-1;float best=precise ? 1e30f : 96.f*96.f;
            for (const auto* target:front().live) {
                if (!canPickUnit(*target) || !target->alive() || target->embarked() || !target->type) continue;
                float d=(target->x-wx)*(target->x-wx)+(target->z-wz)*(target->z-wz);
                if (precise && !unitUnderCursor(*target,mouseX_,mouseY_,&d)) continue;
                if (d>=best) continue;
                for (int id:selection_) {
                    const auto* b=frameUnitP(id);
                    if (!b || !b->alive() || b->embarked() || b->underConstruction || !b->type ||
                        !b->type->isBuilder || b->type->isStructure() || b->repeatType ||
                        b->id==target->id || !worldTables().allied(b->player,target->player)) continue;
                    if (target->underConstruction ? !canAssistSite(*b,*target) : target->hp>=target->type->maxHp) continue;
                    worker=id;targetId=target->id;best=d;break;
                }
            }
            if (targetId<0) return;
            tak::net::Command c;
            c.kind=frameUnitP(targetId)->underConstruction ? tak::net::Cmd::Assist : tak::net::Cmd::Repair;
            c.unitId=worker;c.targetId=targetId;c.queue=queue;issue(c);voice(worker,"move");
            return;
        }
        if (cmd == 'u') {   // unload: selected transport(s) sail to (wx,wz), disembark
            bool any = false;
            for (int id : selection_) {
                const auto* u = frameUnitP(id);
                if (!u || !u->type || !u->type->canTransport ||
                    (!queue && u->cargo.empty())) continue;
                tak::net::Command c;
                c.kind = tak::net::Cmd::Unload;
                c.targetId = unloadDestinationHeight(mapView_.map(),wx,wz);
                c.unitId = id;
                c.x = wx;
                c.z = wz;
                c.queue = queue ? 1 : 0;
                issue(c);
                any = true;
            }
            if (any) voice(selection_.front(), "move");
            return;
        }
        if (cmd == 'l') {   // load: the friendly unit under the cursor boards a transport
            int transportId = -1, pid = -1;
            float best = 24.0f * 24.0f;
            for (int id : selection_) {
                const auto* t = frameUnitP(id);
                if (!t || !t->type || !t->alive() || t->embarked() ||
                    t->underConstruction || !t->type->canTransport) continue;
                for (const UnitR* up : front().live) {
                    const auto& u=*up;
                    if (!canPickUnit(u) || !canLoadPassenger(u,*t)) continue;
                    const float dx=u.x-wx,dz=u.z-wz,d=dx*dx+dz*dz;
                    if (d<best) {best=d;pid=u.id;transportId=id;}
                }
            }
            if (pid < 0) return;
            tak::net::Command c;
            c.kind = tak::net::Cmd::Load;
            c.unitId = pid;
            c.targetId = transportId;
            c.queue = queue ? 1 : 0;
            issue(c);
            voice(transportId, "move");
            return;
        }
        if (cmd == 'g') {   // guard: needs a friendly unit
            int buddy = -1;
            float best = precise ? 24.0f : 96.0f;   // generous radius on the minimap
            best *= best;
            for (const UnitR* _up : front().live) { const UnitR& u = *_up;
                if (!canPickUnit(u)) continue;
                if (!u.alive() || u.player != localPlayer_) continue;
                float dx = u.x - wx, dz = u.z - wz, d = dx * dx + dz * dz;
                if (d < best) { best = d; buddy = u.id; }
            }
            if (buddy < 0) return;
            for (int id : selection_) {
                if (id == buddy) continue;
                tak::net::Command c;
                c.kind = tak::net::Cmd::Guard;
                c.unitId = id;
                c.targetId = buddy;
                c.queue = queue ? 1 : 0;
                issue(c);
            }
            voice(selection_.front(), "guard");
            return;
        }
        // 'a' (attack) targets an enemy under a precise click; otherwise (and always
        // on the minimap) it is an attack-move to the ground point.
        int enemy = -1;
        if (cmd == 'a' && precise) {
            const auto* first = frameUnitP(selection_.front());
            float best = 20 * 20;
            for (const UnitR* _up : front().live) { const UnitR& u = *_up;
                if (!canPickUnit(u)) continue;
                if (!u.alive() || u.embarked() || !first ||
                    worldTables().allied(u.player, first->player))
                    continue;
                float dx = u.x - wx, dz = u.z - wz;
                if (dx * dx + dz * dz < best) { best = dx * dx + dz * dz; enemy = u.id; }
            }
        }
        for (int id : selection_) {
            tak::net::Command c;
            if (enemy >= 0) {
                c.kind = tak::net::Cmd::Attack;
                c.targetId = enemy;
            } else {
                c.kind = (cmd == 'f' || cmd == 'a') ? tak::net::Cmd::AttackMove
                         : cmd == 'p'               ? tak::net::Cmd::Patrol
                                                    : tak::net::Cmd::Move;
                c.x = wx;
                c.z = wz;
            }
            c.unitId = id;
            c.queue = queue ? 1 : 0;
            issue(c);
        }
        voice(selection_.front(),
              (cmd == 'a' || cmd == 'f') ? "attack" : cmd == 'p' ? "patrol" : "move");
    }

    bool GameView::minimapArmedOrder(float mx, float my, int winW, int winH) {
        if (!pendingCmd_) return false;
        float wx, wz;
        if (!minimapToWorld(mx, my, winW, winH, wx, wz)) return false;
        const bool queue = (SDL_GetModState() & KMOD_SHIFT) != 0;
        issueArmedOrder(pendingCmd_, wx, wz, queue, false);
        armedOrderIssued(queue);
        return true;
    }

    void GameView::armedOrderIssued(bool queue) {
        // Shift queues the order AND keeps its command armed for the next
        // click; without Shift the order disarms at once.
        shiftOrderArmed_ = queue;
        if (!queue) pendingCmd_ = 0;
    }

    void GameView::loadInterfaceFonts() {
        // By role (client/font.h): bodfontbody and the Times faces are reading text and
        // follow UI FONT; font48 is retail lettering and always draws its art.
        auto load = [&](Font& font, const char* path, FontRole role) {
            font.destroyGlyphs();
            try { font = Font(ren_, vfs_, path, role); }
            catch (const std::exception& e) { std::fprintf(stderr, "font load: %s\n", e.what()); }
        };
        load(hudFont_, "fonts/bodfontbody.gaf", FontRole::Text);
        load(bigFont_, "fonts/font48.gaf", FontRole::Art);
        load(statFont_, "fonts/b_times new roman (100b).gaf", FontRole::Text);
        if (!statFont_.ok()) load(statFont_, "fonts/ig_times new roman (100).gaf", FontRole::Text);
        load(scoreboardFont_, "fonts/ig_times new roman (100).gaf", FontRole::Text);
        scoreboardFont_.setLetterSpacing(0);
        // The embedded TrueType face (client/uifont.h) is not art; only its glyph sheets
        // go, and the next draw rebuilds them.
        tak::releaseUiTextGlyphs();
    }

    void GameView::reloadInterfaceArt(const tak::Settings& s) {
        loadInterfaceFonts();
        loadPanel(side_);
        loadGui(side_);
        loadOrderButtons();
        for (auto& [name, texture] : weaponIcons_) gpuvram::destroy(texture);
        weaponIcons_.clear();
        for (auto& [name, texture] : icons_) if (texture) gpuvram::destroy(texture);
        icons_.clear();   // build portraits reload smoothed (or not) on next use
        for (auto& [name, texture] : scoreboardLogos_) gpuvram::destroy(texture);
        scoreboardLogos_.clear();
        auto kill = [](SDL_Texture*& t) { gpuvram::destroy(t); t = nullptr; };
        kill(unitInfoBg_); kill(unitInfoIcon_);
        unitInfoIconFor_.clear();
        for (auto& texture : unitInfoOk_) kill(texture);
        unitInfoLabelFont_.destroyGlyphs(); unitInfoValueFont_.destroyGlyphs();
        if (cursorsInit_) cursors_.load(ren_, vfs_, &s);
        cursorMode_ = -1;
        hwCursorFailed_ = false;
    }

    void GameView::loadPanel(const std::string& side) {
        gpuvram::destroy(panelTex_); panelTex_ = nullptr;
        gpuvram::destroy(botTex_); botTex_ = nullptr;
        std::string base = "anims/" + side + "ingame";
        try {
            auto pal = tak::gaf::Palette::fromBytes(vread(base + ".pcx"), base + ".pcx");
            for (auto& sq : tak::gaf::load(vread(base + ".gaf"), pal, -1, base + ".gaf")) {
                if (sq.frames.empty()) continue;
                auto& f = sq.frames[0];
                // Iron Plague's Creon art (creingame.gaf) names the same two panels
                // ButtonPanel and InfoPanel; the four original sides use Aid*/Main*.
                if (sq.name == "AidPanel" || sq.name == "MainPanel" || sq.name == "ButtonPanel") {
                    // panelW_/panelH_ keep the 1x LOGICAL size, which is what the HUD
                    // lays out in -- so the texture being built at 2x is invisible here.
                    panelTex_ = tak::art::makeTexture(ren_, f.rgba, f.width, f.height);
                    panelW_ = f.width;
                    panelH_ = f.height;
                } else if (sq.name == "AidBotPanel" || sq.name == "BottomPanel" ||
                           sq.name == "InfoPanel") {
                    botTex_ = tak::art::makeTexture(ren_, f.rgba, f.width, f.height);
                    botW_ = f.width;
                    botH_ = f.height;
                }
            }
        } catch (const std::exception&) {}
    }

    tak::gaf::Palette GameView::guiPalette(const std::string& gaf) {
        std::string pp = "anims/" + gaf + ".pcx";
        try {
            return tak::gaf::Palette::fromBytes(vread(pp), pp);
        } catch (const std::exception&) {}
        try {
            return tak::gaf::Palette::fromBytes(vread("palettes/guipal.pal"),
                                                "palettes/guipal.pal");
        } catch (const std::exception&) {}
        return {};
    }

    SDL_Texture* GameView::loadGuiFrame(const std::string& gaf, const std::string& seq, int frame) {
        if (gaf.empty() || seq.empty()) return nullptr;
        std::string gp = "anims/" + gaf;
        if (gp.size() < 4 || gp.substr(gp.size() - 4) != ".gaf") gp += ".gaf";
        try {
            auto pal = guiPalette(gaf.size() >= 4 && gaf.substr(gaf.size() - 4) == ".gaf"
                                      ? gaf.substr(0, gaf.size() - 4)
                                      : gaf);
            for (auto& sq : tak::gaf::load(vread(gp), pal, -1, gp)) {
                if (sq.name != seq) continue;
                if (frame < 0 || size_t(frame) >= sq.frames.size()) frame = 0;
                if (sq.frames.empty()) return nullptr;
                auto& f = sq.frames[size_t(frame)];
                if (f.width == 0 || f.height == 0) return nullptr;
                SDL_Texture* t = tak::art::makeTexture(ren_, f.rgba, f.width, f.height);
                return t;
            }
        } catch (const std::exception&) {}
        return nullptr;
    }

    void GameView::loadGui(const std::string& side) {
        for (auto& v : guiTex_)
            for (auto* t : v)
                if (t) gpuvram::destroy(t);
        guiTex_.clear();
        gui_ = {};
        if (tak::devFlag("TAK_NOGUI")) return;   // debug: shoot the fallback stone HUD instead
        std::string path = "guis/" + side + "ingame.gui";
        try {
            gui_ = tak::gui::parse(vread(path), path);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "loadGui: %s: %s\n", path.c_str(), e.what());
            return;
        }
        guiTex_.resize(gui_.gadgets.size());
        for (size_t i = 0; i < gui_.gadgets.size(); ++i) {
            const auto& g = gui_.gadgets[i];
            for (const auto& im : g.imgs)
                guiTex_[i].push_back(loadGuiFrame(im.gaf, im.seq, im.frame));
        }
        if (tak::devEnv("TAK_GUIDEBUG")) {
            std::fprintf(stderr, "== %s: %zu gadgets ==\n", path.c_str(),
                         gui_.gadgets.size());
            for (size_t i = 0; i < gui_.gadgets.size(); ++i) {
                const auto& g = gui_.gadgets[i];
                std::fprintf(stderr, "  [%2zu] t%-2d %-18s (%3d,%3d %3dx%3d)", i, g.type,
                             g.name.c_str(), g.x, g.y, g.w, g.h);
                for (size_t k = 0; k < g.imgs.size(); ++k)
                    std::fprintf(stderr, " %s:%s#%d%s", g.imgs[k].gaf.c_str(),
                                 g.imgs[k].seq.c_str(), g.imgs[k].frame,
                                 guiTex_[i][k] ? "" : "(!)");
                std::fprintf(stderr, "\n");
            }
        }
    }

    void GameView::loadOrderButtons() {
        for (auto& b : orderBtns_) for (auto* texture : b.frames) gpuvram::destroy(texture);
        orderBtns_.clear();
        auto grab = [&](const char* gaf, const char* seq, char cmd,
                        const char* label, int f0 = 0, int f1 = 1, int f2 = 2) {
            try {
                std::string pp = "anims/" + std::string(gaf) + ".pcx";
                std::string gp = "anims/" + std::string(gaf) + ".gaf";
                auto pal = tak::gaf::Palette::fromBytes(vread(pp), pp);
                for (auto& sq : tak::gaf::load(vread(gp), pal, -1, gp)) {
                    if (sq.name != seq || sq.frames.size() < 3) continue;
                    OrderBtn b;
                    b.cmd = cmd;
                    b.label = label;
                    int idx[3] = {f0, f1, f2};
                    for (int i = 0; i < 3; ++i) {
                        auto& f = sq.frames[size_t(idx[i])];
                        if (f.width == 0) continue;
                        // b.w/b.h keep the 1x logical size the button lays out in.
                        b.frames[i] = tak::art::makeTexture(ren_, f.rgba, f.width, f.height);
                        b.w = f.width;
                        b.h = f.height;
                    }
                    if (b.frames[0]) orderBtns_.push_back(b);
                }
            } catch (const std::exception&) {}
        };
        grab("actionbuttons", "MoveButton", 'm', "MOVE");
        grab("actionbuttons", "AttackButton", 'a', "ATTACK");
        grab("actionbuttons", "PatrolButton", 'p', "PATROL");
        grab("actionbuttons", "GuardButton", 'g', "GUARD");
        // Fight-move (Keys.TDF LOWER_F) reuses the Attack glyph, tinted.
        grab("actionbuttons", "AttackButton", 'f', "FIGHT-MOVE");
        grab("igcommonbuttons", "StopButton", 0, "STOP", 1, 2, 3);
    }

    SDL_FRect GameView::orderBtnRect(size_t i, int winW) const {
        return {float(winW) - 54, 220 + float(i) * 52, 44, 44};
    }

    void GameView::drawOrderColumn(int winW, int winH) {
        (void)winH;
        if (orderBtns_.empty() || selection_.empty()) return;
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_FRect col{float(winW) - 60, 210, 56,
                      float(orderBtns_.size()) * 52 + 12};
        SDL_SetRenderDrawColor(ren_, 20, 18, 16, 170);
        SDL_RenderFillRectF(ren_, &col);
        SDL_SetRenderDrawColor(ren_, 120, 105, 80, 255);
        SDL_RenderDrawRectF(ren_, &col);
        for (size_t i = 0; i < orderBtns_.size(); ++i) {
            const auto& b = orderBtns_[i];
            SDL_FRect r = orderBtnRect(i, winW);
            bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y &&
                       mouseY_ <= r.y + r.h;
            bool armed = b.cmd && pendingCmd_ == b.cmd;
            SDL_Texture* t = armed && b.frames[2] ? b.frames[2]
                             : hot && b.frames[1] ? b.frames[1]
                                                  : b.frames[0];
            // Fight-move reuses the Attack glyph -- tint it so it reads apart from Attack
            // (matches the fight-move mouse cursor). Reset after, as the glyph may be shared.
            if (b.cmd == 'f')
                SDL_SetTextureColorMod(t, kFightMoveTint.r, kFightMoveTint.g, kFightMoveTint.b);
            SDL_RenderCopyF(ren_, t, nullptr, &r);
            if (b.cmd == 'f') SDL_SetTextureColorMod(t, 255, 255, 255);
            if (armed) {
                SDL_SetRenderDrawColor(ren_, 255, 220, 90, 255);
                SDL_RenderDrawRectF(ren_, &r);
            }
            if (hot) {
                float px = 1.8f;
                float tw = blockWidth(b.label, px);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                SDL_FRect tb{r.x - tw - 14, r.y + 14, tw + 10, 22};
                SDL_RenderFillRectF(ren_, &tb);
                blockText(b.label, r.x - tw - 9, r.y + 18, px, {235, 225, 180, 255});
            }
        }
        drawWeaponButtons(winW, winH);
    }

    const UnitR* GameView::multiWeaponSel() {
        if (selection_.empty()) return nullptr;
        const auto* u = frameUnitP(selection_.front());
        if (u && u->alive() && u->type && u->type->weapons.size() > 1) return u;
        return nullptr;
    }

    void GameView::drawWeaponButtons(int winW, int winH) {
        weaponRects_.clear();
        const auto* u = multiWeaponSel();
        if (!u) return;
        int n = int(u->type->weapons.size());
        const float bw = 26, gap = 4;
        // A row just above the HUD bar, right-aligned to the order column's right
        // edge (the row can be wider than the 56px column, so don't centre it or it
        // runs off the screen edge).
        float y = float(winH) - barH() - bw - 8;
        float row = n * bw + (n - 1) * gap;
        float x0 = float(winW) - 4 - row;
        for (int i = 0; i < n; ++i) {
            SDL_FRect r{x0 + i * (bw + gap), y, bw, bw};
            weaponRects_.push_back(r);
            bool active = u->weaponSlot == i;
            bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y &&
                       mouseY_ <= r.y + r.h;
            SDL_SetRenderDrawColor(ren_, active ? 90 : 34, active ? 70 : 30,
                                   active ? 30 : 26, 235);
            SDL_RenderFillRectF(ren_, &r);
            SDL_SetRenderDrawColor(ren_, active ? 255 : (hot ? 200 : 120),
                                   active ? 220 : (hot ? 180 : 105),
                                   active ? 90 : 80, 255);
            SDL_RenderDrawRectF(ren_, &r);
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "%d", i + 1);
            blockText(lbl, r.x + 8, r.y + 6, 2.0f,
                      active ? SDL_Color{255, 240, 180, 255} : SDL_Color{205, 195, 165, 255});
            if (hot && !u->type->weapons[size_t(i)].name.empty()) {
                const std::string& nm = u->type->weapons[size_t(i)].name;
                float px = 1.6f, tw = blockWidth(nm.c_str(), px);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                SDL_FRect tb{r.x - tw - 14, r.y + 4, tw + 10, 20};
                SDL_RenderFillRectF(ren_, &tb);
                blockText(nm.c_str(), r.x - tw - 9, r.y + 7, px, {235, 225, 180, 255});
            }
        }
    }

    void GameView::selectWeapon(int slot) {
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (!u || !u->type || int(u->type->weapons.size()) <= slot) continue;
            tak::net::Command c;
            c.kind = tak::net::Cmd::SetWeapon;
            c.unitId = id;
            c.targetId = slot;
            issue(c);
        }
    }

    bool GameView::weaponButtonClick(float mx, float my) {
        for (size_t i = 0; i < weaponRects_.size(); ++i) {
            const auto& r = weaponRects_[i];
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            selectWeapon(int(i));
            return true;
        }
        return false;
    }

    int GameView::guiIdx(const char* name) const {
        for (size_t i = 0; i < gui_.gadgets.size(); ++i)
            if (gui_.gadgets[i].name == name) return int(i);
        return -1;
    }

    int GameView::guiIdxLeft(const char* name) const {
        int best = -1;
        for (size_t i = 0; i < gui_.gadgets.size(); ++i)
            if (gui_.gadgets[i].name == name &&
                (best < 0 || gui_.gadgets[i].x < gui_.gadgets[size_t(best)].x))
                best = int(i);
        return best;
    }

    int GameView::guiIdxRight(const char* name) const {
        int best = -1;
        for (size_t i = 0; i < gui_.gadgets.size(); ++i)
            if (gui_.gadgets[i].name == name &&
                (best < 0 || gui_.gadgets[i].x > gui_.gadgets[size_t(best)].x))
                best = int(i);
        return best;
    }

    std::vector<std::pair<int, char>> GameView::guiActiveButtons() const {
        std::vector<std::pair<int, char>> out;
        if (gui_.gadgets.empty() || selection_.empty()) return out;
        const UnitR* front = frameUnitP(selection_.front());
        if (!front || !front->type) return out;
        auto add = [&](const char* nm, char c) {
            int i = guiIdx(nm);
            if (i >= 0) out.push_back({i, c});
        };
        bool mobile = front->type->maxVel > tak::sim::Fixed();        // inverse of isStructure()
        bool armed = !front->type->weapons.empty();
        const bool rally = front->type->producesUnits();
        // The five order slots sit at fixed .gui positions that nothing else uses, so
        // retail keeps them on screen and swaps in the Disabled face when the order
        // doesn't apply (Gadget slot 0). Emitting them as UPPERCASE marks them
        // unavailable: the draw picks the disabled art and they stay unclickable.
        // The context slots below OVERLAP each other (HEAL/LOAD share one rect, CLEAR/
        // UNLOAD another, cloak/power a third), so those must stay hidden, never
        // blanked -- retail hides those the same way.
        add("MOVE", (mobile || rally) ? 'm' : 'M');
        add("PATROL", (mobile || rally) ? 'p' : 'P');
        add("GUARD", mobile ? 'g' : 'G');
        add("ATTACK", armed ? 'a' : 'A');
        add("STOP", 's');
        // Context orders: reclaim (a mobile reclaiming builder), and transport
        // load/unload. CLEAR and UNLOAD share a .gui slot (599,213) but a unit is
        // never both a reclaimer and a transport, so only one shows.
        bool builder = false, reclaimer = false;
        for (int id : selection_) {
            const auto* u = frameUnitP(id);
            if (!u || !u->alive() || !u->type || !u->type->isBuilder ||
                !u->type->canMove || u->player != localPlayer_)
                continue;
            builder = true;
            if (u->type->canReclaim) reclaimer = true;
        }
        if (builder) add("HEAL", 'r');       // repair a damaged friendly
        if (reclaimer) add("CLEAR", 'c');
        if (front->type->canTransport) {
            if (int(front->cargo.size()) < front->type->transportCap) add("LOAD", 'l');
            // Keep the command available to queue a destination during pickup.
            add("UNLOAD", 'u');
        }
        // Combat stance radio: offensive/defensive/passive. Gated on the FBI's
        // unitstandorders, which is what retail's panel consults -- not on a
        // mobile-and-armed guess that only happens to agree on the shipped data.
        if (mobile && armed && front->type->canSetStance) {
            add("Offensive", 'O');
            add("Defensive", 'D');
            add("Passive", 'H');
        }
        // Cloak toggle (cloakers) OR power on/off (onOffable) -- they share the 303
        // row, and a unit has at most one of the two capabilities.
        if (front->type->canCloak) {
            add("Uncloaked", 'k');
            add("Cloaked", 'K');
        } else if (front->type->onOffable) {
            add("Inactive", 'F');
            add("Active", 'N');
        }
        int nw = int(front->type->weapons.size());
        if (nw > 1) {
            add("PrimaryWeapon", '1');
            add("SecondaryWeapon", '2');
            if (nw > 2) add("SpecialWeapon", '3');
        }
        return out;
    }

    void GameView::renderGui(int winW, int winH) {
        if (gui_.gadgets.empty()) { drawOrderColumn(winW, winH); return; }
        guiBtnRects_.clear();
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

        // Command panel background. ButtonPanel frame 0 is the idle dragon medallion;
        // frame 1 is the button-slot panel shown while a unit is selected (retail swaps
        // the medallion out for the order grid).
        int mi = guiIdx("UnitMenu");
        if (mi >= 0 && !guiTex_[mi].empty()) {
            int pf = !selection_.empty() && guiTex_[mi].size() > 1 && guiTex_[mi][1] ? 1 : 0;
            SDL_Texture* pt = guiTex_[mi][size_t(pf)] ? guiTex_[mi][size_t(pf)]
                                                      : guiTex_[mi][0];
            if (pt) {
                SDL_FRect r = guiCmdRect(gui_.gadgets[mi]);
                SDL_RenderCopyF(ren_, pt, nullptr, &r);
            }
        }

        const UnitR* selFront =
            !selection_.empty() ? frameUnitP(selection_.front()) : nullptr;
        for (auto [idx, cmd] : guiActiveButtons()) {
            const auto& g = gui_.gadgets[idx];
            auto& tex = guiTex_[idx];
            SDL_FRect r = guiCmdRect(g);
            // A disabled slot draws but never takes a click (uppercase = unavailable).
            if (!(cmd == 'M' || cmd == 'P' || cmd == 'G' || cmd == 'A'))
                guiBtnRects_.push_back({r, cmd});
            bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y &&
                       mouseY_ <= r.y + r.h;
            bool active = false;
            if (cmd >= '1' && cmd <= '3')
                active = selFront && selFront->weaponSlot == (cmd - '1');
            else if (cmd == 'O') active = selFront && selFront->stance == 0;
            else if (cmd == 'D') active = selFront && selFront->stance == 1;
            else if (cmd == 'H') active = selFront && selFront->stance == 2;
            else if (cmd == 'K') active = selFront && selFront->cloakOn;
            else if (cmd == 'k') active = selFront && !selFront->cloakOn;
            else if (cmd == 'N') active = selFront && selFront->active;
            else if (cmd == 'F') active = selFront && !selFront->active;
            else if (cmd != 's')
                active = pendingCmd_ == cmd;
            // Retail's Gadget image slots are NOT (normal, hover, grey). Its per-class
            // slot-name table reads {"Disabled", "Pressed", "Unpressed"} for both the
            // Button and RadioButton classes, so:
            //   slot 0 = Disabled (the empty socket), 1 = Pressed, 2 = Unpressed (idle).
            // Button's constructor sets state 2, mouse-down sets 1 and mouse-up sets 2
            // back. We had 1 and 2 the other way round for push buttons, so every order
            // button sat permanently in its pushed-in face and "lit up" to its normal
            // one on hover. Toggles were already right: a selected radio shows Pressed.
            //
            // There is deliberately no toggle/push branch below. A toggle wants Pressed
            // while it is the SELECTED option and a push button wants it while ARMED,
            // and `active` already means exactly that for each -- so the list of toggle
            // letters that used to sit here had no reader and is gone.
            // An UPPERCASE order letter is the same order, marked unavailable for this
            // selection: draw the Disabled face and stay unclickable.
            bool disabled = cmd == 'M' || cmd == 'P' || cmd == 'G' || cmd == 'A';
            auto tx = [&](int i) -> SDL_Texture* {
                return i >= 0 && i < int(tex.size()) ? tex[size_t(i)] : nullptr;
            };
            // Weapon slots composite the weapon's own icon (anims/weaponpic) -- the
            // WPrimaryButton GAF is just an empty recess. The pic already includes the
            // frame, so it replaces the slot art.
            if (cmd >= '1' && cmd <= '3' && selFront) {
                int slot = cmd - '1';
                SDL_Texture* wt = slot < int(selFront->type->weapons.size())
                    ? weaponIcon(selFront->type->weapons[size_t(slot)].name, active || hot)
                    : nullptr;
                if (wt) SDL_RenderCopyF(ren_, wt, nullptr, &r);
                else {
                    char n[2] = {cmd, 0};
                    float px = std::max(1.4f, r.h / 18.0f), tw = blockWidth(n, px);
                    blockText(n, r.x + (r.w - tw) * 0.5f, r.y + r.h * 0.28f, px,
                              {200, 190, 160, 255});
                }
            } else {
                // A push button is Pressed while it is armed (waiting for you to
                // click a target) or during the click flash; a toggle is Pressed while
                // it is the selected option. Hover no longer changes the face -- it
                // only raises the tooltip, as in retail.
                bool held = active ||
                            (cmd == guiPressed_ && SDL_GetTicks() - guiPressedMs_ < 120);
                int slot = disabled ? 0 : (held ? 1 : 2);
                SDL_Texture* t = tx(slot);
                if (!t) t = tx(held ? 2 : 1);   // art missing that face: use the other
                if (!t) t = tx(0);
                if (t) SDL_RenderCopyF(ren_, t, nullptr, &r);
                // Armed with no distinct pressed face: fall back to the gold outline.
                if (active && (!t || slot != 1)) {
                    SDL_SetRenderDrawColor(ren_, 255, 220, 90, 255);
                    SDL_RenderDrawRectF(ren_, &r);
                }
            }
            if (hot && !g.cmd.empty()) {
                float px = 1.6f, tw = blockWidth(g.cmd.c_str(), px);
                SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                SDL_FRect tb{r.x - tw - 14, r.y + r.h * 0.3f, tw + 10, 20};
                SDL_RenderFillRectF(ren_, &tb);
                blockText(g.cmd.c_str(), r.x - tw - 9, r.y + r.h * 0.3f + 3, px,
                          {235, 225, 180, 255});
            }
        }

        // Mana panel at the command-panel foot: the orb is a MANA BULB (its 24 frames
        // are liquid-fill levels, picked by mana fraction); "MANA X/Y" sits in a box
        // above it, with +income to the orb's left and -expenditure to its right.
        int cb = guiIdx("CrystalBall");
        if (cb >= 0) {
            const PlayerR& tm = framePlayer(localPlayer_);
            float cap = tm.storage;
            SDL_FRect orb = guiCmdRect(gui_.gadgets[cb]);
            if (!guiTex_[cb].empty()) {
                int nf = int(guiTex_[cb].size());
                int fr = tm.manaBulbFrame(nf);
                if (guiTex_[cb][size_t(fr)])
                    SDL_RenderCopyF(ren_, guiTex_[cb][size_t(fr)], nullptr, &orb);
            }
            // "MANA" over "X/Y", both centred (H and V) in the panel's black HelpText
            // recess above the orb.
            int pmi = guiIdx("UnitMenu"), hti = guiIdx("HelpText");
            SDL_FRect panel = pmi >= 0 ? guiCmdRect(gui_.gadgets[pmi]) : orb;
            SDL_FRect mbox = hti >= 0 ? guiCmdRect(gui_.gadgets[hti])
                                      : SDL_FRect{panel.x + 6, orb.y - 60, panel.w - 12, 52};
            SDL_SetRenderDrawColor(ren_, 8, 8, 8, 235);
            SDL_RenderFillRectF(ren_, &mbox);
            SDL_SetRenderDrawColor(ren_, 70, 62, 44, 255);
            SDL_RenderDrawRectF(ren_, &mbox);
            char nums[32];
            std::snprintf(nums, sizeof nums, "%d/%d", int(tm.mana), int(cap));
            float px = std::max(1.4f, mbox.h / 20.0f);
            float gap = 3, lineH = 7 * px;
            // Shrink to fit both lines within the recess (H and V).
            while (px > 1.0f && (2 * lineH + gap > mbox.h - 4 ||
                                 blockWidth(nums, px) > mbox.w - 6)) {
                px -= 0.1f; lineH = 7 * px;
            }
            float y0 = mbox.y + (mbox.h - (2 * lineH + gap)) * 0.5f;
            SDL_Color mc{200, 215, 255, 255};
            float w1 = blockWidth("MANA", px), w2 = blockWidth(nums, px);
            blockText("MANA", mbox.x + (mbox.w - w1) * 0.5f, y0, px, mc);
            blockText(nums, mbox.x + (mbox.w - w2) * 0.5f, y0 + lineH + gap, px, mc);
            // Both rates come from the same resource history as the simulation.
            float ipx = std::max(1.5f, orb.h / 24.0f);
            char inb[16], outb[16];
            std::snprintf(inb, sizeof inb, "+%d", int(tm.income + 0.5f));
            std::snprintf(outb, sizeof outb, "-%d", int(tm.expenditure + 0.5f));
            float iy = orb.y + orb.h * 0.5f - 3.5f * ipx;
            blockText(inb, orb.x - blockWidth(inb, ipx) - 5, iy, ipx, {150, 225, 150, 255});
            blockText(outb, orb.x + orb.w + 5, iy, ipx, {230, 160, 150, 255});
        }
    }

    bool GameView::guiClick(float mx, float my) {
        for (auto& [r, cmd] : guiBtnRects_) {
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            playClickTone();
            // Retail pushes the button in on mouse-DOWN and releases it on UP, firing
            // the command there. We dispatch on down (changing that would also change
            // how dragging off a button cancels it), so latch a short press flash
            // instead -- the same visible feedback without moving the dispatch.
            guiPressed_ = cmd;
            guiPressedMs_ = SDL_GetTicks();
            if (cmd == 's') {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
            } else if (cmd >= '1' && cmd <= '3') {
                selectWeapon(cmd - '1');
            } else if (cmd == 'O' || cmd == 'D' || cmd == 'H') {
                int st = cmd == 'O' ? 0 : cmd == 'D' ? 1 : 2;
                issuePerUnit(tak::net::Cmd::Stance, st);
            } else if (cmd == 'K' || cmd == 'k') {
                issuePerUnit(tak::net::Cmd::Cloak, cmd == 'K' ? 1 : 0);
            } else if (cmd == 'N' || cmd == 'F') {
                issuePerUnit(tak::net::Cmd::SetActive, cmd == 'N' ? 1 : 0);
            } else {
                pendingCmd_ = cmd;
            }
            return true;
        }
        return false;
    }

    std::vector<std::string> GameView::conjureMenu(const std::string& builderType) const {
        const auto& all = registry_.buildable(builderType);
        std::vector<std::string> out;
        for (const auto& id : all)
            if (worldTables().buildAllowed(registry_.find(id)) &&
                (missionAllowed_.empty() || std::find(missionAllowed_.begin(), missionAllowed_.end(), id) != missionAllowed_.end()))
                out.push_back(id);
        return out;
    }

    void GameView::drawGauge(const char* name, float frac, SDL_Color c) {
        int gi = guiIdxLeft(name);
        if (gi < 0) return;
        SDL_FRect r = guiBarRect(gui_.gadgets[gi]);
        if (r.h < 5) { r.y -= (5 - r.h); r.h = 5; }   // the retail gauge is 2px -- lift for legibility
        frac = std::clamp(frac, 0.0f, 1.0f);
        SDL_SetRenderDrawColor(ren_, 8, 8, 8, 230);
        SDL_RenderFillRectF(ren_, &r);
        SDL_FRect f{r.x, r.y, r.w * frac, r.h};
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &f);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 200);
        SDL_RenderDrawRectF(ren_, &r);
    }

    bool GameView::drawGuiInfoBar(int winW, int winH) {
        if (gui_.gadgets.empty()) return false;
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float barTop = float(winH - barH());
        // Repeat the InfoPanel at its original aspect ratio, scaled to the bar
        // height. The renderer clips the last tile at the window's right edge.
        int bi = guiIdx("BottomBar");
        SDL_Texture* chrome = bi >= 0 && !guiTex_[bi].empty() ? guiTex_[bi][0] : nullptr;
        int chromeW = 0, chromeH = 0;
        if (chrome && SDL_QueryTexture(chrome, nullptr, nullptr, &chromeW, &chromeH) == 0 &&
            chromeW > 0 && chromeH > 0 && barH() > 0) {
            const float tileW = float(chromeW) * float(barH()) / float(chromeH);
            for (float x = 0; x < float(winW); x += tileW) {
                SDL_FRect r{x, barTop, tileW, float(barH())};
                SDL_RenderCopyF(ren_, chrome, nullptr, &r);
            }
        } else {
            SDL_SetRenderDrawColor(ren_, 42, 38, 34, 255);
            SDL_FRect bar{0, barTop, float(winW), float(barH())};
            SDL_RenderFillRectF(ren_, &bar);
        }
        int ei = guiIdx("BottomEnd");
        if (ei >= 0 && !guiTex_[ei].empty() && guiTex_[ei][0]) {
            SDL_FRect r = guiBarRect(gui_.gadgets[ei]);
            SDL_RenderCopyF(ren_, guiTex_[ei][0], nullptr, &r);
        }
        // Unit info as a FIXED group centred in the bar (like retail): UnitInfo1
        // (selected unit: portrait + name + HP/mana bars), ActionText (status), and
        // UnitInfo2 (the conjured target: name + progress bar). Every element keeps its
        // .gui spot so the layout never shifts, but -- unlike retail, which always shows
        // both blocks -- only what carries data draws: no portrait box with nothing
        // selected, no mana bar without a pool, no target block without a target.
        {
            float vs = float(barH()) / 49.0f;
            float groupW = (512.0f - 59.0f) * vs;   // UnitInfo1.x .. UnitInfo2 right edge
            float off = (float(winW) - groupW) * 0.5f - 59.0f * vs;
            auto place = [&](int gi) {
                SDL_FRect r = guiBarRect(gui_.gadgets[gi]);
                r.x += off;
                return r;
            };
            auto bar = [&](int gi, float frac, SDL_Color c) {
                if (gi < 0) return;
                SDL_FRect r = place(gi);
                if (r.h < 5) { r.y -= (5 - r.h) * 0.5f; r.h = 5; }
                drawBar(r.x, r.y, r.w, r.h, frac, c);
            };
            const UnitR* u =
                selection_.empty() ? nullptr : frameUnitP(selection_.front());
            if (u && (!u->alive() || !u->type)) u = nullptr;
            float tpx = std::max(2.0f, float(barH()) / 24.0f);
            // A multi-selection shows the GROUP, not its front unit: no unit name (the
            // squad's label instead, when the selection is one), HP / mana totals, and
            // none of the front unit's own extras (veterancy chevron, conjure target).
            const bool multi = u && selection_.size() > 1;

            // The conjured target (site under construction, or the head of a build queue).
            std::string tName; float tProg = 0;
            if (u && !multi) {
                if (u->buildSiteId) {
                    if (const auto* s = frameUnitP(u->buildSiteId); s && s->type) {
                        tName = s->type->name;
                        tProg = s->hp / std::max(1.0f, float(s->type->maxHp));
                    }
                } else if (!u->buildQueue.empty() && u->buildQueue.front()) {
                    // The unit being produced shows its own HP, as for a site above (and
                    // as the bar over that unit does). buildProgress/buildTime ignored
                    // the producer's workertime, and retail-resource production does
                    // not advance buildProgress at all.
                    tName = u->buildQueue.front()->name;
                    if (const auto* s = u->productionSiteId ? frameUnitP(u->productionSiteId) : nullptr;
                        s && s->type)
                        tProg = s->hp / std::max(1.0f, float(s->type->maxHp));
                }
            }

            // --- UnitInfo1: selected unit ---
            int ii = guiIdx("UnitImage");
            if (ii >= 0 && u && !multi) {
                SDL_FRect pr = place(ii);
                SDL_Texture* ic = iconFor(u->type->id);
                if (!ic) ic = modelIconTex(u->type->id, colorSlot_[localPlayer_ & 7],
                                           u->type->maxVel > tak::sim::Fixed());
                if (ic) {   // no image at all: no bare box either
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
                    SDL_RenderFillRectF(ren_, &pr);
                    SDL_RenderCopyF(ren_, ic, nullptr, &pr);
                    SDL_SetRenderDrawColor(ren_, 96, 84, 60, 255);
                    SDL_RenderDrawRectF(ren_, &pr);
                }
                if (u->veteran > 0) {
                    int xi = guiIdxLeft("Experience");
                    int tier = u->veteran >= 7 ? 2 : u->veteran >= 4 ? 1 : 0;
                    if (xi >= 0 && tier < int(guiTex_[xi].size()) && guiTex_[xi][size_t(tier)]) {
                        SDL_FRect cr{pr.x + 1, pr.y + pr.h - 22 * vs - 1, 11 * vs, 22 * vs};
                        SDL_RenderCopyF(ren_, guiTex_[xi][size_t(tier)], nullptr, &cr);
                    }
                }
            } else if (ii >= 0 && multi) {
                // A multi-selection: one portrait per type, the front unit's anchoring
                // the strip at this spot and the rest running LEFT over the chrome and
                // the EndCap scroll out to the screen's left edge. All full size until
                // a full-size strip would pass that edge -- only then do they ALL
                // (the front one too) shrink, fold, or cap with "+K".
                const SDL_FRect pr = place(ii);
                const float gap = 3 * vs, margin = 4 * vs;
                const float room = tak::selportraits::room(0, pr.x, margin);
                drawSelPortraits(pr, room, gap, 1.5f * vs);
            }
            // The name row: the unit's name (in its own capitals under MODERN) or the
            // squad label, shrunk to stop short of the conjured target's name to its right
            // and, with any descenders it has, of the health bar below it.
            auto nameText = [&](int slot, const std::string& text, int rightSlot, int barSlot) {
                const SDL_FRect nr = place(slot);
                float px = tpx;
                if (rightSlot >= 0)
                    px = std::min(px, tpx * (place(rightSlot).x - nr.x - 3 * tpx) / std::max(1.0f, blockWidth(text, tpx)));
                // Its own descenders only (a "g" or "y" in the name; caps-only names keep
                // the full row), a UI-scaled pixel clear of the bar.
                if (barSlot >= 0)
                    px = std::min(px, tpx * (place(barSlot).y - std::max(1.0f, vs) - nr.y)
                                          / (7 * tpx + tak::uiTextInkDescent(text, tpx)));
                blockText(text, nr.x, nr.y, std::max(1.0f, px), {236, 226, 192, 255});
            };
            if (int t1 = guiIdxLeft("UnitText"); u && !multi && t1 >= 0)
                nameText(t1, u->displayName(), guiIdxRight("UnitText"), guiIdxLeft("HealthBar"));
            else if (const std::string sq = multi ? selSquadLabel() : std::string(); !sq.empty() && t1 >= 0)
                nameText(t1, sq, guiIdxRight("UnitText"), guiIdxLeft("HealthBar"));
            // HP with anything selected; mana only for a pool (the group's casters).
            if (multi) {
                const auto t = selTotals();
                bar(guiIdxLeft("HealthBar"), t.hpFrac(), {210, 70, 60, 255});
                if (t.hasMana) bar(guiIdxLeft("ManaBar"), t.manaFrac(), {90, 150, 255, 255});
            } else if (u) {
                bar(guiIdxLeft("HealthBar"), u->hp / std::max(1.0f, float(u->type->maxHp)),
                    {210, 70, 60, 255});
                if (u->type->maxMana > 0)
                    bar(guiIdxLeft("ManaBar"), u->mana / u->type->maxMana, {90, 150, 255, 255});
            }

            // --- ActionText: status (or the total for a multi-selection) ---
            // Not at the .gui's ActionText spot: that sits on the name row, where a long
            // name ("BEAST HANDLER") runs into it. Centred in the gap between the two
            // HP / mana bar pairs' SPOTS instead (drawn or not, so it never moves),
            // shrunk only if it would not fit there.
            if (int ai = guiIdx("ActionText"); u && ai >= 0) {
                SDL_FRect ar = place(ai);
                std::string st = multi
                    ? std::to_string(selection_.size()) + " SELECTED"
                    : std::string(unitStatusText(u));
                float px = tpx, x = ar.x, y = ar.y;
                const int lh = guiIdxLeft("HealthBar"), lm = guiIdxLeft("ManaBar");
                const int rh = guiIdxRight("HealthBar");
                if (lh >= 0 && lm >= 0 && rh >= 0) {
                    const SDL_FRect a = place(lh), m = place(lm), b = place(rh);
                    const float l = std::max(a.x + a.w, m.x + m.w), gap = b.x - l;
                    if (gap > 0 && !st.empty()) {
                        px = std::max(1.0f, std::min(tpx, tpx * (gap - 2 * tpx) / std::max(1.0f, blockWidth(st, tpx))));
                        x = l + (gap - blockWidth(st, px)) * 0.5f;
                        y = (std::min(a.y, m.y) + std::max(a.y + a.h, m.y + m.h) - 7 * px) * 0.5f;
                    }
                }
                blockText(st, x, y, px, {170, 205, 255, 255});
            }

            // --- UnitInfo2: the conjured target, only while there is one ---
            // Name + progress; its ManaBar spot stays empty (a target has no pool to show).
            if (!tName.empty()) {
                if (int t2 = guiIdxRight("UnitText"); t2 >= 0) nameText(t2, tName, -1, guiIdxRight("HealthBar"));
                bar(guiIdxRight("HealthBar"), std::clamp(tProg, 0.0f, 1.0f), {210, 70, 60, 255});
            }
        }
        return true;
    }

    std::vector<tak::selportraits::Group> GameView::selTypeGroups() const {
        std::vector<std::pair<int, std::string>> live;
        live.reserve(selection_.size());
        for (int id : selection_)
            if (const UnitR* u = frameUnitP(id); u && u->alive() && u->type)
                live.push_back({id, u->type->id});
        return tak::selportraits::group(live);
    }

    tak::selportraits::Totals GameView::selTotals() const {
        std::vector<tak::selportraits::Vitals> v;
        v.reserve(selection_.size());
        for (int id : selection_)
            if (const UnitR* u = frameUnitP(id); u && u->alive() && u->type)
                v.push_back({u->hp, float(u->type->maxHp), u->mana, u->type->maxMana});
        return tak::selportraits::total(v);
    }

    std::string GameView::selSquadLabel() const {
        if (selection_.size() < 2) return {};
        std::vector<std::pair<int, bool>> squads;
        for (int id : selection_)
            if (const UnitR* u = frameUnitP(id); u && u->alive() && u->type)
                squads.push_back({u->player == localPlayer_ ? int(u->squad) : 0, u->type->isBuilder});
        if (squads.empty() || squads.front().first == 0) return {};   // skip the roster scan
        std::vector<int> sel(selection_);
        std::sort(sel.begin(), sel.end());
        // The local squads' unselected units. Not a conjured site: it inherits its
        // builder's squad the moment it is laid, and is no member to command yet.
        std::vector<std::pair<int, bool>> rest;
        for (const UnitR* u : front().live)
            if (u->alive() && u->player == localPlayer_ && u->type && u->squad &&
                !u->underConstruction && !std::binary_search(sel.begin(), sel.end(), u->id))
                rest.push_back({int(u->squad), u->type->isBuilder});
        return tak::selportraits::squadLabel(tak::selportraits::selectionSquad(squads, rest));
    }

    float GameView::drawSelPortraits(SDL_FRect primary, float room, float gap, float px) {
        namespace sp = tak::selportraits;
        if (selection_.size() < 2) return 0;
        const auto groups = selTypeGroups();
        if (groups.empty()) return 0;
        const sp::Layout L = sp::layout(int(groups.size()), primary.w, primary.h, gap, room,
                                        kSelPortraitMinScale);
        const sp::Rect p0{primary.x, primary.y, primary.w, primary.h};
        auto rectOf = [&](int i) {
            sp::Rect r = sp::slot(L, i, p0, gap);
            return SDL_FRect{r.x, r.y, r.w, r.h};
        };
        auto hot = [&](const SDL_FRect& r) {
            return mouseX_ >= r.x && mouseX_ <= r.x + r.w && mouseY_ >= r.y && mouseY_ <= r.y + r.h;
        };
        // A count in the portrait's bottom-right corner on a dark plate (the build
        // menu's queued-count badge), sized down with a shrunk portrait.
        auto badge = [&](const SDL_FRect& r, const std::string& num, float bpx) {
            const float nw = blockWidth(num, bpx), pad = std::max(1.0f, bpx);
            SDL_FRect nb{r.x + r.w - nw - 2 * pad - 1, r.y + r.h - 7 * bpx - 2 * pad - 1,
                         nw + 2 * pad, 7 * bpx + 2 * pad};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 205);
            SDL_RenderFillRectF(ren_, &nb);
            blockText(num, nb.x + pad, nb.y + pad, bpx, {255, 235, 140, 255});
        };
        std::string tip;
        SDL_FRect tipAt{};
        for (int i = 0; i < L.shown; ++i) {
            const auto& g = groups[size_t(i)];
            const SDL_FRect r = rectOf(i);
            const auto* t = registry_.find(g.type);
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);   // slot 0 too: one size for all
            SDL_RenderFillRectF(ren_, &r);
            SDL_Texture* ic = iconFor(g.type);
            if (!ic && t) ic = modelIconTex(g.type, colorSlot_[localPlayer_ & 7],
                                            t->maxVel > tak::sim::Fixed());
            if (ic) SDL_RenderCopyF(ren_, ic, nullptr, &r);
            const bool h = hot(r);
            SDL_SetRenderDrawColor(ren_, h ? 255 : 96, h ? 230 : 84, h ? 120 : 60, 255);
            SDL_RenderDrawRectF(ren_, &r);
            badge(r, std::to_string(g.ids.size()), std::max(1.0f, px * L.scale));
            if (h && t) { tip = t->name; tipAt = r; }
            selPortraitRects_.push_back({r, g.type});
        }
        if (L.overflow) {   // the types that did not fit: a "+K" tile, clicks swallowed
            const SDL_FRect r = rectOf(L.shown);
            const int hidden = int(groups.size()) - L.shown;
            SDL_SetRenderDrawColor(ren_, 14, 12, 10, 235);
            SDL_RenderFillRectF(ren_, &r);
            SDL_SetRenderDrawColor(ren_, 96, 84, 60, 255);
            SDL_RenderDrawRectF(ren_, &r);
            const std::string k = "+" + std::to_string(hidden);
            float kpx = std::max(1.0f, px * L.scale);
            kpx = std::min(kpx, (r.w - 4) / std::max(1.0f, blockWidth(k, 1)));
            blockText(k, r.x + (r.w - blockWidth(k, kpx)) * 0.5f, r.y + (r.h - 7 * kpx) * 0.5f,
                      kpx, {236, 226, 192, 255});
            if (hot(r)) { tip = std::to_string(hidden) + " MORE TYPES"; tipAt = r; }
            selPortraitRects_.push_back({r, std::string()});
        }
        if (!tip.empty()) {   // the hovered portrait's unit name, just above the bar
            const float tpx = std::max(1.0f, px), tw = blockWidth(tip, tpx), dh = tak::uiTextDescent(tpx);
            const float tx = std::clamp(tipAt.x + tipAt.w / 2 - tw / 2, 6.0f,
                                        std::max(6.0f, float(winW_) - tw - 6));
            const float ty = float(winH_ - barH()) - 7 * tpx - dh - 10;
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
            SDL_FRect tb{tx - 5, ty - 4, tw + 10, 7 * tpx + dh + 8};
            SDL_RenderFillRectF(ren_, &tb);
            blockText(tip, tx, ty, tpx, {255, 240, 190, 255});
        }
        return L.width;
    }

    bool GameView::selPortraitClick(float mx, float my, bool lmb) {
        if (spectating_ || selection_.size() < 2) return false;
        for (const auto& [r, type] : selPortraitRects_) {
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            if (!lmb || type.empty()) return true;   // other buttons / the "+K" tile: just swallow
            std::vector<int> ids;
            for (const auto& g : selTypeGroups())
                if (g.type == type) { ids = g.ids; break; }
            if (ids.empty()) return true;   // that type left the selection since the draw
            const bool remove = (SDL_GetModState() & KMOD_SHIFT) != 0;
            playClickTone();
            selection_ = tak::selportraits::click(selection_, ids, remove);
            if (!remove && !selection_.empty()) voice(selection_.front(), "select");
            return true;
        }
        return false;
    }

    std::string GameView::conjureTargetName(const UnitR* u) const {
        if (!u || !u->type) return {};
        if (u->buildSiteId != 0)
            if (const auto* site = frameUnitP(u->buildSiteId); site && site->type)
                return site->type->name;
        if (!u->buildQueue.empty() && u->buildQueue.front())
            return u->buildQueue.front()->name;
        return {};
    }

    void GameView::drawBar(float x, float y, float w, float h, float frac, SDL_Color c) {
        frac = std::clamp(frac, 0.0f, 1.0f);
        SDL_FRect r{x, y, w, h};
        SDL_SetRenderDrawColor(ren_, 8, 8, 8, 235);
        SDL_RenderFillRectF(ren_, &r);
        SDL_FRect f{x, y, w * frac, h};
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &f);
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 200);
        SDL_RenderDrawRectF(ren_, &r);
    }

    bool GameView::orderColumnClick(float mx, float my, int winW) {
        if (weaponButtonClick(mx, my)) return true;
        if (orderBtns_.empty() || selection_.empty()) return false;
        for (size_t i = 0; i < orderBtns_.size(); ++i) {
            SDL_FRect r = orderBtnRect(i, winW);
            if (mx < r.x || mx > r.x + r.w || my < r.y || my > r.y + r.h) continue;
            const auto& b = orderBtns_[i];
            if (b.cmd) {
                pendingCmd_ = b.cmd;
            } else {
                for (int id : selection_) {
                    tak::net::Command c;
                    c.kind = tak::net::Cmd::Stop;
                    c.unitId = id;
                    issue(c);
                }
            }
            return true;
        }
        return false;
    }

    SDL_Texture* GameView::weaponIcon(const std::string& wname, bool selected) {
        std::string base;
        for (char c : wname)
            if (c != ' ') base += char(std::tolower((unsigned char)c));
        std::string key = base + (selected ? "#s" : "#n");
        if (auto it = weaponIcons_.find(key); it != weaponIcons_.end()) return it->second;
        SDL_Texture* tex = nullptr;
        const char* suf = selected ? "sbh" : "sb";
        std::string paths[2] = {"anims/weaponpic/" + base + suf + ".jpg",
                                selected ? "anims/weaponpic/default_selected.jpg"
                                         : "anims/weaponpic/default_up.jpg"};
        for (const auto& path : paths) {
            try {
                auto img = tak::jpeg::load(vread(path));
                tex = tak::art::makeTexture(ren_, img.rgba, img.width, img.height);
                break;
            } catch (const std::exception&) {}
        }
        weaponIcons_[key] = tex;
        return tex;
    }

    void GameView::colorSwatch(float x, float y, float s, int color, std::function<void(int)> action) {
        SDL_FRect r{x, y, s, s};
        SDL_Color c = playerColors_[color % 10];
        SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
        SDL_RenderFillRectF(ren_, &r);
        SDL_SetRenderDrawColor(ren_, lbHot(r) ? 255 : 30, lbHot(r) ? 255 : 30, 30, 255);
        SDL_RenderDrawRectF(ren_, &r);
        if (action) lobbyHots_.push_back({r, std::move(action)});
    }

    void GameView::drawUnitCounts(int winW) {
        int cnt[tak::sim::kMaxPlayers] = {};
        std::string sides[tak::sim::kMaxPlayers];
        const int np = frameNumPlayers();
        for (const UnitR* up : front().live) {
            if (!up->alive() || !up->type || up->player < 0 || up->player >= np) continue;
            ++cnt[up->player];
            if (sides[up->player].empty()) sides[up->player] = up->type->side;
        }
        std::vector<int> rows;
        bool showTeams = false;
        for (int p = 0; p < np; ++p) {
            if (framePlayer(p).built == 0 && cnt[p] == 0) continue;
            for (int q : rows)
                if (framePlayer(p).team == framePlayer(q).team) showTeams = true;
            rows.push_back(p);
        }
        if (showTeams) std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
            return framePlayer(a).team < framePlayer(b).team;
        });
        // Retail's compact F4 table: white serif text, faction/colour emblems,
        // translucent player rows, and Name / Kills / Losses / Score columns.
        // Independent saved scale; shrink only when the complete table cannot fit.
        const float requestedScale = (settings_ ? settings_->scorecardScale : 1.0f) * kHudBase;
        const float scale = std::max(0.1f, std::min({requestedScale, float(winW) / (484.0f + (showTeams ? 64.0f : 0.0f)),
            float(std::max(1, winH_ - barH())) / (24.0f + 32.0f * (np + 1))}));
        const float x = 12 * scale, top = 12 * scale, rowH = 32 * scale;
        const float nameW = 190 * scale, numberW = 90 * scale;
        const float teamW = showTeams ? 64 * scale : 0;
        const float numbersX = x + nameW + teamW;
        const float width = nameW + teamW + 3 * numberW;
        float fontScale = 1, fontTop = 0, fontH = 7;
        if (scoreboardFont_.ok()) scoreboardFont_.vbounds("Ag0123456789", 1, fontTop, fontH);
        fontScale = 20 * scale / std::max(1.0f, fontH);
        auto textWidth = [&](const std::string& text, float s) {
            return scoreboardFont_.ok() ? float(scoreboardFont_.width(text, s)) : blockWidth(text, s);
        };
        auto text = [&](const std::string& value, float tx, float y, float available,
                        bool center, SDL_Color color) {
            float s = fontScale;
            float tw = textWidth(value, s);
            if (tw > available) { s *= available / tw; tw = textWidth(value, s); }
            if (center) tx += (available - tw) * 0.5f;
            const float baseline = y + (rowH - fontH * s) * 0.5f - fontTop * s;
            if (scoreboardFont_.ok()) {
                scoreboardFont_.draw(ren_, value, tx + scale, baseline + scale, s, {0,0,0,220});
                scoreboardFont_.draw(ren_, value, tx, baseline, s, color);
            } else blockText(value, tx, baseline, s, color);
        };
        const SDL_Color white{245,245,245,255};
        text("Name", x, top, nameW, false, white);
        if (showTeams) text("Team", x + nameW, top, teamW, true, white);
        text("Kills", numbersX, top, numberW, true, white);
        text("Losses", numbersX + numberW, top, numberW, true, white);
        text("Score", numbersX + 2 * numberW, top, numberW, true, white);
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        float y = top + rowH;
        for (int t : rows) {
            const auto& player = framePlayer(t);
            SDL_FRect row{x, y, width, rowH};
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 110);
            SDL_RenderFillRectF(ren_, &row);
            SDL_SetRenderDrawColor(ren_, 157, 145, 100, 155);
            SDL_RenderDrawLineF(ren_, x, y, x + width, y);
            SDL_RenderDrawLineF(ren_, x, y + rowH, x + width, y + rowH);
            std::string name = playerName_[t & 7];
            if (name.empty()) name = "P" + std::to_string(t + 1) + " " + sides[t];
            if (playerAi_[t & 7] && name.rfind("AI ", 0) != 0) name = "AI " + name;
            if (player.defeated) name += " (OUT)";
            const SDL_Color color = player.defeated ? SDL_Color{160,160,160,255} : white;
            std::string side = sides[t];
            // Keep the emblem after defeat, when no living unit supplies a side.
            if (!side.empty()) scoreboardSides_[t & 7] = side;
            else side = scoreboardSides_[t & 7];
            if (!player.side.empty()) side = player.side;
            const std::string seq = tak::factionLogoSequence(side);
            const std::string key = seq + std::to_string(colorSlot_[t & 7]);
            auto [it, inserted] = scoreboardLogos_.try_emplace(key, nullptr);
            if (inserted && !seq.empty())
                it->second = loadGuiFrame("colorlogos2", seq, colorSlot_[t & 7]);
            SDL_FRect emblem{x + 2 * scale, y + 2 * scale, 28 * scale, 28 * scale};
            if (it->second) SDL_RenderCopyF(ren_, it->second, nullptr, &emblem);
            else {
                const auto c = playerColor(t);
                SDL_SetRenderDrawColor(ren_, c.r, c.g, c.b, 255);
                SDL_RenderFillRectF(ren_, &emblem);
            }
            text(name, x + 36 * scale, y, nameW - 42 * scale, false, color);
            if (showTeams) text(std::to_string(player.team + 1), x + nameW, y, teamW, true, color);
            text(std::to_string(player.kills), numbersX, y, numberW, true, color);
            text(std::to_string(player.losses), numbersX + numberW, y, numberW, true, color);
            text(std::to_string(player.score), numbersX + 2 * numberW, y, numberW, true, color);
            y += rowH;
        }
    }

    void GameView::drawObjectivesPanel(int winW, int /*winH*/) {
        if (missionObjectives_.empty()) return;
        const float x0 = 12, top = 92;
        // Integer-sized glyphs have predictable bounds: the decorative book font
        // uses baseline offsets that made the old 21px rows overlap each other.
        const float s = 2.0f, lh = 22.0f;
        const float maxW = std::min(480.0f, winW * 0.40f);
        std::vector<std::pair<std::string, bool>> lines;
        if (showObjectives_) for (const std::string& obj : missionObjectives_) {
            std::string line, word;
            bool first = true;
            auto flush = [&] {
                if (line.empty()) return;
                lines.push_back({line, first});
                first = false;
                line.clear();
            };
            auto push = [&] {
                if (word.empty()) return;
                if (!line.empty() && blockWidth(line + " " + word, s) > maxW) flush();
                if (!line.empty()) line += ' ';
                for (char c : word) {
                    if (blockWidth(line + c, s) > maxW) flush();
                    line += c;
                }
                word.clear();
            };
            for (char c : obj) {
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                    push();
                    if (c == '\n') flush();
                } else word += c;
            }
            push();
            flush();
        }
        const float panelW = showObjectives_ ? maxW + 34 : blockWidth("[O] OBJECTIVES", s) + 20;
        const float panelH = showObjectives_ ? 52 + float(lines.size()) * lh : 30;
        SDL_FRect bg{x0 - 6, top - 6, panelW, panelH};
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren_, 14, 13, 18, 235);
        SDL_RenderFillRectF(ren_, &bg);
        SDL_SetRenderDrawColor(ren_, 96, 84, 60, 255);
        SDL_RenderDrawRectF(ren_, &bg);
        objPanelBottom_ = bg.y + bg.h;
        blockText(showObjectives_ ? "OBJECTIVES" : "[O] OBJECTIVES",
                  x0, top, s, {240, 214, 148, 255});
        if (!showObjectives_) return;
        float y = top + 26;
        for (const auto& [text, isFirst] : lines) {
            if (isFirst) {
                SDL_FRect dot{x0 + 2, y + 5, 4, 4};
                SDL_SetRenderDrawColor(ren_, 240, 214, 148, 255);
                SDL_RenderFillRectF(ren_, &dot);
            }
            blockText(text, x0 + 14, y, s, {240, 242, 248, 255});
            y += lh;
        }
        blockText("[O] HIDE", x0, y + 4, s, {190, 198, 212, 255});
    }

    void GameView::drawPanel(int winW, int winH) {
        // Bottom bar: the retail InfoPanel chrome + unit info when a .gui is loaded,
        // else our own stone strip. The build menu + mana readout below draw on top.
        selPortraitRects_.clear();
        bool guiBar = drawGuiInfoBar(winW, winH);
        SDL_FRect bar{0, float(winH - barH()), float(winW), float(barH())};
        if (!guiBar) {
            if (botTex_) {
                for (int x = 0; x < winW; x += botW_) {
                    SDL_Rect dst{x, winH - barH(), botW_, barH()};
                    SDL_RenderCopy(ren_, botTex_, nullptr, &dst);
                }
            } else if (panelTex_) {
                for (int x = 0; x < winW; x += panelW_) {
                    SDL_Rect src{0, 40, panelW_, barH()};
                    SDL_Rect dst{x, winH - barH(), panelW_, barH()};
                    SDL_RenderCopy(ren_, panelTex_, &src, &dst);
                }
            } else {
                SDL_SetRenderDrawColor(ren_, 42, 38, 34, 255);
                SDL_RenderFillRectF(ren_, &bar);
            }
            SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren_, 0, 0, 0, 90);
            SDL_RenderFillRectF(ren_, &bar);
            SDL_SetRenderDrawColor(ren_, 120, 105, 80, 255);
            SDL_RenderDrawLineF(ren_, 0, bar.y, float(winW), bar.y);
        }
        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);

        char buf[96];
        SDL_Color fc = factionColor();
        // A solid faction-coloured panel with a dark frame — black text on top.
        auto shade = [&](float x, float w) {
            SDL_FRect z{x, bar.y + 4, w, barH() - 8.0f};
            SDL_SetRenderDrawColor(ren_, fc.r, fc.g, fc.b, 255);
            SDL_RenderFillRectF(ren_, &z);
            SDL_SetRenderDrawColor(ren_, 20, 18, 16, 255);
            SDL_RenderDrawRectF(ren_, &z);
        };

        // Bottom-LEFT: portrait + stats for the selected unit. Skipped when the retail
        // InfoPanel bar already drew the unit info (drawGuiInfoBar).
        // A multi-selection's other types sit LEFT of the portrait. This block is
        // pinned to the screen's left edge, so it widens instead: the strip takes the
        // left of the panel, the portrait and stats move right past it (up to the
        // mana readout at the right), and the portrait still leads its strip.
        if (!guiBar && !selection_.empty() && statFont_.ok()) {
            const auto* u = frameUnitP(selection_.front());
            if (u && u->alive() && u->type) {
                float px = 8;
                // The strip's room runs from the panel's right edge to the mana panel
                // (winW - 200), less a gap: full size until it would reach the mana.
                const float gap = 4, room = tak::selportraits::room(px + 288, float(winW) - 200, 8);
                SDL_FRect pr{px + 4, bar.y + 8, 56, barH() - 20.0f};
                float strip = 0;
                if (selection_.size() > 1)
                    strip = tak::selportraits::layout(int(selTypeGroups().size()), pr.w, pr.h, gap,
                                                      room, kSelPortraitMinScale).width;
                pr.x += strip;
                shade(px, 288 + strip);
                SDL_Texture* ic = selection_.size() > 1 ? nullptr : iconFor(u->type->id);
                if (ic) {
                    SDL_RenderCopyF(ren_, ic, nullptr, &pr);
                    SDL_SetRenderDrawColor(ren_, 20, 18, 16, 255);
                    SDL_RenderDrawRectF(ren_, &pr);
                }
                // A multi-selection draws its front portrait in the strip, sized like the rest.
                if (selection_.size() > 1) drawSelPortraits(pr, room, gap, 1.8f);   // same layout as above
                float tx = px + 70 + strip;
                SDL_Color blk{0, 0, 0, 255};
                if (selection_.size() > 1) {   // the group: its squad label + HP totals
                    blockText(selSquadLabel(), tx, bar.y + 9, 2.6f, blk);
                    const auto t = selTotals();
                    std::snprintf(buf, sizeof buf, "HP %d/%d", int(t.hp), int(t.maxHp));
                } else {
                    blockText(u->displayName(), tx, bar.y + 9, 2.6f, blk);
                    std::snprintf(buf, sizeof buf, "HP %d/%d", int(u->hp),
                                  int(u->type->maxHp));
                }
                blockText(buf, tx, bar.y + 32, 2.3f, blk);
                if (selection_.size() > 1) {
                    std::snprintf(buf, sizeof buf, "%zu SELECTED", selection_.size());
                    blockText(buf, tx, bar.y + 52, 1.8f, blk);
                }
            }
        }

        // Conjure menu: clickable build icons for the selected builder, in a
        // horizontal row just above the info bar -- or several, stacked upward, when a
        // long menu does not fit left of the command panel (client/buildrow.h, the one
        // place the row's geometry is computed). Where the rows sit along the bottom is
        // a user preference (Options "BUILD MENU": left / centered / right).
        iconRects_.clear();
        buildMenuRect_ = {};
        buildRowTop_ = 0;
        const auto* b = selectedBuilder();
        if (b) {
            // Mission-filtered; an entry naming no known type takes no slot.
            std::vector<const tak::sim::UnitType*> menu;
            for (const auto& id : conjureMenu(b->type->id))
                if (const auto* bt = registry_.find(id)) menu.push_back(bt);
            int n = int(menu.size());
            // Row size: the bar height (already uiScale-scaled) times the user's
            // extra BUILD MENU SCALE, so the row can grow independently of the HUD.
            const auto lay = tak::buildrow::layout(n, bar.y, float(barH()), buildBarScale_,
                                                   float(winW), float(mapViewW(winW)),
                                                   buildBarAlign_);
            // Everything drawn INSIDE an icon (the +++ badge, the queue count, the
            // tooltip) is sized from the scale in effect, so a shrunken icon does not
            // get full-size furniture spilling out of it.
            const float bScale = lay.scale;
            const float iconTop = lay.top;   // the top row: the tooltip and TRAINING sit over it
            if (n > 0) {
                buildMenuRect_ = {lay.box.x, lay.box.y, lay.box.w, lay.box.h};
                buildRowTop_ = lay.bandTop;
            }
            // A recessed container behind the rows so the conjure menu reads as one HUD
            // strip (matching the InfoPanel bar's dark inset + bronze frame).
            if (n > 0 && guiBar) {
                const SDL_FRect& box = buildMenuRect_;
                SDL_SetRenderDrawColor(ren_, 14, 12, 10, 225);
                SDL_RenderFillRectF(ren_, &box);
                SDL_SetRenderDrawColor(ren_, 96, 84, 60, 255);
                SDL_RenderDrawRectF(ren_, &box);
            }
            for (int i = 0; i < n; ++i) {
                const auto* bt = menu[size_t(i)];
                const auto& li = lay.icons[size_t(i)];
                SDL_FRect r{li.x, li.y, li.w, li.h};
                SDL_SetRenderDrawColor(ren_, 20, 18, 14, 235);
                SDL_FRect rb{r.x - 1, r.y - 1, r.w + 2, r.h + 2};
                SDL_RenderFillRectF(ren_, &rb);
                SDL_Texture* ic = iconFor(bt->id);
                // isStructure(), not the canmove flag: a Barracks icon should sit
                // square like the building it is, not turned like a soldier.
                if (!ic) ic = modelIconTex(bt->id, colorSlot_[localPlayer_ & 7],
                                           !isStructure(bt));
                if (ic) {
                    // A few retail portraits differ by a pixel, and generated
                    // model thumbnails are square. Fit both without distortion.
                    int iw = 0, ih = 0;
                    if (SDL_QueryTexture(ic, nullptr, nullptr, &iw, &ih) == 0 &&
                        iw > 0 && ih > 0) {
                        float scale = std::min(r.w / iw, r.h / ih);
                        SDL_FRect imageRect{r.x + (r.w - iw * scale) / 2,
                                            r.y + (r.h - ih * scale) / 2,
                                            iw * scale, ih * scale};
                        SDL_RenderCopyF(ren_, ic, nullptr, &imageRect);
                    }
                }
                else {
                    SDL_SetRenderDrawColor(ren_, 60, 55, 50, 255);
                    SDL_RenderFillRectF(ren_, &r);
                }
                bool hot = mouseX_ >= r.x && mouseX_ <= r.x + r.w &&
                           mouseY_ >= r.y && mouseY_ <= r.y + r.h;
                SDL_SetRenderDrawColor(ren_, hot ? 255 : 110, hot ? 230 : 100,
                                       hot ? 120 : 70, 255);
                SDL_RenderDrawRectF(ren_, &r);
                // Infinite-build marker: bright +++ over the repeating unit's icon.
                if (b->repeatType == bt) {
                    float px = 2.8f * bScale;   // badges track the row scale
                    float pw = blockWidth("+++", px);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 180);
                    SDL_FRect pb{r.x + (r.w - pw) / 2 - 3, r.y + 4 * bScale,
                                 pw + 6, 22 * bScale};
                    SDL_RenderFillRectF(ren_, &pb);
                    blockText("+++", r.x + (r.w - pw) / 2, r.y + 7 * bScale, px,
                              {120, 255, 130, 255});
                }
                // Queued-count badge (bottom-right of the icon): how many are queued.
                if (int qc = frameQueuedCount(b->id, bt)) {
                    char num[8];
                    std::snprintf(num, sizeof num, "%d", qc);
                    float px = 2.2f * bScale, nw = blockWidth(num, px);
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 205);
                    SDL_FRect nb{r.x + r.w - nw - 7 * bScale,
                                 r.y + r.h - 21 * bScale,
                                 nw + 7 * bScale, 20 * bScale};
                    SDL_RenderFillRectF(ren_, &nb);
                    blockText(num, r.x + r.w - nw - 4 * bScale,
                              r.y + r.h - 18 * bScale, px, {255, 235, 140, 255});
                }
                if (hot) {
                    char tip[80];
                    std::snprintf(tip, sizeof tip, "%s  %d MANA", bt->name.c_str(),
                                  int(bt->buildCost));
                    float px = 2.0f * bScale;
                    float tw = blockWidth(tip, px);
                    // Kept left of the command panel like the icons (a right-aligned
                    // row's last tooltip would otherwise hang over the mana bulb).
                    float tipx = std::max(6.0f, std::min(r.x + r.w / 2 - tw / 2,
                                                         float(mapViewW(winW)) - tw - 6));
                    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 210);
                    SDL_FRect tb{tipx - 6, iconTop - 28 * bScale,
                                 tw + 12, 26 * bScale};
                    SDL_RenderFillRectF(ren_, &tb);
                    blockText(tip, tipx, iconTop - 24 * bScale, px, {255, 240, 190, 255});
                }
                iconRects_.push_back({r, bt});
            }
            if (!b->buildQueue.empty()) {
                char q[64];
                std::snprintf(q, sizeof q, "TRAINING %s (%zu)",
                              b->buildQueue.front()->name.c_str(),
                              b->buildQueue.size());
                // From the top row's first icon, but never on into the command panel
                // (a short right-aligned menu leaves the line little room).
                const float qx = std::max(tak::buildrow::kEdge,
                    std::min(lay.textX, float(mapViewW(winW)) - tak::buildrow::kEdge -
                                            blockWidth(q, 1.8f * bScale)));
                blockText(q, qx, iconTop - 24 * bScale, 1.8f * bScale,
                          {160, 210, 255, 255});
            }
        }

        // Bottom-RIGHT: mana -- only on our own bar. The retail GUI bar draws the mana
        // readout at the command-panel foot around the orb (renderGui) instead.
        if (!guiBar) {
            const PlayerR& tm = framePlayer(localPlayer_);
            float manaX = float(winW) - 192;
            shade(manaX - 8, 200);
            SDL_Color txt{0, 0, 0, 255};
            blockText("MANA", manaX, bar.y + 9, 2.0f, txt);
            std::snprintf(buf, sizeof buf, "%d/%d", int(tm.mana),
                          int(tm.storage));
            blockText(buf, manaX, bar.y + 30, 2.3f, txt);
            std::snprintf(buf, sizeof buf, "+%d/SEC", int(tm.income + 0.5f));
            blockText(buf, manaX, bar.y + 52, 1.8f, txt);
        }
    }

void GameView::drawGiveUnitsMenu(int winW, int winH) {
    giveUnitsHots_.clear();
    std::vector<int> players;
    for (int p=0;p<frameNumPlayers();++p)
        if (!resultParticipants_ || (*resultParticipants_ & (1u<<p))) players.push_back(p);
    const auto eligible=eligibleGiftRecipients();
    const auto manaMask=requestedManaSharing_.value_or(framePlayer(localPlayer_).manaShareMask);
    const float scale=std::min({1.0f,float(winW)/1080.0f,float(winH)/550.0f});
    const float width=1040*scale,rowH=38*scale;
    const float height=(172+44*players.size())*scale;
    const float x=(winW-width)/2,y=(winH-height)/2;
    SDL_SetRenderDrawBlendMode(ren_,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren_,0,0,0,160);
    SDL_FRect dim{0,0,float(winW),float(winH)};SDL_RenderFillRectF(ren_,&dim);
    SDL_FRect panel{x,y,width,height};
    SDL_SetRenderDrawColor(ren_,26,28,36,245);SDL_RenderFillRectF(ren_,&panel);
    SDL_SetRenderDrawColor(ren_,120,130,160,255);SDL_RenderDrawRectF(ren_,&panel);
    blockText("DIPLOMACY",x+24*scale,y+22*scale,2.5f*scale,{235,225,180,255});
    blockText("PLAYER",x+24*scale,y+66*scale,1.6f*scale,{190,190,200,255});
    blockText("SHARE MANA",x+252*scale,y+66*scale,1.6f*scale,{190,190,200,255});
    blockText("CHAT",x+394*scale,y+66*scale,1.6f*scale,{190,190,200,255});
    blockText("VOICE",x+456*scale,y+66*scale,1.6f*scale,{190,190,200,255});
    blockText("VOLUME",x+524*scale,y+66*scale,1.6f*scale,{190,190,200,255});
    auto button=[&](const std::string& label,SDL_FRect r,int p,DiplomacyAction action,bool enabled) {
        const bool hover=enabled && mouseX_>=r.x && mouseX_<r.x+r.w && mouseY_>=r.y && mouseY_<r.y+r.h;
        SDL_SetRenderDrawColor(ren_,enabled?(hover?90:60):37,enabled?(hover?110:66):39,enabled?(hover?150:86):46,255);
        SDL_RenderFillRectF(ren_,&r);
        blockText(label,r.x+(r.w-blockWidth(label,1.7f*scale))/2,
            r.y+(r.h-7*1.7f*scale)/2,1.7f*scale,enabled?SDL_Color{235,235,240,255}:SDL_Color{105,108,118,255});
        if(enabled)giveUnitsHots_.push_back({r,p,action});
    };
    auto checkbox=[&](float cx,float by,int p,DiplomacyAction action,bool checked,bool enabled) {
        SDL_FRect r{x+cx*scale,by+8*scale,22*scale,22*scale};
        SDL_SetRenderDrawColor(ren_,enabled?165:65,enabled?175:68,enabled?190:75,255);
        SDL_RenderDrawRectF(ren_,&r);
        if(checked) {
            SDL_SetRenderDrawColor(ren_,enabled?225:85,enabled?220:88,enabled?160:95,255);
            for(int i=0;i<2;++i) {
                SDL_RenderDrawLineF(ren_,r.x+4*scale,r.y+(11+i)*scale,r.x+9*scale,r.y+(16+i)*scale);
                SDL_RenderDrawLineF(ren_,r.x+9*scale,r.y+(16+i)*scale,r.x+18*scale,r.y+(5+i)*scale);
            }
        }
        if(enabled)giveUnitsHots_.push_back({r,p,action});
    };
    // VOLUME: how loud that player's voice plays here, drawn as the VOICE CHAT page's
    // sliders are (track, fill, handle, value) and greyed like that page while voice chat
    // is off. A greyed slider shows its value but takes no input.
    const bool voiceOn=voice_ && voice_->enabled();
    auto volumeSlider=[&](float by,int p) {
        const float tx=x+524*scale,tw=140*scale,th=7*scale,ty=by+(rowH-th)/2;
        const float frac=float(voiceVolume_[size_t(p)])/100.0f;
        SDL_FRect track{tx,ty,tw,th};
        SDL_SetRenderDrawColor(ren_,48,52,66,255);SDL_RenderFillRectF(ren_,&track);
        SDL_FRect fill{tx,ty,tw*frac,th};
        if(voiceOn)SDL_SetRenderDrawColor(ren_,80,140,225,255);
        else SDL_SetRenderDrawColor(ren_,70,76,92,255);
        SDL_RenderFillRectF(ren_,&fill);
        SDL_FRect handle{tx+tw*frac-4*scale,ty-4*scale,8*scale,th+8*scale};
        if(voiceOn)SDL_SetRenderDrawColor(ren_,205,215,240,255);
        else SDL_SetRenderDrawColor(ren_,95,100,115,255);
        SDL_RenderFillRectF(ren_,&handle);
        const std::string pct=std::to_string(int(voiceVolume_[size_t(p)]))+"%";
        blockText(pct,tx+tw+14*scale,by+(rowH-7*1.6f*scale)/2,1.6f*scale,
            voiceOn?SDL_Color{150,195,235,255}:SDL_Color{90,100,115,255});
        // The hot area is the row's height over the track: a value is where along it.
        if(voiceOn)giveUnitsHots_.push_back({{tx,by,tw,rowH},p,DiplomacyAction::Volume});
    };
    float by=y+92*scale;
    for(int p:players) {
        std::string name=playerName_[p];
        if(name.empty())name="PLAYER "+std::to_string(p+1);
        if(p==localPlayer_)name+=" (YOU)";
        const float ts=std::min(1.9f*scale,210*scale/std::max(1.0f,blockWidth(name,1)));
        blockText(name,x+24*scale,by+(rowH-7*ts)/2,ts,{235,235,240,255});
        const bool ally=p!=localPlayer_ && framePlayer(p).team==framePlayer(localPlayer_).team;
        const bool sharing=ally && !framePlayer(p).defeated && !framePlayer(localPlayer_).defeated;
        if(ally)checkbox(294,by,p,DiplomacyAction::Mana,manaMask & (1u<<p),sharing);
        checkbox(402,by,p,DiplomacyAction::Chat,chatRecipients_ & (1u<<p),true);
        // VOICE: chat's column again, for voice chat. Your own voice never comes back to
        // you, so your own box is shown as it stands but cannot be changed.
        checkbox(468,by,p,DiplomacyAction::Voice,voiceRecipients_ & (1u<<p),p!=localPlayer_);
        if(voiceVolumeSlider(p))volumeSlider(by,p);
        if(ally)button("GIVE SELECTED UNITS",{x+738*scale,by,278*scale,rowH},p,DiplomacyAction::Give,eligible[size_t(p)]);
        by+=44*scale;
    }
    button("CLOSE",{x+width-164*scale,y+height-56*scale,140*scale,rowH},-1,DiplomacyAction::Close,true);
}

    bool GameView::unitUnderCursor(const UnitR& u, float mx, float my, float* d2) {
        if (!canPickUnit(u)) return false;
        if (dotsFrame_) {
            // Tactical Dots on screen: the hit region is the dot, with the same
            // 9px floor as a tiny model so a lone dot is still easy to click.
            SDL_FPoint c = dotCentre(u);
            float half = std::max(dotSide(u) * 0.5f + 1.0f, 9.0f);
            if (std::fabs(mx - c.x) > half || std::fabs(my - c.y) > half) return false;
            if (d2) { float dx = c.x - mx, dy = c.y - my; *d2 = dx * dx + dy * dy; }
            return true;
        }
        float zms = mapView_.zoom();
        SDL_FPoint p = unitScreen(u);
        const SDL_FRect& hb = unitHitBox(u.type);
        float ax = p.x, ay = p.y + 12.0f * zms;         // sprite draw anchor
        float cx = ax + (hb.x + hb.w * 0.5f) * zms;
        float cy = ay + (hb.y + hb.h * 0.5f) * zms;
        float hw = std::max(hb.w * zms * 0.5f, 9.0f);
        float hh = std::max(hb.h * zms * 0.5f, 9.0f);
        if (std::fabs(mx - cx) > hw || std::fabs(my - cy) > hh) return false;
        if (d2) { float dx = cx - mx, dy = cy - my; *d2 = dx * dx + dy * dy; }
        return true;
    }

    void GameView::playClickTone(int semitone) {
        constexpr bool kPitchedBongs = false;   // pending icd proof
        constexpr int kToneRef = 4;
        float rate = (kPitchedBongs && semitone > 0)
                         ? std::pow(2.0f, float(semitone - kToneRef) / 12.0f)
                         : 1.0f;
        std::string t = "tone" + side_;
        if (!sounds_.has(t)) return;
        // Peak-normalize the acknowledgment to a prominent UI level: retail's
        // DirectSound path played tones hot (full per-ear gain, no equal-power
        // pan loss, no mixing headroom); matching by amplitude alone leaves
        // both retail's tone (peak 0.31) and soft click.hpi replacements
        // (peak 0.39) buried under our /2-headroom + 0.707-pan chain. Target
        // ~0.7 per ear: gain = 0.7 / (peak * 0.5 * 0.707), capped so a
        // near-silent file can't amplify noise floor.
        float peak = sounds_.peakOf(t);
        float gain = peak > 0.01f
                         ? std::clamp(0.7f / (peak * 0.5f * 0.707f), 1.0f, 8.0f)
                         : 2.0f;
        sounds_.play(t, gain, rate);
    }

    // A chat sender's name colour: their player colour when the name is a seated
    // player's, a warm gold for this client's own notices, a neutral grey otherwise
    // (spectators). Dark player colours are lifted so a name never sinks into the
    // box's dark backing.
    SDL_Color GameView::chatSenderColor(const std::string& who, bool notice) const {
        auto same = [](const std::string& a, const std::string& b) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        };
        // Gold only for notices this client raised itself: a relayed sender name is
        // whatever the other end chose, so "SERVER" from the wire gets no special look.
        if (notice) return {255, 214, 120, 255};
        SDL_Color c{200, 205, 215, 255};
        for (int i = 0; i < 8; ++i)
            if (!playerName_[i].empty() && same(playerName_[i], who)) { c = playerColor(i); break; }
        // Too dark to read on black (the blue player is): mix toward white, keeping the hue.
        const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
        if (lum < 160) {
            const float k = std::min(0.6f, (160 - lum) / (255 - lum));
            c = {Uint8(c.r + (255 - c.r) * k), Uint8(c.g + (255 - c.g) * k), Uint8(c.b + (255 - c.b) * k), 255};
        }
        return c;
    }

    // The in-game chat box. Received lines read oldest to newest downwards, the newest
    // just above the composer; each starts with the sender's name in their colour and
    // wraps to the box width. The text is the HUD's plain Times face drawn in flat
    // colour with a drop shadow, over one translucent dark panel, so it reads over
    // bright sand and dark forest alike. Size is UI SCALE x CHAT SCALE; the box sits
    // left or centred over the play area (never under the command panel), above the
    // information bar and the build row's band with its TRAINING line, whether or not
    // a builder's row is up (chatbox.h hudFloor).
    void GameView::drawGameChat(int winW, int winH) {
        // The embedded face; the HUD font only if it ever failed to load.
        if (tak::uiFont().ok()) drawGameChatIn(tak::uiFont(), winW, winH);
        else drawGameChatIn(hudFont_, winW, winH);
    }

    template <class Face>
    void GameView::drawGameChatIn(const Face& font, int winW, int winH) {
        namespace cb = tak::chatbox;
        if (!mp_ || !font.ok() || (!chatTyping_ && gameChat_.empty())) return;

        const float areaW = float(mapViewW(winW));
        const float margin = std::round(10 * uiScale_);
        // Above the information bar AND the build row, its TRAINING line and tooltip
        // (chatbox.h hudFloor): the box never covers HUD text, at any scale.
        const float barTop = float(winH - barH());
        const float bottom = cb::hudFloor(barTop, float(barH()), buildBarScale_, buildRowTop_,
                                          !spectating_, margin);
        // The box may use 45% of the play area's height, so long as it fits above the
        // HUD; higher up it would start to cover the battle rather than annotate it.
        const float maxBoxH = std::max(0.0f, std::min(barTop * 0.45f, bottom - 2 * margin));

        float top1 = 0, h1 = 0;
        font.vbounds("Hdgjpqy", 1, top1, h1);
        h1 = std::max(h1, 1.0f);
        // Text 16 px tall at 100%, exactly as chosen; but never so big that three rows
        // (two lines and the composer) cannot fit in the room there is. That shrink stops
        // at 10 px text (or the chosen size, if smaller), below which it would be unreadable.
        const float sChosen = uiScale_ * chatScale_, sFloor = std::min(sChosen, 10.0f / 16.0f);
        float s = sChosen;
        auto rowH = [&](float sc) { return 16.0f * sc + 6.0f * sc; };
        while (s > sFloor && 3 * rowH(s) + 16 * s > maxBoxH) s = std::max(sFloor, s * 0.9f);
        const float textH = 16.0f * s;
        const float fs = textH / h1;          // font scale
        const float lineH = rowH(s), pad = std::round(8 * s), indent = std::round(14 * s);
        // The drop shadow follows the box scale (1 px at 100%, 2 px from 200%); the GAF
        // fallback keeps its font-relative one.
        const float shadow = std::is_same_v<Face, Font> ? std::max(1.0f, std::round(fs * 0.6f))
                                                        : std::max(1.0f, std::round(0.8f * s));
        const float wrapW = std::max(80.0f, std::min(areaW - 2 * margin, 760 * s) - 2 * pad);
        auto width = [&](const std::string& t) { return float(font.width(t, fs)); };

        struct Seg { std::string text; SDL_Color color; };
        struct Row { std::vector<Seg> segs; float x = 0, alpha = 1; bool composer = false; };
        std::vector<Row> rows;
        auto addMessage = [&](const std::string& head, SDL_Color headColor, const std::string& body,
                              SDL_Color bodyColor, float alpha, bool composer) {
            const float headW = width(head);
            const auto parts = cb::wrap(body, std::max(wrapW - headW, wrapW * 0.25f), wrapW - indent, width);
            for (size_t i = 0; i < parts.size(); ++i) {
                Row r;
                r.alpha = alpha;
                r.composer = composer;
                if (i == 0) {
                    r.segs.push_back({head, headColor});
                    r.x = 0;
                } else r.x = indent;
                r.segs.push_back({parts[i], bodyColor});
                rows.push_back(std::move(r));
            }
        };
        const SDL_Color bodyColor{238, 234, 222, 255};
        // While typing, the recent history is shown in full; otherwise only lines still
        // inside their hold + fade time.
        const int maxMsgs = chatTyping_ ? 8 : 6;
        std::vector<const GameChat*> msgs;
        for (auto it = gameChat_.rbegin(); it != gameChat_.rend() && int(msgs.size()) < maxMsgs; ++it)
            if (chatTyping_ || cb::lineAlpha(it->age) > 0) msgs.push_back(&*it);
        std::reverse(msgs.begin(), msgs.end());
        for (const GameChat* m : msgs) {
            const float a = chatTyping_ ? 1.0f : cb::lineAlpha(m->age);
            if (m->who.empty())   // a local system line (power codes, the poster): no name, all gold
                addMessage("", chatSenderColor(m->who, true), m->text, chatSenderColor(m->who, true), a, false);
            else
                addMessage(m->who + ": ", chatSenderColor(m->who, m->notice), m->text, bodyColor, a, false);
        }
        const size_t firstComposer = rows.size();
        if (chatTyping_) addMessage("SAY: ", {255, 214, 120, 255}, chatDraft_ + "_", {255, 248, 225, 255}, 1, true);
        if (rows.empty()) return;

        // Too tall: drop the oldest rows; then, for a draft taller than the room on its
        // own, scroll the composer so its last rows (the caret's) stay in view.
        size_t composerAt = firstComposer;
        auto sepH = [&] { return chatTyping_ && composerAt > 0 ? std::round(6 * s) : 0.0f; };
        auto boxH = [&] { return float(rows.size()) * lineH + 2 * pad - 6 * s + sepH(); };
        while (boxH() > maxBoxH && composerAt > 0) {
            rows.erase(rows.begin());
            --composerAt;
        }
        while (boxH() > maxBoxH && rows.size() > 1) rows.erase(rows.begin());
        // No room at all above a build row that fills the window: idle lines wait rather
        // than draw over the row's text (the composer, asked for, still shows at the top).
        if (!chatTyping_ && boxH() > maxBoxH) return;
        float contentW = 0, alpha = 0;
        for (const Row& r : rows) {
            float w = r.x;
            for (const Seg& g : r.segs) w += width(g.text);
            contentW = std::max(contentW, w);
            alpha = std::max(alpha, r.alpha);
        }
        // While typing the box keeps its full width, so it does not grow under the caret.
        const float boxW = (chatTyping_ ? wrapW : contentW) + 2 * pad;
        const cb::Rect box = cb::place(chatAlign_, boxW, boxH(), areaW, bottom, 0, margin);

        SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
        SDL_FRect bg{box.x, box.y, box.w, box.h};
        SDL_SetRenderDrawColor(ren_, 8, 8, 12, Uint8(200 * alpha));
        SDL_RenderFillRectF(ren_, &bg);
        SDL_SetRenderDrawColor(ren_, 96, 84, 60, Uint8(200 * alpha));   // the HUD's bronze frame
        SDL_RenderDrawRectF(ren_, &bg);

        // Clip so a row can never draw outside the box (a long unbroken name, say).
        SDL_Rect clip{int(box.x), int(box.y), int(std::ceil(box.w)), int(std::ceil(box.h))};
        SDL_RenderSetClipRect(ren_, &clip);
        float y = box.y + pad;
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            if (r.composer && i == composerAt && i > 0) {   // a rule between history and composer
                SDL_SetRenderDrawColor(ren_, 96, 84, 60, 200);
                SDL_RenderDrawLineF(ren_, box.x + pad, y + sepH() / 2 - 3 * s, box.x + box.w - pad, y + sepH() / 2 - 3 * s);
                y += sepH();
            }
            float x = box.x + pad + r.x;
            const float baseY = y - top1 * fs;
            for (const Seg& g : r.segs) {
                const Uint8 a = Uint8(255 * r.alpha);
                font.drawSolid(ren_, g.text, x + shadow, baseY + shadow, fs, {0, 0, 0, a});
                font.drawSolid(ren_, g.text, x, baseY, fs, {g.color.r, g.color.g, g.color.b, a});
                x += width(g.text);
            }
            y += lineH;
        }
        SDL_RenderSetClipRect(ren_, nullptr);
    }
