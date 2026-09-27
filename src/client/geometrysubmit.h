#pragma once
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>

namespace tak {
// SDL's compatibility OpenGL backend copies/reformats every geometry vertex.
// Large already-packed batches can use client arrays directly. Other renderers,
// non-normalized textures and fractional output scales retain the SDL path.
class GeometrySubmit {
public:
    int draw(SDL_Renderer* renderer,SDL_Texture* texture,std::span<const SDL_Vertex> vertices,bool direct=true) {
        if(vertices.empty())return 0;
        auto normal=[&] { return SDL_RenderGeometry(renderer,texture,vertices.data(),int(vertices.size()),nullptr,0); };
        if(!direct || vertices.size()<512)return normal();
        SDL_RendererInfo info{};
        if(SDL_GetRendererInfo(renderer,&info)!=0 || !info.name || std::strcmp(info.name,"opengl"))return normal();
        float sx=1,sy=1;SDL_RenderGetScale(renderer,&sx,&sy);
        if((sx!=1 && sx!=2) || (sy!=1 && sy!=2))return normal();
        const auto context=SDL_GL_GetCurrentContext();
        if(!checked_ || context!=context_) { checked_=true;context_=context;available_=load(); }
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
        bindBuffer_(GL_ARRAY_BUFFER,0);
        matrixMode_(GL_MODELVIEW);pushMatrix_();scale_(sx,sy,1);
        enable_(GL_VERTEX_ARRAY);enable_(GL_COLOR_ARRAY);
        if(texture)enable_(GL_TEXTURE_COORD_ARRAY);else disable_(GL_TEXTURE_COORD_ARRAY);
        vertex_(2,GL_FLOAT,sizeof(SDL_Vertex),&vertices[0].position);
        color_(4,GL_UNSIGNED_BYTE,sizeof(SDL_Vertex),&vertices[0].color);
        if(texture)texcoord_(2,GL_FLOAT,sizeof(SDL_Vertex),&vertices[0].tex_coord);
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
           !proc(popMatrix_,"glPopMatrix") || !proc(scale_,"glScalef") || !proc(draw_,"glDrawArrays"))return false;
        const auto* version=reinterpret_cast<const char*>(getString_(GL_VERSION));
        return version && (std::atoi(version)<3 || SDL_GL_ExtensionSupported("GL_ARB_compatibility"));
    }
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
    void (APIENTRY *scale_)(GLfloat,GLfloat,GLfloat)=nullptr;
    void (APIENTRY *draw_)(GLenum,GLint,GLsizei)=nullptr;
};
}
