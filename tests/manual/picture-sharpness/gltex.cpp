// Draws a texture the way Lumen's canvas does (RGBA8, optional full mip chain, linear mag) into an
// FBO of the size it has on screen, with no window system: EGL surfaceless + GLES 3.
// gltex <in.rgba> <tw> <th> <out.rgba> <fbw> <fbh> <x> <y> <w> <h> <mip|nomip|nearest>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
int main(int argc, char **argv)
{
    if (argc < 12) return 2;
    const int tw = atoi(argv[2]), th = atoi(argv[3]), fw = atoi(argv[5]), fh = atoi(argv[6]);
    const float x = atof(argv[7]), y = atof(argv[8]), w = atof(argv[9]), h = atof(argv[10]);
    const char *mode = argv[11];
    std::vector<unsigned char> tex(size_t(tw) * th * 4);
    FILE *f = fopen(argv[1], "rb"); if (!f || fread(tex.data(), 1, tex.size(), f) != tex.size()) return 3; fclose(f);
    EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (!eglInitialize(dpy, nullptr, nullptr)) { fprintf(stderr, "no egl\n"); return 4; }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n = 0; eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, n ? cfg : EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ctxAttr);
    if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { fprintf(stderr, "no context\n"); return 5; }
    GLint maxTex = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    fprintf(stderr, "renderer: %s | max texture %d\n", glGetString(GL_RENDERER), maxTex);
    GLuint fbo, col; glGenFramebuffers(1, &fbo); glGenTextures(1, &col);
    glBindTexture(GL_TEXTURE_2D, col); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, col, 0);
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (!strcmp(mode, "mip")) { glGenerateMipmap(GL_TEXTURE_2D); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); }
    else if (!strcmp(mode, "nomip")) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); }
    else { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); }
    const char *vs = "#version 300 es\nin vec2 p; in vec2 uv; out vec2 v; void main(){ v = uv; gl_Position = vec4(p, 0.0, 1.0); }";
    const char *fs = "#version 300 es\nprecision highp float; in vec2 v; out vec4 c; uniform sampler2D s; void main(){ c = texture(s, v); }";
    GLuint a = glCreateShader(GL_VERTEX_SHADER); glShaderSource(a, 1, &vs, nullptr); glCompileShader(a);
    GLuint b = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(b, 1, &fs, nullptr); glCompileShader(b);
    GLuint pr = glCreateProgram(); glAttachShader(pr, a); glAttachShader(pr, b); glBindAttribLocation(pr, 0, "p"); glBindAttribLocation(pr, 1, "uv"); glLinkProgram(pr); glUseProgram(pr);
    auto nx = [&](float v) { return v / fw * 2 - 1; }; auto ny = [&](float v) { return v / fh * 2 - 1; };
    const float verts[] = {nx(x), ny(y), 0, 0, nx(x + w), ny(y), 1, 0, nx(x), ny(y + h), 0, 1, nx(x + w), ny(y + h), 1, 1};
    glViewport(0, 0, fw, fh); glClearColor(1, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, verts); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, verts + 2);
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    std::vector<unsigned char> out(size_t(fw) * fh * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    f = fopen(argv[4], "wb"); fwrite(out.data(), 1, out.size(), f); fclose(f);
    return glGetError() == GL_NO_ERROR ? 0 : 6;
}
