#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
inline uint32_t fake_ms=0;
inline uint32_t millis(){return fake_ms;}
inline void delay(uint32_t ms){fake_ms+=ms;}
inline size_t strlcpy(char *dst,const char *src,size_t length){
    size_t size=std::strlen(src);
    if(length){size_t n=std::min(size,length-1);std::memcpy(dst,src,n);dst[n]=0;}
    return size;
}
struct FakeSerial {
    std::vector<uint8_t> input,output; size_t at=0;
    int available(){return int(input.size()-at);}
    int read(){return available()?input[at++]:-1;}
    size_t readBytes(uint8_t *p,size_t n){n=std::min(n,input.size()-at);std::memcpy(p,input.data()+at,n);at+=n;return n;}
    size_t write(const uint8_t *p,size_t n){output.insert(output.end(),p,p+n);return n;}
    void println(const char *p){write((const uint8_t *)p,std::strlen(p));write((const uint8_t *)"\n",1);}
    void flush(){}
    void updateBaudRate(uint32_t){}
};
inline FakeSerial Serial;
struct Reboot {};
struct FakeESP { [[noreturn]] void restart(){throw Reboot{};} };
inline FakeESP ESP;
