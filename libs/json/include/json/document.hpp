#pragma once
#include "picojson.h"
#include <optional>
#include <stdexcept>
#include <string_view>
namespace kafel::json {
// Bound recursive parsing, reject trailing documents and numeric overflow.
inline std::optional<picojson::value> document(std::string_view text,std::size_t maximum=65536) {
    if(text.size()>maximum) return {};
    unsigned depth=0;bool quoted=false,escaped=false;
    for(char c:text) {
        if(quoted) {
            if(escaped) escaped=false;
            else if(c=='\\') escaped=true;
            else if(c=='"') quoted=false;
        } else if(c=='"') quoted=true;
        else if(c=='{'||c=='[') { if(++depth>32) return {}; }
        else if(c=='}'||c==']') { if(!depth) return {};--depth; }
    }
    if(quoted||depth) return {};
    try {
        picojson::value root;std::string error;
        auto end=picojson::parse(root,text.begin(),text.end(),&error);
        while(end!=text.end()&&(*end==' '||*end=='\t'||*end=='\r'||*end=='\n'))++end;
        if(!error.empty()||end!=text.end()) return {};
        return root;
    } catch(const std::overflow_error&) { return {}; }
}
}
