#pragma once
#include "cob/cob.h"
#include <vector>

namespace tak::sim {
// 512860 retires an owner immediately when there is no Dying callback and
// Killed did not arm an owner timer. Recognize the constant, bitmap-explosion
// Killed handlers used by buildings, without guessing about arbitrary scripts.
// These handlers always request corpse 1, including on explosion damage.
inline bool retailInstantCorpse(const cob::File& file) {
    if (file.scriptIndex("Dying")>=0 || file.scriptIndex("death")>=0) return false;
    const int index=file.scriptIndex("Killed");
    if (index<0) return false;
    size_t pc=file.scripts[size_t(index)].entry;
    std::vector<int32_t> stack;
    int32_t corpse=0;
    for (unsigned steps=0;steps<256 && pc<file.code.size();++steps) {
        const uint32_t op=file.code[pc++];
        switch (op) {
            case 0x10022000: break; // CREATE_LOCAL
            case 0x10021001: // PUSH_CONST
                if (pc>=file.code.size()) return false;
                stack.push_back(int32_t(file.code[pc++]));break;
            case 0x10023002: // POP_LOCAL: only the corpse out-parameter
                if (pc>=file.code.size() || file.code[pc++]!=1 || stack.empty()) return false;
                corpse=stack.back();stack.pop_back();break;
            case 0x10036000: { // bitwise OR
                if (stack.size()<2) return false;
                const auto value=stack.back();stack.pop_back();stack.back()|=value;break;
            }
            case 0x10071000: // EXPLODE: immediate bitmap effects, no detached piece
                if (pc>=file.code.size() || stack.empty() || !(stack.back()&32)) return false;
                if (file.code[pc++]>=file.pieces.size()) return false;
                stack.pop_back();break;
            case 0x10065000: return corpse==1;
            default: return false; // any state query, timer, branch or asynchronous work
        }
    }
    return false;
}
}
