#pragma once
#include <SDL.h>
#include <algorithm>
#include <string>

namespace cart {
// Positions are UTF-8 byte offsets at code-point boundaries. All storage and
// clipboard operations preserve Unicode even if a chosen UI font lacks a glyph.
struct TextEdit {
    size_t caret=0,anchor=0;
    static size_t previous(const std::string& s,size_t p) {
        if(!p)return 0;
        --p;while(p && (uint8_t(s[p])&0xc0)==0x80)--p;return p;
    }
    static size_t next(const std::string& s,size_t p) {
        if(p>=s.size())return s.size();
        ++p;while(p<s.size() && (uint8_t(s[p])&0xc0)==0x80)++p;return p;
    }
    void focus(const std::string& s,bool select=true) {caret=s.size();anchor=select?0:caret;}
    bool selection() const {return caret!=anchor;}
    void erase(std::string& s) {const auto first=std::min(caret,anchor);s.erase(first,std::max(caret,anchor)-first);caret=anchor=first;}
    bool input(const SDL_Event& e,std::string& s,bool numeric=false,bool multiline=false) {
        caret=std::min(caret,s.size());anchor=std::min(anchor,s.size());
        const auto original=s;
        auto insert=[&](std::string text) {
            text.erase(std::remove_if(text.begin(),text.end(),[&](unsigned char c) {
                return numeric ? !(c>='0' && c<='9') && c!='-' : c<32 && !(multiline && c=='\n');
            }),text.end());
            if(s.size()+text.size()>65536)return;
            erase(s);s.insert(caret,text);caret+=text.size();anchor=caret;
        };
        if(e.type==SDL_TEXTINPUT)insert(e.text.text);
        else if(e.type==SDL_KEYDOWN) {
            const auto key=e.key.keysym.sym;const bool ctrl=e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI),shift=e.key.keysym.mod&KMOD_SHIFT;
            if(ctrl && key==SDLK_a) {anchor=0;caret=s.size();}
            else if(ctrl && (key==SDLK_c || key==SDLK_x)) {
                if(selection()) {SDL_SetClipboardText(s.substr(std::min(caret,anchor),std::max(caret,anchor)-std::min(caret,anchor)).c_str());if(key==SDLK_x)erase(s);}
            } else if(ctrl && key==SDLK_v) {if(char* text=SDL_GetClipboardText()) {insert(text);SDL_free(text);}}
            else if(key==SDLK_BACKSPACE) {if(!selection())anchor=previous(s,caret);erase(s);}
            else if(key==SDLK_DELETE) {if(!selection())anchor=next(s,caret);erase(s);}
            else if(key==SDLK_LEFT || key==SDLK_RIGHT || key==SDLK_HOME || key==SDLK_END) {
                if(key==SDLK_HOME)caret=0;else if(key==SDLK_END)caret=s.size();
                else if(selection() && !shift)caret=key==SDLK_LEFT?std::min(caret,anchor):std::max(caret,anchor);
                else caret=key==SDLK_LEFT?previous(s,caret):next(s,caret);
                if(!shift)anchor=caret;
            } else if(multiline && key==SDLK_RETURN && shift)insert("\n");
        }
        return s!=original;
    }
};
} // namespace cart
