#pragma once
#include "crt/crt.h"
#include <optional>
namespace cart {
enum class RuleColumn { Group, Condition, Action };
struct RuleClipboard {
    RuleColumn column=RuleColumn::Group;
    std::vector<tak::crt::RuleGroup> groups;
    std::optional<tak::crt::Rule> rule;
    bool copy(const std::vector<tak::crt::RuleGroup>& source,int group,int row,RuleColumn selected,bool wholePlayer=false);
    // Inserts after the selection (or at the end). Incompatible operand kinds
    // are rejected, rather than silently turning an action into a condition.
    bool paste(std::vector<tak::crt::RuleGroup>& target,int& group,int& row,RuleColumn selected) const;
};
bool moveRule(std::vector<tak::crt::RuleGroup>& groups,int& group,int& row,RuleColumn selected,int direction);
}
