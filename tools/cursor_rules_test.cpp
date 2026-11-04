// The game view's cursor decisions (client/cursorrules.h), row by row against
// retail's selector (KINGDOMS.icd 0x525d30 / 0x521cd0 / 0x4dd780) in the
// right-click interface the shipped installs use, plus the rows that are ours.
// Pure functions: no renderer, no data.

#include "client/cursorrules.h"

#include <cstdio>
#include <string>

namespace {
using tak::CursorId;
using namespace tak::client;

int failures = 0;

const char* name(CursorId c) { return tak::cursorSequenceName(c); }

void expect(CursorId got, CursorId want, const std::string& row) {
    if (got == want) return;
    std::fprintf(stderr, "FAIL: %s: got %s, want %s\n", row.c_str(), name(got), name(want));
    ++failures;
}

CursorId stage(const CursorFrame& f, CursorStage want, const std::string& row) {
    CursorId fixed = CursorId::Hourglass;
    const CursorStage got = cursorStage(f, fixed);
    if (got != want) {
        std::fprintf(stderr, "FAIL: %s: stage %d, want %d\n", row.c_str(), int(got), int(want));
        ++failures;
    }
    return fixed;
}

CursorFrame world() { CursorFrame f; f.area = PointerArea::World; return f; }

void stages() {
    // Retail slot order (0x4bdc0e): the aggregator keeps the lowest.
    expect(lowerCursorSlot(CursorId::TooFar, CursorId::Attack), CursorId::Attack, "slot: Attack beats TooFar");
    expect(lowerCursorSlot(CursorId::Airstrike, CursorId::TooFar), CursorId::Airstrike, "slot: Airstrike beats TooFar");
    expect(lowerCursorSlot(CursorId::Red, CursorId::Select), CursorId::Select, "slot: Select beats Red");
    expect(lowerCursorSlot(CursorId::Green, CursorId::Red), CursorId::Red, "slot: Red beats Green");
    expect(lowerCursorSlot(CursorId::Normal, CursorId::Green), CursorId::Green, "slot: Green beats Normal");
    if (retailCursorSlot(CursorId::Attack) != 1 || retailCursorSlot(CursorId::Repair) != 6 ||
        retailCursorSlot(CursorId::Move) != 14 || retailCursorSlot(CursorId::Normal) != 19 ||
        retailCursorSlot(CursorId::Hourglass) != 20) {
        std::fprintf(stderr, "FAIL: retail slot numbers\n");
        ++failures;
    }

    CursorFrame f = world(); f.modalUi = true; f.armed = true;
    expect(stage(f, CursorStage::Fixed, "A4 modal UI"), CursorId::Normal, "A4 modal UI is Normal");
    f = world(); f.area = PointerArea::Interface; f.armed = true;
    expect(stage(f, CursorStage::Fixed, "A5/A14 interface"), CursorId::Normal,
           "A5/A14 build strip, command panel, bottom bar: Normal even armed");
    f.armed = false; f.placing = true;
    expect(stage(f, CursorStage::Fixed, "A5 interface + placing"), CursorId::Normal, "A5 interface hides placement");
    f = world(); f.area = PointerArea::Unknown;
    expect(stage(f, CursorStage::Fixed, "A13 no pointer"), CursorId::Normal, "A13 no pointer yet");

    f = world(); f.watcher = true; f.watcherOverUnit = true;
    expect(stage(f, CursorStage::Fixed, "A7 watcher"), CursorId::Select, "A7 watcher over a finished unit: Select (ours)");
    f.watcherOverUnit = false;
    expect(stage(f, CursorStage::Fixed, "A8 watcher"), CursorId::Normal, "A8 watcher elsewhere");
    f.watcherOverUnit = true; f.area = PointerArea::Interface;
    expect(stage(f, CursorStage::Fixed, "A6 watcher UI"), CursorId::Normal, "A6 watcher over the HUD");
    f.area = PointerArea::Minimap;
    expect(stage(f, CursorStage::Fixed, "A6 watcher minimap"), CursorId::Normal, "A6 watcher over the minimap");
    f.area = PointerArea::World; f.boxDrag = true;
    expect(stage(f, CursorStage::Fixed, "A6 watcher drag"), CursorId::Normal, "A6 watcher dragging");

    // Mode 14 in the main view: the ghost shows validity, the cursor is Normal.
    f = world(); f.placing = true;
    expect(stage(f, CursorStage::Fixed, "A9 placing"), CursorId::Normal, "A9/A10 build placement: Normal (retail 0x525d30)");
    f.area = PointerArea::Minimap;
    expect(stage(f, CursorStage::Fixed, "A9 placing minimap"), CursorId::Normal,
           "A9 placement over the minimap: Normal (our minimap click moves the camera)");
    f = world(); f.reclaimAreaDrag = true; f.boxDrag = false;
    expect(stage(f, CursorStage::Fixed, "A11 reclaim box"), CursorId::Reclaim, "A11 right-drag clear-area box");
    f = world(); f.boxDrag = true; f.armed = true;
    expect(stage(f, CursorStage::Fixed, "A12 box select"), CursorId::Normal, "A12 left-drag box select");
    f = world(); f.armed = true;
    stage(f, CursorStage::Armed, "armed in the main view");
    f.area = PointerArea::Minimap;
    stage(f, CursorStage::Armed, "armed over the minimap");
    f = world();
    stage(f, CursorStage::Hover, "hover in the main view");
    f.area = PointerArea::Minimap;
    stage(f, CursorStage::Hover, "hover over the minimap");
}

void hover() {
    const auto h = [](HoverKind k, bool others, bool raise = false, bool reclaim = false) {
        HoverFacts f; f.target = k; f.othersSelected = others;
        f.raisableAtPoint = raise; f.reclaimableAtPoint = reclaim;
        return cursorForHover(f);
    };
    // Nothing selected (or only the hovered unit): 0x521cd0's empty list.
    expect(h(HoverKind::OwnSelectable, false), CursorId::Select, "D own finished unit, nothing selected");
    expect(h(HoverKind::Friendly, false), CursorId::Normal, "D own site / ally, nothing selected");
    expect(h(HoverKind::Enemy, false), CursorId::Normal, "D enemy, nothing selected");
    expect(h(HoverKind::None, false, true, true), CursorId::Normal, "D body / feature, nothing selected");
    expect(h(HoverKind::None, false), CursorId::Normal, "D ground, nothing selected");

    // Something selected: only Select, Red, Green and Normal.
    expect(h(HoverKind::Friendly, true), CursorId::Green,
           "C4 owner's case: a selected builder over the site it builds (or can build): Green, not Repair");
    expect(h(HoverKind::Friendly, true), CursorId::Green, "C5 non-builder over an own/allied site: Green");
    expect(h(HoverKind::OwnSelectable, true), CursorId::Select, "C2/C3 own finished transport: Select, not Load");
    expect(h(HoverKind::OwnSelectable, true), CursorId::Select, "C6 damaged own unit with a builder: Select, not Repair");
    expect(h(HoverKind::Friendly, true), CursorId::Green, "C6 damaged ally with a builder: Green, not Repair");
    expect(h(HoverKind::OwnSelectable, true), CursorId::Select, "C7 own finished unit");
    expect(h(HoverKind::Friendly, true), CursorId::Green, "C8 ally unit");
    expect(h(HoverKind::Enemy, true), CursorId::Red, "C9-C13 enemy: Red for any selection");
    expect(h(HoverKind::None, true, true, false), CursorId::Green, "C14 a body a selected unit can raise: Green");
    expect(h(HoverKind::None, true, false, true), CursorId::Green, "C15 reclaimable feature with a reclaimer: Green");
    expect(h(HoverKind::None, true), CursorId::Normal, "C16 ground");
    // C1: a loaded transport selected has no special case: ground stays Normal.
    expect(h(HoverKind::None, true), CursorId::Normal, "C1 loaded transport over ground: Normal, not Unload");
    expect(h(HoverKind::Enemy, true), CursorId::Red, "C1 loaded transport over an enemy: Red");
}

void attack() {
    const auto mix = [](HoverKind target, std::initializer_list<AttackUnitFacts> units) {
        CursorId c = CursorId::Normal;
        for (const auto& u : units) c = lowerCursorSlot(c, attackUnitCursor(target, u));
        return c;
    };
    AttackUnitFacts mobileHit; mobileHit.attacker = true; mobileHit.mobile = true; mobileHit.canHit = true;
    AttackUnitFacts mobileMiss = mobileHit; mobileMiss.canHit = false;
    AttackUnitFacts tower; tower.attacker = true; tower.canHit = true;
    AttackUnitFacts towerOut = tower; towerOut.canHit = false;
    AttackUnitFacts air; air.attacker = true; air.mobile = true; air.airstrike = true;
    AttackUnitFacts rally; rally.rallyHolder = true;
    AttackUnitFacts none;

    expect(mix(HoverKind::Enemy, {none}), CursorId::Normal, "B a: no attacker selected");
    expect(mix(HoverKind::Enemy, {mobileHit}), CursorId::Attack, "B a: enemy a mobile unit can hurt");
    expect(mix(HoverKind::Enemy, {mobileMiss}), CursorId::TooFar, "B a: enemy it cannot hurt (immunity)");
    expect(mix(HoverKind::Enemy, {towerOut}), CursorId::TooFar, "B a: enemy out of a tower's range");
    expect(mix(HoverKind::Enemy, {tower}), CursorId::Attack, "B a: enemy in a tower's range");
    expect(mix(HoverKind::Enemy, {mobileMiss, mobileHit}), CursorId::Attack, "B a: lowest slot wins");
    expect(mix(HoverKind::Enemy, {air}), CursorId::Airstrike, "B a: airstrike weapon, any target");
    expect(mix(HoverKind::Enemy, {air, mobileMiss}), CursorId::Airstrike, "B a: Airstrike beats TooFar");
    expect(mix(HoverKind::Enemy, {air, mobileHit}), CursorId::Attack, "B a: Attack beats Airstrike");
    expect(mix(HoverKind::None, {mobileMiss}), CursorId::Attack, "B a: ground");
    expect(mix(HoverKind::None, {air}), CursorId::Airstrike, "B a: all-airstrike selection over ground");
    expect(mix(HoverKind::None, {air, mobileMiss}), CursorId::Attack, "B a: mixed selection over ground");
    expect(mix(HoverKind::Friendly, {mobileHit}), CursorId::TooFar, "B a: friendly target, mobile attacker: TooFar");
    expect(mix(HoverKind::OwnSelectable, {mobileHit}), CursorId::TooFar, "B a: own unit, mobile attacker: TooFar");
    expect(mix(HoverKind::Friendly, {tower}), CursorId::Attack, "B a: friendly in an immobile unit's range");
    expect(mix(HoverKind::Friendly, {towerOut}), CursorId::TooFar, "B a: friendly out of an immobile unit's range");
    expect(mix(HoverKind::Enemy, {rally}), CursorId::Attack, "C10/B a: rally holder sends its output (ours)");
}

void armed() {
    const auto a = [](char cmd, bool capable, auto&& set) {
        ArmedFacts f; f.cmd = cmd; f.capable = capable; set(f);
        return cursorForArmed(f);
    };
    const auto plain = [](ArmedFacts&) {};
    for (char cmd : {'m', 'f', 'p', 'g', 'r', 'c', 'u', 'l'})
        expect(a(cmd, false, plain), CursorId::Normal, std::string("B ") + cmd + ": nothing capable");
    expect(a('m', true, plain), CursorId::Move, "B m: Move everywhere");
    expect(a('f', true, plain), CursorId::Move, "B f: fight-move shows Move (retail MOVE + 0x400)");
    expect(a('p', true, plain), CursorId::Patrol, "B p: Patrol");
    expect(a('u', true, plain), CursorId::Unload, "B u: Unload");
    expect(a('g', true, [](ArmedFacts& f) { f.eligibleTarget = true; }), CursorId::Defend, "B g: over a guardable unit");
    expect(a('g', true, plain), CursorId::Normal, "B g: elsewhere");
    expect(a('r', true, [](ArmedFacts& f) { f.eligibleTarget = true; }), CursorId::Repair, "B r: over a site or damaged friend");
    expect(a('r', true, plain), CursorId::Normal, "B r: elsewhere");
    expect(a('c', true, [](ArmedFacts& f) { f.reclaimAtPoint = true; f.raiseAtPoint = true; }), CursorId::Reclaim,
           "B c: a raisable body that is a reclaimable feature: Reclaim (retail mode 12)");
    expect(a('c', true, [](ArmedFacts& f) { f.reclaimAtPoint = true; }), CursorId::Reclaim, "B c: reclaimable feature");
    expect(a('c', true, [](ArmedFacts& f) { f.raiseAtPoint = true; }), CursorId::Revive,
           "B c: a body only raisable (ours: the click raises it)");
    expect(a('c', true, plain), CursorId::Normal, "B c: elsewhere");
    expect(a('l', true, [](ArmedFacts& f) { f.singleTransport = true; f.loadTarget = true; }), CursorId::Load,
           "B l: one transport over a passenger");
    expect(a('l', true, [](ArmedFacts& f) { f.singleTransport = true; }), CursorId::Normal, "B l: no passenger");
    expect(a('l', true, [](ArmedFacts& f) { f.loadTarget = true; }), CursorId::Normal, "B l: several transports");
}
} // namespace

int main() {
    stages();
    hover();
    attack();
    armed();
    if (failures) return 1;
    std::printf("cursor rules: all rows pass\n");
    return 0;
}
