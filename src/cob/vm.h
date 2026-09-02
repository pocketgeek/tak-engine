#pragma once

#include "cob/cob.h"

#include <cstdint>
#include <random>
#include <vector>

namespace tak::cob {

// Minimal COB interpreter, sufficient to run unit animation scripts.
// Unit-state queries return 0 and side effects outside piece animation
// (sounds, explosions, attach) are no-ops.

struct PieceState {
    float move[3] = {0, 0, 0};      // world units
    float rot[3] = {0, 0, 0};       // radians
    float spin[3] = {0, 0, 0};      // radians/sec
    bool visible = true;

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
    void setStatic(size_t i, int32_t v);

    // Advance time by dt seconds: run threads, progress animations.
    void tick(float dt);

    const std::vector<PieceState>& pieces() const { return pieces_; }
    const File& file() const { return file_; }
    bool anyThreadAlive() const;

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
    float now_ = 0;
    std::mt19937 rng_{12345};
};

} // namespace tak::cob
