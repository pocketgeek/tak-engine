#pragma once
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>
#include <cstdint>
#include <vector>
#include "client/gpuvram.h"

namespace tak {
// SDL's compatibility OpenGL backend copies/reformats every geometry vertex.
// Large already-packed batches can use client arrays directly. Other renderers,
// non-normalized textures and fractional output scales retain the SDL path.
class GeometrySubmit {
public:
    GeometrySubmit()=default;
    GeometrySubmit(const GeometrySubmit&)=delete;
    GeometrySubmit& operator=(const GeometrySubmit&)=delete;
    ~GeometrySubmit() { clear(); }
    void clear() {
        if(buffer_ && SDL_GL_GetCurrentContext()==context_ && deleteBuffers_)deleteBuffers_(1,&buffer_);
        gpuvram::g_bytes-=bufferBytes_;bufferBytes_=0;buffer_=0;revision_=0;seenRevision_=0;
    }
    uint64_t uploads() const { return uploads_; }
    // A nonzero revision retains this mesh on compatible OpenGL backends.
    // Callers increment it whenever any vertex changes; zero keeps streaming.
    int draw(SDL_Renderer* renderer,SDL_Texture* texture,std::span<const SDL_Vertex> vertices,bool direct=true,uint64_t revision=0,SDL_FPoint offset={0,0}) {
        if(vertices.empty())return 0;
        auto normal=[&] {
            auto source=vertices;
            if(offset.x!=0 || offset.y!=0) {
                translated_.assign(vertices.begin(),vertices.end());
                for(auto& v:translated_){v.position.x+=offset.x;v.position.y+=offset.y;}
                source=translated_;
            }
            return SDL_RenderGeometry(renderer,texture,source.data(),int(source.size()),nullptr,0);
        };
        if(!direct || vertices.size()<512)return normal();
        SDL_RendererInfo info{};
        if(SDL_GetRendererInfo(renderer,&info)!=0 || !info.name || std::strcmp(info.name,"opengl"))return normal();
        float sx=1,sy=1;SDL_RenderGetScale(renderer,&sx,&sy);
        // Exact integer AA scales share the same projection setup, including
        // compact 16x model targets. Fractional sampling retains the SDL path.
        if((sx!=1 && sx!=2 && sx!=4) || (sy!=1 && sy!=2 && sy!=4))return normal();
        const auto context=SDL_GL_GetCurrentContext();
        if(!checked_ || context!=context_) { clear();checked_=true;context_=context;available_=load(); }
        if(!available_)return normal();
        if(texture) {
            float tw=0,th=0;
            if(SDL_GL_BindTexture(texture,&tw,&th)!=0)return normal();
            SDL_GL_UnbindTexture(texture);
            if(tw!=1 || th!=1)return normal();
        }
        // The degenerate SDL draw installs the exact shader, blend, target,
        // viewport and scissor state without changing any pixels.
        const SDL_Vertex prime[3]={{{0,0},{255,255,255,255},{0,0}},
                                  {{0,0},{255,255,255,255},{0,0}},
                                  {{0,0},{255,255,255,255},{0,0}}};
        if(SDL_RenderGeometry(renderer,texture,prime,3,nullptr,0)!=0 || SDL_RenderFlush(renderer)!=0)return -1;
        GLint buffer=0,mode=0;
        getInteger_(GL_ARRAY_BUFFER_BINDING,&buffer);getInteger_(GL_MATRIX_MODE,&mode);
        pushAttrib_(GL_CURRENT_BIT);pushClientAttrib_(GL_CLIENT_VERTEX_ARRAY_BIT);
        bool retained=false;
        const size_t bytes=vertices.size_bytes();
        // Moving cameras rebuild every frame. Do not allocate a new persistent
        // buffer until the mesh actually survives into a second draw.
        const bool stable=revision && revision==seenRevision_;
        if(buffer_ && (revision_!=revision || bufferBytes_!=bytes))clear();
        seenRevision_=revision;
        if(stable && genBuffers_ && bytes<=(size_t(64)<<20)) {
            if(!buffer_ && !gpuvram::blocked() && gpuvram::wouldFit(bytes)) {
                genBuffers_(1,&buffer_);bindBuffer_(GL_ARRAY_BUFFER,buffer_);
                bufferData_(GL_ARRAY_BUFFER,GLsizeiptr(bytes),vertices.data(),GL_STATIC_DRAW);
                GLint allocated=0;getBufferParameter_(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&allocated);
                if(size_t(allocated)==bytes) {
                    bufferBytes_=bytes;gpuvram::g_bytes+=bytes;revision_=revision;++uploads_;
                } else {clear();gpuvram::noteFail();}
            }
            retained=buffer_ && revision_==revision;
        }
        bindBuffer_(GL_ARRAY_BUFFER,retained?buffer_:0);
        matrixMode_(GL_MODELVIEW);pushMatrix_();scale_(sx,sy,1);translate_(offset.x,offset.y,0);
        enable_(GL_VERTEX_ARRAY);enable_(GL_COLOR_ARRAY);
        if(texture)enable_(GL_TEXTURE_COORD_ARRAY);else disable_(GL_TEXTURE_COORD_ARRAY);
        vertex_(2,GL_FLOAT,sizeof(SDL_Vertex),retained?reinterpret_cast<const void*>(offsetof(SDL_Vertex,position)):&vertices[0].position);
        color_(4,GL_UNSIGNED_BYTE,sizeof(SDL_Vertex),retained?reinterpret_cast<const void*>(offsetof(SDL_Vertex,color)):&vertices[0].color);
        if(texture)texcoord_(2,GL_FLOAT,sizeof(SDL_Vertex),retained?reinterpret_cast<const void*>(offsetof(SDL_Vertex,tex_coord)):&vertices[0].tex_coord);
        draw_(GL_TRIANGLES,0,GLsizei(vertices.size()));
        popMatrix_();matrixMode_(GLenum(mode));
        popClientAttrib_();bindBuffer_(GL_ARRAY_BUFFER,GLuint(buffer));popAttrib_();
        used_=true;
        return 0;
    }
    bool active() const { return used_; }
private:
    template<class T> static bool proc(T& out,const char* name) {
        out=reinterpret_cast<T>(SDL_GL_GetProcAddress(name));return out!=nullptr;
    }
    bool load() {
        if(!proc(getString_,"glGetString") || !proc(getInteger_,"glGetIntegerv") ||
           !proc(pushAttrib_,"glPushAttrib") || !proc(popAttrib_,"glPopAttrib") ||
           !proc(pushClientAttrib_,"glPushClientAttrib") || !proc(popClientAttrib_,"glPopClientAttrib") ||
           !proc(bindBuffer_,"glBindBuffer") || !proc(enable_,"glEnableClientState") ||
           !proc(disable_,"glDisableClientState") || !proc(vertex_,"glVertexPointer") ||
           !proc(color_,"glColorPointer") || !proc(texcoord_,"glTexCoordPointer") ||
           !proc(matrixMode_,"glMatrixMode") || !proc(pushMatrix_,"glPushMatrix") ||
           !proc(popMatrix_,"glPopMatrix") || !proc(translate_,"glTranslatef") || !proc(scale_,"glScalef") || !proc(draw_,"glDrawArrays"))return false;
        // Optional retained buffers do not affect the existing direct fallback.
        if(!proc(genBuffers_,"glGenBuffers") || !proc(deleteBuffers_,"glDeleteBuffers") ||
           !proc(bufferData_,"glBufferData") || !proc(getBufferParameter_,"glGetBufferParameteriv"))genBuffers_=nullptr;
        const auto* version=reinterpret_cast<const char*>(getString_(GL_VERSION));
        return version && (std::atoi(version)<3 || SDL_GL_ExtensionSupported("GL_ARB_compatibility"));
    }
    std::vector<SDL_Vertex> translated_;
    GLuint buffer_=0;
    size_t bufferBytes_=0;
    uint64_t revision_=0,seenRevision_=0,uploads_=0;
    void (APIENTRY *genBuffers_)(GLsizei,GLuint*)=nullptr;
    void (APIENTRY *deleteBuffers_)(GLsizei,const GLuint*)=nullptr;
    void (APIENTRY *bufferData_)(GLenum,GLsizeiptr,const void*,GLenum)=nullptr;
    void (APIENTRY *getBufferParameter_)(GLenum,GLenum,GLint*)=nullptr;
    bool checked_=false,available_=false,used_=false;
    SDL_GLContext context_=nullptr;
    const GLubyte* (APIENTRY *getString_)(GLenum)=nullptr;
    void (APIENTRY *getInteger_)(GLenum,GLint*)=nullptr;
    void (APIENTRY *pushAttrib_)(GLbitfield)=nullptr;
    void (APIENTRY *popAttrib_)()=nullptr;
    void (APIENTRY *pushClientAttrib_)(GLbitfield)=nullptr;
    void (APIENTRY *popClientAttrib_)()=nullptr;
    void (APIENTRY *bindBuffer_)(GLenum,GLuint)=nullptr;
    void (APIENTRY *enable_)(GLenum)=nullptr;
    void (APIENTRY *disable_)(GLenum)=nullptr;
    void (APIENTRY *vertex_)(GLint,GLenum,GLsizei,const GLvoid*)=nullptr;
    void (APIENTRY *color_)(GLint,GLenum,GLsizei,const GLvoid*)=nullptr;
    void (APIENTRY *texcoord_)(GLint,GLenum,GLsizei,const GLvoid*)=nullptr;
    void (APIENTRY *matrixMode_)(GLenum)=nullptr;
    void (APIENTRY *pushMatrix_)()=nullptr;
    void (APIENTRY *popMatrix_)()=nullptr;
    void (APIENTRY *translate_)(GLfloat,GLfloat,GLfloat)=nullptr;
    void (APIENTRY *scale_)(GLfloat,GLfloat,GLfloat)=nullptr;
    void (APIENTRY *draw_)(GLenum,GLint,GLsizei)=nullptr;
};
}
