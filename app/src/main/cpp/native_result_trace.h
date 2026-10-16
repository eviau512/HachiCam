#pragma once
#include <cstdint>
#include <string>
#include <utility>
namespace fusion {
struct NativeResultTrace {
    uintptr_t handle=0;std::string text;
    void publish(uintptr_t id,const std::string& value){handle=id;text=value.substr(0,16384);}
    std::string take(uintptr_t id){
        if(!id||id!=handle)return {};
        handle=0;auto result=std::move(text);text.clear();return result;
    }
    void clear(){handle=0;text.clear();}
};
}
