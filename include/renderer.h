#ifndef RENDERER_H
#define RENDERER_H

#include <SDL.h>
#include <array>
#include <vector>

#ifdef TG5040_PLATFORM
#include <SDL_opengles2.h>
using GLShaderHandle = GLuint;
#else
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include <SDL_opengl.h>
#endif
using GLShaderHandle = GLuint;
#endif

class Renderer
{
public:
    Renderer(SDL_Window* window);
    ~Renderer();

    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void present();

    void renderPageEx(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight, double angleDeg, SDL_RendererFlip flip);
    void renderPageExARGB(const std::vector<uint32_t>& argbData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight, double angleDeg, SDL_RendererFlip flip, const void* bufferToken = nullptr);

    void drawARGBImage(const uint32_t* argbData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight, bool flipX = false, bool flipY = false);
    void drawFilledRect(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void drawRect(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void drawLine(float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

    int getWindowWidth() const;
    int getWindowHeight() const;

    void toggleFullscreen();

    // Static method to get required SDL initialization flags
    static Uint32 getRequiredSDLInitFlags();

private:
    SDL_Window* m_window;
    GLuint m_textureId = 0;
    int m_currentTexWidth = 0;
    int m_currentTexHeight = 0;
    bool m_isFullscreen = false;
    const void* m_lastBufferToken = nullptr;
    int m_lastBufferWidth = 0;
    int m_lastBufferHeight = 0;

    GLShaderHandle m_textureProgram = 0;
    GLShaderHandle m_colorProgram = 0;
    GLint m_texUniformSampler = -1;
    GLint m_texUniformMVP = -1;
    GLint m_texUniformTint = -1;
    GLint m_colorUniformMVP = -1;
    GLint m_colorUniformColor = -1;
    GLint m_texAttribPos = -1;
    GLint m_texAttribUV = -1;
    GLint m_colorAttribPos = -1;

    GLuint m_vertexBuffer = 0;
    GLuint m_indexBuffer = 0;
#ifndef TG5040_PLATFORM
    GLuint m_vertexArray = 0;
#endif

    std::array<float, 16> m_projection{};
    std::vector<uint32_t> m_uploadBuffer;

    void ensureBuffers();
    void updateProjection();
    void ensureTextureCapacity(int width, int height);
    void uploadTexture(const uint32_t* argbData, int srcWidth, int srcHeight);
    void drawQuad(float x, float y, float width, float height, bool flipX, bool flipY, const float* uv = nullptr);
    void drawColoredQuad(float x, float y, float width, float height, float r, float g, float b, float a, bool outline);
    void drawColoredLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a);
    void setBlendMode();
    static GLShaderHandle compileShader(GLenum type, const char* source);
    static GLShaderHandle createProgram(const char* vertexSrc, const char* fragmentSrc);
};

#endif // RENDERER_H
