#pragma once

#include "cob/cob.h"

#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace tak::cob {

// Minimal COB interpreter, sufficient to run unit animation scripts.
// Unit-state queries return 0 and side effects outside piece animation
// (sounds, explosions, attach) are no-ops.

struct PieceState {
    float move[3] = {0, 0, 0};      // world units
    float rot[3] = {0, 0, 0};       // radians
    float spin[3] = {0, 0, 0};      // current rate, radians/sec
    bool visible = true;

    // Spin ramp: SPIN accelerates the rate toward spinTarget, STOP_SPIN
    // decelerates it toward 0, both at spinAccel rad/sec^2 (0 = jump instantly).
    float spinTarget[3] = {0, 0, 0}, spinAccel[3] = {0, 0, 0};

    // In-flight animations (target + speed per axis).
    float moveTarget[3] = {0, 0, 0}, moveSpeed[3] = {0, 0, 0};
    bool moving[3] = {false, false, false};
    float rotTarget[3] = {0, 0, 0}, rotSpeed[3] = {0, 0, 0};
    bool turning[3] = {false, false, false};
};

class Vm {
public:
    explicit Vm(File file);

    // Start a script by name with integer args; returns false if unknown.
    bool start(const std::string& script, const std::vector<int32_t>& args = {});

    // Engine hooks (mission scripting). Defaults: return 0 / ignore.
    std::function<int32_t(int sub, const std::vector<int32_t>&)> onMapCommand;
    std::function<int32_t(int32_t valId, const std::vector<int32_t>&)> onGet;
    std::function<void(int32_t valId, int32_t value)> onSetUnitValue;
    void setStatic(size_t i, int32_t v);
    void reset() { threads_.clear(); }   // stop all threads, keep piece poses

    // Advance time by dt seconds: run threads, progress animations.
    void tick(float dt);

    const std::vector<PieceState>& pieces() const { return pieces_; }
    const File& file() const { return file_; }
    bool anyThreadAlive() const;
    size_t threadCount() const { return threads_.size(); }
    std::vector<uint32_t> threadPcs() const {
        std::vector<uint32_t> out;
        for (const auto& t : threads_) out.push_back(t.pc);
        return out;
    }
    int32_t getStatic(size_t i) const { return i < statics_.size() ? statics_[i] : 0; }

private:
    struct Thread {
        uint32_t pc = 0;
        std::vector<int32_t> stack;
        std::vector<int32_t> locals;
        float sleepUntil = 0;
        int waitPiece = -1, waitAxis = 0;
        bool waitTurn = false;
        uint32_t signalMask = 0;
        bool dead = false;
        std::vector<uint32_t> callStack;
    };

    void run(Thread& t);
    int32_t pop(Thread& t);
    void push(Thread& t, int32_t v);

    File file_;
    std::vector<int32_t> statics_;
    std::vector<PieceState> pieces_;
    std::vector<Thread> threads_;
    std::vector<Thread> pending_;
    bool ticking_ = false;
    float now_ = 0;
    std::mt19937 rng_{12345};
};

} // namespace tak::cob
