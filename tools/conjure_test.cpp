// conjure_test -- the Zhon monarch's (zonhunt) conjure animation, driven through the
// real COB VM the way the client does. The conjure gesture (RestoreWatcher looping
// `build`) and the airborne body pose (FlightControl -> `attack`) both gate on the
// ACTIVE static (6), which the client sets via setSFXoccupy(5) while the flyer works.
// This pins that the arms actually move -- and that they FREEZE if the flyer is not
// kept active, which is the failure the engine's altitude `busy` test guards against
// (traced with tools/re/emuphase.py + cobtool; verified live driving the Vm).
//
//   conjure_test <retail-install-dir>
#include "hpi/hpi.h"
#include "cob/cob.h"
#include "cob/vm.h"
#include <cstdio>
#include <memory>
#include <set>
#include <string>

using namespace tak;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

// Does any arm/hand piece animate over `frames` ticks?
static bool armMoves(cob::Vm& vm, int frames) {
    const cob::File& f = vm.file();
    static const std::set<std::string> arms =
        {"armup_L", "armup_R", "armlow_L", "armlow_R", "hand_R", "hand_L"};
    for (int i = 0; i < frames; ++i) {
        vm.tick(1.0f / 30.0f);
        for (size_t p = 0; p < vm.pieces().size() && p < f.pieces.size(); ++p) {
            if (!arms.count(f.pieces[p])) continue;
            const auto& ps = vm.pieces()[p];
            for (int a = 0; a < 3; ++a)
                if (ps.moving[a] || ps.turning[a] || ps.spin[a] != 0.0f) return true;
        }
    }
    return false;
}

static std::shared_ptr<const cob::File> load(const hpi::Vfs& vfs) {
    return std::make_shared<const cob::File>(
        cob::load(vfs.read("scripts/zonhunt.cob"), "zonhunt.cob"));
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: conjure_test <install>\n"); return 2; }
    hpi::Vfs vfs = hpi::mountRetailRoot(argv[1]);
    auto f = load(vfs);
    check(f->scriptIndex("RestoreWatcher") >= 0 && f->scriptIndex("FlightControl") >= 0 &&
              f->scriptIndex("build") >= 0,
          "zonhunt carries the conjure threads (RestoreWatcher/FlightControl/build)");

    std::printf("[conjure while ACTIVE -- the client's normal case]\n");
    {
        cob::Vm vm(f);
        vm.start("Create");
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("setSFXoccupy", {5});   // active (static 6) -- client sets this while busy
        vm.start("BeginFlight");
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("StartBuilding");
        check(armMoves(vm, 120), "the conjure arm gesture plays (build loops via RestoreWatcher)");
    }

    std::printf("[conjure while NOT active -- documents the freeze the busy test prevents]\n");
    {
        cob::Vm vm(f);
        vm.start("Create");
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("setSFXoccupy", {0});   // NOT active (static 6 clear)
        for (int i = 0; i < 30; ++i) vm.tick(1.0f / 30.0f);
        vm.start("StartBuilding");
        check(!armMoves(vm, 120),
              "with the flyer inactive the arms FREEZE (why the engine keeps it airborne)");
    }

    std::printf("conjure_test: %s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
