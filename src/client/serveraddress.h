#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tak {
struct ServerAddress { std::string host; uint16_t port=7677; };

// Menu addresses default to verified TLS. Plain TCP is an explicit LAN/testing
// opt-in; the automatically launched local server uses its separate launch path.
inline std::optional<ServerAddress> parseServerAddress(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==text.npos)return {};
    text=text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
    bool secure=true;
    if(text.starts_with("tls://"))text.remove_prefix(6);
    else if(text.starts_with("tcp://")) {secure=false;text.remove_prefix(6);}
    if(text.empty() || text.find_first_of("/ \t\r\n")!=text.npos)return {};
    std::string_view host=text,port;
    bool hasPort=false;
    if(text.front()=='[') {
        const auto end=text.find(']');
        if(end==text.npos)return {};
        host=text.substr(1,end-1);
        const auto rest=text.substr(end+1);
        if(!rest.empty()) {
            if(rest.front()!=':')return {};
            hasPort=true;port=rest.substr(1);
        }
    } else if(const auto colon=text.find(':');colon!=text.npos && colon==text.rfind(':')) {
        host=text.substr(0,colon);port=text.substr(colon+1);hasPort=true;
    }
    if(host.empty() || host.find_first_of("[]")!=host.npos)return {};
    unsigned number=7677;
    if(hasPort) {
        const auto parsed=std::from_chars(port.data(),port.data()+port.size(),number);
        if(parsed.ec!=std::errc{} || parsed.ptr!=port.data()+port.size() || number==0 || number>65535)return {};
    }
    return ServerAddress{(secure?"tls://":"")+std::string(host),uint16_t(number)};
}

inline std::string formatServerAddress(std::string host,uint16_t port) {
    const bool secure=host.starts_with("tls://");
    if(secure)host.erase(0,6);
    if(host.find(':')!=host.npos)host="["+host+"]";
    return std::string(secure?"tls://":"tcp://")+host+":"+std::to_string(port);
}
}
