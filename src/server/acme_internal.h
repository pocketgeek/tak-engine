#pragma once
#include <cstdint>
#include <memory>
#include <string>
namespace tak::srv::acme_detail {
// Internal HTTP-01 responder. The listener exists only for this object's lifetime.
class Challenge {
public:
    Challenge(uint16_t port,std::string token,std::string authorization);
    ~Challenge();
    Challenge(const Challenge&)=delete;
    Challenge& operator=(const Challenge&)=delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bool validDomain(const std::string& domain);
}
