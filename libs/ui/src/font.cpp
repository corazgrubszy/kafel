#include "ui/font.hpp"
#include "stb/stb_truetype.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace ui {
namespace {
constexpr int samples=4;
char32_t next(std::string_view& text) {
    const auto first=static_cast<unsigned char>(text.front()); text.remove_prefix(1);
    if(first<128) return first;
    unsigned count; char32_t value,minimum;
    if(first>=0xc2&&first<=0xdf) { count=1; value=first&31; minimum=0x80; }
    else if(first>=0xe0&&first<=0xef) { count=2; value=first&15; minimum=0x800; }
    else if(first>=0xf0&&first<=0xf4) { count=3; value=first&7; minimum=0x10000; }
    else throw std::invalid_argument("invalid UTF-8");
    while(count--) {
        if(text.empty()||(static_cast<unsigned char>(text.front())&0xc0)!=0x80) throw std::invalid_argument("invalid UTF-8");
        value=(value<<6)|(static_cast<unsigned char>(text.front())&63); text.remove_prefix(1);
    }
    if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff)) throw std::invalid_argument("invalid UTF-8");
    return value;
}
struct Mask { int width,height; std::vector<unsigned char> pixels; };
void draw_mask(Surface& surface,PixelRect clip,int left,int top,const Mask& mask,Color color) {
    const int x0=static_cast<int>(std::floor(left/static_cast<float>(samples)));
    const int y0=static_cast<int>(std::floor(top/static_cast<float>(samples)));
    const int x1=static_cast<int>(std::ceil((left+mask.width)/static_cast<float>(samples)));
    const int y1=static_cast<int>(std::ceil((top+mask.height)/static_cast<float>(samples)));
    for(int y=std::max(y0,clip.y);y<std::min(y1,clip.bottom());++y)
        for(int x=std::max(x0,clip.x);x<std::min(x1,clip.right());++x) {
            unsigned coverage=0;
            for(int sy=0;sy<samples;++sy) for(int sx=0;sx<samples;++sx) {
                const int mx=x*samples+sx-left,my=y*samples+sy-top;
                if(mx>=0&&my>=0&&mx<mask.width&&my<mask.height) coverage+=mask.pixels[my*mask.width+mx];
            }
            surface.blend(x,y,color,(coverage+8)/16);
        }
}
}
struct TextFont::Impl {
    struct Glyph { char32_t cp; float size,advance; int index,x,y,cap_top; Mask mask; };
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    std::vector<Glyph> glyphs;
    MissingGlyph policy=MissingGlyph::Reject;
    const Glyph& glyph(char32_t cp,float size) const {
        for(const auto& g:glyphs) if(g.cp==cp&&g.size==size) return g;
        if(policy==MissingGlyph::QuestionMark && cp!=U'?') return glyph(U'?',size);
        throw std::invalid_argument("text glyph or size not prepared");
    }
};
TextFont::TextFont(const char* path,std::string_view repertoire,std::span<const TextStyle> styles):impl_(std::make_unique<Impl>()) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    const auto length=file.tellg();
    if(!file||length<=0||length>4*1024*1024) throw std::invalid_argument("cannot read trusted font");
    impl_->bytes.resize(static_cast<std::size_t>(length)); file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(impl_->bytes.data()),length)||!stbtt_InitFont(&impl_->info,impl_->bytes.data(),0)) throw std::invalid_argument("invalid trusted font");
    prepare(repertoire,styles);
}
TextFont::TextFont(std::span<const unsigned char> bytes,std::string_view repertoire,
                   std::span<const TextStyle> styles,MissingGlyph policy):impl_(std::make_unique<Impl>()) {
    if(bytes.empty()||bytes.size()>4*1024*1024) throw std::invalid_argument("invalid trusted font size");
    impl_->bytes.assign(bytes.begin(),bytes.end()); impl_->policy=policy;
    if(!stbtt_InitFont(&impl_->info,impl_->bytes.data(),0)) throw std::invalid_argument("invalid trusted font");
    prepare(repertoire,styles);
    if(policy==MissingGlyph::QuestionMark) for(auto style:styles) (void)impl_->glyph(U'?',style.em_px);
}
void TextFont::prepare(std::string_view repertoire,std::span<const TextStyle> styles) {
    for(const auto style:styles) {
        if(!std::isfinite(style.em_px)||style.em_px<1||style.em_px>128) throw std::invalid_argument("invalid font size");
        auto remaining=repertoire;
        while(!remaining.empty()) {
            const auto cp=next(remaining);
            if(std::any_of(impl_->glyphs.begin(),impl_->glyphs.end(),[&](const auto& g){return g.cp==cp&&g.size==style.em_px;})) continue;
            const int index=stbtt_FindGlyphIndex(&impl_->info,static_cast<int>(cp));
            if(!index) throw std::invalid_argument("missing trusted font glyph");
            const float scale=stbtt_ScaleForMappingEmToPixels(&impl_->info,style.em_px*samples);
            int x,y,right,bottom,advance,cap;
            stbtt_GetGlyphBitmapBox(&impl_->info,index,scale,scale,&x,&y,&right,&bottom);
            stbtt_GetCodepointBitmapBox(&impl_->info,'H',scale,scale,nullptr,&cap,nullptr,nullptr);
            stbtt_GetGlyphHMetrics(&impl_->info,index,&advance,nullptr);
            Impl::Glyph g{cp,style.em_px,advance*scale/samples,index,x,y,cap,{right-x,bottom-y,{}}};
            g.mask.pixels.resize(static_cast<std::size_t>(g.mask.width*g.mask.height));
            if(!g.mask.pixels.empty()) stbtt_MakeGlyphBitmap(&impl_->info,g.mask.pixels.data(),g.mask.width,g.mask.height,g.mask.width,scale,scale,index);
            impl_->glyphs.push_back(std::move(g));
        }
    }
}
TextFont::~TextFont()=default;
float TextFont::measure(std::string_view text,TextStyle style) const {
    float width=0; int previous=0;
    const float scale=stbtt_ScaleForMappingEmToPixels(&impl_->info,style.em_px);
    while(!text.empty()) {
        const auto& g=impl_->glyph(next(text),style.em_px);
        if(previous) width+=stbtt_GetGlyphKernAdvance(&impl_->info,previous,g.index)*scale+style.tracking;
        width+=g.advance; previous=g.index;
    }
    return width;
}
void TextFont::draw(Surface& surface,PixelRect clip,int x,int top,std::string_view text,TextStyle style) const {
    float pen=static_cast<float>(x); int previous=0;
    const float scale=stbtt_ScaleForMappingEmToPixels(&impl_->info,style.em_px);
    while(!text.empty()) {
        const auto& g=impl_->glyph(next(text),style.em_px);
        if(previous) pen+=stbtt_GetGlyphKernAdvance(&impl_->info,previous,g.index)*scale+style.tracking;
        draw_mask(surface,clip,static_cast<int>(std::lround(pen*samples))+g.x,top*samples-g.cap_top+g.y,g.mask,style.color);
        pen+=g.advance; previous=g.index;
    }
}
namespace {
struct Point { float x,y; };
struct Outline {
    std::array<Point,20> points{}; std::size_t count=0;
    Outline(std::initializer_list<Point> vertices) { for(auto p:vertices) points[count++]=p; }
};
bool inside(float x,float y,const Outline& shape) {
    bool result=false;
    for(std::size_t i=0,j=shape.count-1;i<shape.count;j=i++) {
        const auto a=shape.points[i],b=shape.points[j];
        if((a.y>y)!=(b.y>y) && x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x) result=!result;
    }
    return result;
}
Mask number_mask(int digit,float scale) {
    const Outline outer{{3,0},{37,0},{40,3},{40,29},{37,32},{3,32},{0,29},{0,3}};
    const std::array<Outline,10> shapes={
        outer,
        Outline{{14,0},{32,0},{32,25},{40,25},{40,32},{10,32},{10,25},{23,25},{23,7},{9,7}},
        Outline{{0,0},{37,0},{40,3},{40,18},{8,23},{8,25},{40,25},{40,32},{0,32},{0,17},{32,12},{32,7},{0,7}},
        Outline{{0,0},{37,0},{40,3},{40,29},{37,32},{0,32},{0,25},{32,25},{32,20},{9,20},{9,12},{32,12},{32,7},{0,7}},
        Outline{{0,0},{8,0},{8,13},{26,13},{26,0},{34,0},{34,13},{40,13},{40,21},{34,21},{34,32},{26,32},{26,21},{0,21}},
        Outline{{0,0},{40,0},{40,7},{8,7},{8,12},{37,12},{40,15},{40,29},{37,32},{0,32},{0,25},{32,25},{32,20},{0,20}},
        outer,
        Outline{{0,0},{40,0},{40,8},{23,32},{13,32},{30,7},{0,7}},
        outer,outer
    };
    const Outline hole0{{10,7},{30,7},{32,9},{32,23},{30,25},{10,25},{8,23},{8,9}};
    const Outline upper{{8,7},{32,7},{32,12},{8,12}},lower{{8,20},{32,20},{32,25},{8,25}};
    const Outline opening6{{8,7},{40,7},{40,13},{8,13}},opening9{{0,20},{32,20},{32,25},{0,25}};
    Mask mask{static_cast<int>(std::ceil(40*scale*samples)),static_cast<int>(std::ceil(32*scale*samples)),{}};
    mask.pixels.resize(static_cast<std::size_t>(mask.width*mask.height));
    for(int y=0;y<mask.height;++y) for(int x=0;x<mask.width;++x) {
        const float px=(x+.5f)/(scale*samples),py=(y+.5f)/(scale*samples);
        bool covered=inside(px,py,shapes[digit]);
        if(digit==0) covered&=!inside(px,py,hole0);
        if(digit==6) covered&=!inside(px,py,opening6)&&!inside(px,py,lower);
        if(digit==8) covered&=!inside(px,py,upper)&&!inside(px,py,lower);
        if(digit==9) covered&=!inside(px,py,upper)&&!inside(px,py,opening9);
        mask.pixels[y*mask.width+x]=covered?255:0;
    }
    return mask;
}
}
struct NumberFont::Impl {
    struct Size { float scale; std::array<Mask,10> digits; };
    std::vector<Size> sizes;
};
NumberFont::NumberFont(std::span<const NumberStyle> styles):impl_(std::make_unique<Impl>()) {
    for(auto style:styles) {
        if(!std::isfinite(style.scale)||style.scale<=0||style.scale>4) throw std::invalid_argument("invalid number scale");
        if(std::any_of(impl_->sizes.begin(),impl_->sizes.end(),[&](const auto& s){return s.scale==style.scale;})) continue;
        Impl::Size size{}; size.scale=style.scale;
        for(int i=0;i<10;++i) size.digits[i]=number_mask(i,style.scale);
        impl_->sizes.push_back(std::move(size));
    }
}
float NumberFont::measure(std::string_view text,NumberStyle style) {
    float width=0;
    for(char c:text) {
        if(c>='0'&&c<='9') width+=45*style.scale;
        else if(c=='-' ) width+=27*style.scale;
        else if(c=='.'||c==':') width+=13*style.scale;
        else throw std::invalid_argument("unsupported number character");
    }
    return width;
}
NumberFont::~NumberFont()=default;
float NumberFont::draw(Surface& surface,PixelRect clip,int x,int y,std::string_view text,NumberStyle style) const {
    const auto found=std::find_if(impl_->sizes.begin(),impl_->sizes.end(),[&](const auto& s){return s.scale==style.scale;});
    if(found==impl_->sizes.end()) throw std::invalid_argument("number size not prepared");
    float pen=static_cast<float>(x);
    for(char c:text) {
        if(c==':') {
            const int side=static_cast<int>(std::lround(6*style.scale));
            for(int dy:{8,21}) {
                const PixelRect dot{static_cast<int>(std::lround(pen)),y+static_cast<int>(std::lround(dy*style.scale)),side,side};
                const int left=std::max(dot.x,clip.x),top=std::max(dot.y,clip.y);
                surface.rectangle({left,top,std::max(0,std::min(dot.right(),clip.right())-left),std::max(0,std::min(dot.bottom(),clip.bottom())-top)},style.color);
            }
            pen+=13*style.scale;
        } else if(c=='-'||c=='.') {
            const int w=static_cast<int>(std::lround((c=='-'?22:6)*style.scale));
            const int h=std::max(1,static_cast<int>(std::lround((c=='-'?4:6)*style.scale)));
            const PixelRect mark{static_cast<int>(std::lround(pen)),y+static_cast<int>(std::lround((c=='-'?14:26)*style.scale)),w,h};
            const int left=std::max(mark.x,clip.x),top=std::max(mark.y,clip.y);
            surface.rectangle({left,top,std::max(0,std::min(mark.right(),clip.right())-left),std::max(0,std::min(mark.bottom(),clip.bottom())-top)},style.color);
            pen+=(c=='-'?27:13)*style.scale;
        } else {
            if(c<'0'||c>'9') throw std::invalid_argument("unsupported number character");
            draw_mask(surface,clip,static_cast<int>(std::lround(pen*samples)),y*samples,found->digits[c-'0'],style.color);
            pen+=45*style.scale;
        }
    }
    return pen;
}
}
