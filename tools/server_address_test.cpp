#include "client/serveraddress.h"
#include <cstdio>

int main() {
    int failed=0;
    auto check=[&](const char* input,const char* host,unsigned port) {
        const auto a=tak::parseServerAddress(input);
        if(!a || a->host!=host || a->port!=port) {++failed;std::fprintf(stderr,"FAIL: %s\n",input);return;}
        const auto roundtrip=tak::parseServerAddress(tak::formatServerAddress(a->host,a->port));
        if(!roundtrip || roundtrip->host!=a->host || roundtrip->port!=a->port)++failed;
    };
    check("tak.pgnet.us","tls://tak.pgnet.us",7677);
    check(" tak.pgnet.us \n","tls://tak.pgnet.us",7677);
    check("tak.pgnet.us:1234","tls://tak.pgnet.us",1234);
    check("tls://tak.pgnet.us:7677","tls://tak.pgnet.us",7677);
    check("192.0.2.1","tls://192.0.2.1",7677);
    check("[::1]","tls://::1",7677);
    check("::1","tls://::1",7677);
    check("tls://[::1]:1234","tls://::1",1234);
    check("tcp://[::1]:1234","::1",1234);
    check("tcp://localhost","localhost",7677);
    for(const char* input:{""," ","host:","host:0","host:65536","host:-1","host:123abc","[::1", "[::1]junk","tls://","https://host"})
        if(tak::parseServerAddress(input)) {++failed;std::fprintf(stderr,"accepted invalid: %s\n",input);}
    return failed?1:0;
}
