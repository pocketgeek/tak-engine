#include "cob/vm.h"

#include <cmath>
#include <stdexcept>

namespace tak::cob {

namespace {

constexpr float kPi = 3.14159265358979f;
// COB angles: 65536 units per full circle. Linear: 65536 per world unit.
constexpr float kAngle = 2 * kPi / 65536.0f;
constexpr float kLinear = 1.0f / 65536.0f;
// COB move/turn/spin speeds are per SECOND. (Retail stores speed/30 as a per-frame
// step at its fixed 30Hz update -- i.e. `speed` angle-or-linear units per second --
// so we step by speed*dt in real time and they integrate to the same rate.)

float towards(float cur, float target, float step) {
    if (cur < target) return std::min(cur + step, target);
    return std::max(cur - step, target);
}

// Angular step toward a target along the SHORTEST path, like the retail COB
// interpreter (which compares abs(target-cur) to a half-circle and flips the
// turn direction). Without this, a turn from e.g. 350deg to 10deg sweeps the
// long way through 180deg instead of the short way through 0.
float angTowards(float cur, float target, float step) {
    float d = std::remainder(target - cur, 2 * kPi);   // shortest signed delta in (-pi,pi]
    if (std::abs(d) <= step) return target;
    return cur + (d > 0 ? step : -step);
}

} // namespace

Vm::Vm(File file) : file_(std::move(file)) {
    statics_.assign(file_.numStatics + 8, 0);
    pieces_.assign(file_.pieces.size(), PieceState{});
}

bool Vm::start(const std::string& script, const std::vector<int32_t>& args) {
    int idx = file_.scriptIndex(script);
    if (idx < 0) return false;
    Thread t;
    t.pc = file_.scripts[size_t(idx)].entry;
    t.locals = args;
    t.locals.resize(std::max<size_t>(args.size(), 16), 0);
    // Starting from inside a hook while tick() iterates threads_ must not
    // invalidate the iteration: defer to end of tick.
    if (ticking_) pending_.push_back(std::move(t));
    else threads_.push_back(std::move(t));
    return true;
}

void Vm::setStatic(size_t i, int32_t v) {
    if (i >= statics_.size()) statics_.resize(i + 1, 0);
    statics_[i] = v;
}

bool Vm::anyThreadAlive() const {
    for (const auto& t : threads_)
        if (!t.dead) return true;
    return false;
}

int32_t Vm::pop(Thread& t) {
    if (t.stack.empty()) return 0;
    int32_t v = t.stack.back();
    t.stack.pop_back();
    return v;
}

void Vm::push(Thread& t, int32_t v) { t.stack.push_back(v); }

void Vm::tick(float dt) {
    now_ += dt;

    // Progress piece animations.
    for (auto& p : pieces_) {
        for (int a = 0; a < 3; ++a) {
            if (p.moving[a]) {
                p.move[a] = towards(p.move[a], p.moveTarget[a], p.moveSpeed[a] * dt);
                if (p.move[a] == p.moveTarget[a]) p.moving[a] = false;
            }
            if (p.turning[a]) {
                p.rot[a] = angTowards(p.rot[a], p.rotTarget[a], p.rotSpeed[a] * dt);
                if (p.rot[a] == p.rotTarget[a]) p.turning[a] = false;
            }
            if (p.spinAccel[a] > 0 && p.spin[a] != p.spinTarget[a])
                p.spin[a] = towards(p.spin[a], p.spinTarget[a], p.spinAccel[a] * dt);
            p.rot[a] += p.spin[a] * dt;
        }
    }

    ticking_ = true;
    for (size_t i = 0; i < threads_.size(); ++i)
        if (!threads_[i].dead) run(threads_[i]);
    ticking_ = false;
    for (auto& t : pending_) threads_.push_back(std::move(t));
    pending_.clear();
    std::erase_if(threads_, [](const Thread& t) { return t.dead; });
}

void Vm::run(Thread& t) {
    if (now_ < t.sleepUntil) return;
    if (t.waitPiece >= 0) {
        const auto& p = pieces_[size_t(t.waitPiece)];
        bool busy = t.waitTurn ? p.turning[t.waitAxis] : p.moving[t.waitAxis];
        if (busy) return;
        t.waitPiece = -1;
    }

    const auto& code = file_.code;
    for (int guard = 0; guard < 5000; ++guard) {
        if (t.pc >= code.size()) { t.dead = true; return; }
        uint32_t op = code[t.pc];
        auto arg = [&](int i) { return int32_t(code[t.pc + 1 + size_t(i)]); };

        switch (op) {
            case 0x10021001: push(t, arg(0)); t.pc += 2; break;               // PUSH_CONST
            case 0x10021002: {                                                // PUSH_LOCAL
                size_t i = size_t(arg(0));
                push(t, i < t.locals.size() ? t.locals[i] : 0);
                t.pc += 2; break;
            }
            case 0x10021004: {                                                // PUSH_STATIC
                size_t i = size_t(arg(0));
                push(t, i < statics_.size() ? statics_[i] : 0);
                t.pc += 2; break;
            }
            case 0x10022000: t.locals.push_back(0); t.pc += 1; break;         // CREATE_LOCAL
            case 0x10023002: {                                                // POP_LOCAL
                size_t i = size_t(arg(0));
                if (i >= t.locals.size()) t.locals.resize(i + 1, 0);
                t.locals[i] = pop(t);
                t.pc += 2; break;
            }
            case 0x10023004: {                                                // POP_STATIC
                size_t i = size_t(arg(0));
                if (i >= statics_.size()) statics_.resize(i + 1, 0);
                statics_[i] = pop(t);
                t.pc += 2; break;
            }
            case 0x10024000: pop(t); t.pc += 1; break;                        // POP_STACK

            case 0x10031000: { auto b = pop(t), a = pop(t); push(t, a + b); t.pc += 1; break; }
            case 0x10032000: { auto b = pop(t), a = pop(t); push(t, a - b); t.pc += 1; break; }
            case 0x10033000: { auto b = pop(t), a = pop(t); push(t, a * b); t.pc += 1; break; }
            case 0x10034000: { auto b = pop(t), a = pop(t); push(t, b ? a / b : 0); t.pc += 1; break; }
            case 0x10035000: { auto b = pop(t), a = pop(t); push(t, b ? a % b : 0); t.pc += 1; break; }
            case 0x10039000: { auto b = pop(t), a = pop(t); push(t, a & b); t.pc += 1; break; }
            case 0x1003A000: { auto b = pop(t), a = pop(t); push(t, a | b); t.pc += 1; break; }
            case 0x1003B000: { auto b = pop(t), a = pop(t); push(t, a ^ b); t.pc += 1; break; }
            case 0x1003C000: push(t, ~pop(t)); t.pc += 1; break;

            case 0x10041000: {                                                // RAND
                int32_t hi = pop(t), lo = pop(t);
                if (hi < lo) std::swap(hi, lo);
                push(t, lo + int32_t(rng_() % uint32_t(hi - lo + 1)));
                t.pc += 1; break;
            }
            case 0x10042000: {                                                // GET_UNIT_VALUE
                int32_t valId = pop(t);
                push(t, onGet ? onGet(valId, {}) : 0);
                t.pc += 1; break;
            }
            case 0x10043000: {                                                // GET (valId + 4)
                int32_t p4 = pop(t), p3 = pop(t), p2 = pop(t), p1 = pop(t);
                int32_t valId = pop(t);
                push(t, onGet ? onGet(valId, {p1, p2, p3, p4}) : 0);
                t.pc += 1; break;
            }

            case 0x10051000: { auto b = pop(t), a = pop(t); push(t, a < b); t.pc += 1; break; }
            case 0x10052000: { auto b = pop(t), a = pop(t); push(t, a <= b); t.pc += 1; break; }
            case 0x10053000: { auto b = pop(t), a = pop(t); push(t, a > b); t.pc += 1; break; }
            case 0x10054000: { auto b = pop(t), a = pop(t); push(t, a >= b); t.pc += 1; break; }
            case 0x10055000: { auto b = pop(t), a = pop(t); push(t, a == b); t.pc += 1; break; }
            case 0x10056000: { auto b = pop(t), a = pop(t); push(t, a != b); t.pc += 1; break; }
            case 0x10057000: { auto b = pop(t), a = pop(t); push(t, a && b); t.pc += 1; break; }
            case 0x10058000: { auto b = pop(t), a = pop(t); push(t, a || b); t.pc += 1; break; }
            case 0x1005A000: push(t, !pop(t)); t.pc += 1; break;

            case 0x10061000: {                                                // START_SCRIPT
                int32_t script = arg(0), nparams = arg(1);
                std::vector<int32_t> params(size_t(std::max(nparams, 0)));
                for (int i = nparams - 1; i >= 0; --i) params[size_t(i)] = pop(t);
                t.pc += 3;
                if (script >= 0 && size_t(script) < file_.scripts.size()) {
                    Thread nt;
                    nt.pc = file_.scripts[size_t(script)].entry;
                    nt.locals = params;
                    nt.locals.resize(std::max<size_t>(params.size(), 16), 0);
                    nt.signalMask = t.signalMask;
                    // Defer during tick: pushing to threads_ would invalidate
                    // the reference we are executing from.
                    if (ticking_) pending_.push_back(std::move(nt));
                    else threads_.push_back(std::move(nt));
                    // Retail continues the parent immediately (pc already advanced
                    // by 3), rather than yielding -- so a script that starts several
                    // sub-scripts kicks them all off in the same tick.
                    break;
                }
                break;
            }
            case 0x10062000: {                                                // CALL_SCRIPT
                int32_t script = arg(0), nparams = arg(1);
                for (int i = 0; i < nparams; ++i) pop(t);
                t.callStack.push_back(t.pc + 3);
                if (script >= 0 && size_t(script) < file_.scripts.size())
                    t.pc = file_.scripts[size_t(script)].entry;
                else { t.dead = true; return; }
                break;
            }
            case 0x10064000: t.pc = uint32_t(arg(0)); break;                  // JUMP
            case 0x10065000:                                                  // RETURN
                pop(t);
                if (t.callStack.empty()) { t.dead = true; return; }
                t.pc = t.callStack.back();
                t.callStack.pop_back();
                break;
            case 0x10066000:                                                  // JUMP_IF_FALSE
                if (pop(t) == 0) t.pc = uint32_t(arg(0));
                else t.pc += 2;
                break;
            case 0x10067000: {                                                // SIGNAL
                uint32_t mask = uint32_t(pop(t));
                t.pc += 1;
                for (auto& other : threads_)
                    if (&other != &t && (other.signalMask & mask)) other.dead = true;
                break;
            }
            case 0x10068000: t.signalMask = uint32_t(pop(t)); t.pc += 1; break;

            case 0x10001000: case 0x1000B000: {                               // MOVE[_NOW]
                int piece = arg(0), axis = arg(1);
                bool now = op == 0x1000B000;
                // Push order is [speed, target]: target is on top.
                float target = float(pop(t)) * kLinear;
                float speed = now ? 0 : float(pop(t)) * kLinear;
                if (piece >= 0 && size_t(piece) < pieces_.size() && axis >= 0 && axis < 3) {
                    auto& p = pieces_[size_t(piece)];
                    if (now || speed <= 0) { p.move[axis] = target; p.moving[axis] = false; }
                    else {
                        p.moveTarget[axis] = target;
                        p.moveSpeed[axis] = speed;
                        p.moving[axis] = true;
                    }
                }
                t.pc += 3; break;
            }
            case 0x10002000: case 0x1000C000: {                               // TURN[_NOW]
                int piece = arg(0), axis = arg(1);
                bool now = op == 0x1000C000;
                // Push order is [speed, target]: target is on top.
                float target = float(pop(t)) * kAngle;
                float speed = now ? 0 : float(pop(t)) * kAngle;
                if (piece >= 0 && size_t(piece) < pieces_.size() && axis >= 0 && axis < 3) {
                    auto& p = pieces_[size_t(piece)];
                    if (now || speed <= 0) { p.rot[axis] = target; p.turning[axis] = false; }
                    else {
                        p.rotTarget[axis] = target;
                        p.rotSpeed[axis] = std::abs(speed);
                        p.turning[axis] = true;
                    }
                }
                t.pc += 3; break;
            }
            case 0x10003000: {                                                // SPIN
                // Retail pops speed (target rate) first, then acceleration; it
                // ramps the rate up at that accel, or jumps instantly if accel==0.
                int piece = arg(0), axis = arg(1);
                float target = float(pop(t)) * kAngle;   // target rate (rad/sec)
                float accel = float(pop(t)) * kAngle;    // ramp (rad/sec^2)
                if (piece >= 0 && size_t(piece) < pieces_.size() && axis >= 0 && axis < 3) {
                    auto& p = pieces_[size_t(piece)];
                    p.spinTarget[axis] = target;
                    p.spinAccel[axis] = std::abs(accel);
                    if (accel == 0) p.spin[axis] = target;
                }
                t.pc += 3; break;
            }
            case 0x10004000: {                                                // STOP_SPIN
                // Decelerate the rate toward 0 at the given decel (0 = stop dead).
                int piece = arg(0), axis = arg(1);
                float decel = float(pop(t)) * kAngle;
                if (piece >= 0 && size_t(piece) < pieces_.size() && axis >= 0 && axis < 3) {
                    auto& p = pieces_[size_t(piece)];
                    p.spinTarget[axis] = 0;
                    p.spinAccel[axis] = std::abs(decel);
                    if (decel == 0) p.spin[axis] = 0;
                }
                t.pc += 3; break;
            }
            case 0x10005000:                                                  // SHOW
                if (size_t(arg(0)) < pieces_.size()) pieces_[size_t(arg(0))].visible = true;
                t.pc += 2; break;
            case 0x10006000:                                                  // HIDE
                if (size_t(arg(0)) < pieces_.size()) pieces_[size_t(arg(0))].visible = false;
                t.pc += 2; break;

            case 0x10007000: case 0x10008000: case 0x1000E000: case 0x1000F000:
                t.pc += 2; break;                                             // CACHE/SHADE
            case 0x10010000: pop(t); t.pc += 2; break;                        // EMIT_SFX
            case 0x10071000: pop(t); t.pc += 2; break;                        // EXPLODE
            case 0x10072000: t.pc += 2; break;                                // PLAY_SOUND
            case 0x10073000: {                                                // MAP_COMMAND
                int sub = arg(0), argc = arg(1);
                std::vector<int32_t> params(size_t(std::max(argc, 0)));
                for (int i = argc - 1; i >= 0; --i) params[size_t(i)] = pop(t);
                push(t, onMapCommand ? onMapCommand(sub, params) : 0);
                t.pc += 3; break;
            }
            case 0x10082000: {                                                // SET_UNIT_VALUE
                int32_t value = pop(t);
                int32_t valId = pop(t);
                if (onSetUnitValue) onSetUnitValue(valId, value);
                t.pc += 1; break;
            }
            case 0x10083000: pop(t); pop(t); pop(t); t.pc += 1; break;        // ATTACH
            case 0x10084000: pop(t); t.pc += 1; break;                        // DROP

            case 0x10011000: case 0x10012000: {                               // WAIT_TURN/MOVE
                t.waitPiece = arg(0);
                t.waitAxis = arg(1);
                t.waitTurn = op == 0x10011000;
                t.pc += 3;
                return;
            }
            case 0x10013000: {                                                // SLEEP
                int32_t ms = pop(t);
                t.sleepUntil = now_ + float(ms) / 1000.0f;
                t.pc += 1;
                return;
            }
            default:
                // Unknown opcode: stop this thread rather than corrupt state.
                t.dead = true;
                return;
        }
    }
}

} // namespace tak::cob
