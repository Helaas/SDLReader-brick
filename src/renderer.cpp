#include "renderer.h"
#include "document.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr const char* kTextureVertexShaderES = R"(#version 100
attribute vec2 aPos;
attribute vec2 aUV;
uniform mat4 uMVP;
varying vec2 vUV;
void main()
{
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kTextureFragmentShaderES = R"(#version 100
precision mediump float;
varying vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uTint;
void main()
{
    vec4 color = texture2D(uTexture, vUV);
    gl_FragColor = color * uTint;
}
)";

constexpr const char* kTextureVertexShaderDesktop = R"(#version 150 core
in vec2 aPos;
in vec2 aUV;
uniform mat4 uMVP;
out vec2 vUV;
void main()
{
    vUV = aUV;
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kTextureFragmentShaderDesktop = R"(#version 150 core
in vec2 vUV;
uniform sampler2D uTexture;
uniform vec4 uTint;
out vec4 FragColor;
void main()
{
    vec4 color = texture(uTexture, vUV);
    FragColor = color * uTint;
}
)";

constexpr const char* kColorVertexShaderES = R"(#version 100
attribute vec2 aPos;
uniform mat4 uMVP;
void main()
{
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kColorFragmentShaderES = R"(#version 100
precision mediump float;
uniform vec4 uColor;
void main()
{
    gl_FragColor = uColor;
}
)";

constexpr const char* kColorVertexShaderDesktop = R"(#version 150 core
in vec2 aPos;
uniform mat4 uMVP;
void main()
{
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* kColorFragmentShaderDesktop = R"(#version 150 core
uniform vec4 uColor;
out vec4 FragColor;
void main()
{
    FragColor = uColor;
}
)";

struct VertexPT
{
    float pos[2];
    float uv[2];
};

struct VertexP
{
    float pos[2];
};

inline std::array<float, 16> makeOrthographic(float width, float height)
{
    const float wInv = 1.0f / width;
    const float hInv = 1.0f / height;
    return {2.0f * wInv, 0.0f, 0.0f, 0.0f,
            0.0f, -2.0f * hInv, 0.0f, 0.0f,
            0.0f, 0.0f, -1.0f, 0.0f,
            -1.0f, 1.0f, 0.0f, 1.0f};
}

} // namespace

Renderer::Renderer(SDL_Window* window)
    : m_window(window)
{
    if (!m_window)
    {
        throw std::runtime_error("Renderer received a null SDL_Window pointer.");
    }

#ifdef TG5040_PLATFORM
    m_textureProgram = createProgram(kTextureVertexShaderES, kTextureFragmentShaderES);
    m_colorProgram = createProgram(kColorVertexShaderES, kColorFragmentShaderES);
#else
    m_textureProgram = createProgram(kTextureVertexShaderDesktop, kTextureFragmentShaderDesktop);
    m_colorProgram = createProgram(kColorVertexShaderDesktop, kColorFragmentShaderDesktop);
#endif

    if (!m_textureProgram || !m_colorProgram)
    {
        throw std::runtime_error("Failed to create OpenGL shader programs.");
    }

    m_texAttribPos = glGetAttribLocation(m_textureProgram, "aPos");
    m_texAttribUV = glGetAttribLocation(m_textureProgram, "aUV");
    m_texUniformSampler = glGetUniformLocation(m_textureProgram, "uTexture");
    m_texUniformMVP = glGetUniformLocation(m_textureProgram, "uMVP");
    m_texUniformTint = glGetUniformLocation(m_textureProgram, "uTint");

    m_colorAttribPos = glGetAttribLocation(m_colorProgram, "aPos");
    m_colorUniformMVP = glGetUniformLocation(m_colorProgram, "uMVP");
    m_colorUniformColor = glGetUniformLocation(m_colorProgram, "uColor");

    ensureBuffers();
    setBlendMode();
    updateProjection();
}

Renderer::~Renderer()
{
    if (m_textureId != 0)
    {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    if (m_vertexBuffer != 0)
    {
        glDeleteBuffers(1, &m_vertexBuffer);
        m_vertexBuffer = 0;
    }
    if (m_indexBuffer != 0)
    {
        glDeleteBuffers(1, &m_indexBuffer);
        m_indexBuffer = 0;
    }
    if (m_textureProgram)
    {
        glDeleteProgram(m_textureProgram);
        m_textureProgram = 0;
    }
    if (m_colorProgram)
    {
        glDeleteProgram(m_colorProgram);
        m_colorProgram = 0;
    }
}

void Renderer::ensureBuffers()
{
    if (m_vertexBuffer == 0)
    {
        glGenBuffers(1, &m_vertexBuffer);
    }
    if (m_indexBuffer == 0)
    {
        glGenBuffers(1, &m_indexBuffer);
        const GLushort indices[6] = {0, 1, 2, 0, 2, 3};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    }
}

void Renderer::setBlendMode()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::updateProjection()
{
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(m_window, &width, &height);
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    m_projection = makeOrthographic(static_cast<float>(width), static_cast<float>(height));
}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    updateProjection();
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(m_window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::present()
{
    SDL_GL_SwapWindow(m_window);
}

void Renderer::ensureTextureCapacity(int width, int height)
{
    if (m_textureId == 0)
    {
        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, m_textureId);
    }

    if (width > m_currentTexWidth || height > m_currentTexHeight)
    {
        m_currentTexWidth = width;
        m_currentTexHeight = height;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_currentTexWidth, m_currentTexHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
}

void Renderer::uploadTexture(const uint32_t* argbData, int srcWidth, int srcHeight)
{
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const size_t pixelCount = static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight);
    m_uploadBuffer.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        uint32_t argb = argbData[i];
        uint8_t a = static_cast<uint8_t>((argb >> 24) & 0xFF);
        uint8_t r = static_cast<uint8_t>((argb >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((argb >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(argb & 0xFF);
        m_uploadBuffer[i] = (static_cast<uint32_t>(a) << 24) |
                            (static_cast<uint32_t>(b) << 16) |
                            (static_cast<uint32_t>(g) << 8) |
                            static_cast<uint32_t>(r);
    }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, srcWidth, srcHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_uploadBuffer.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

void Renderer::drawQuad(float x, float y, float width, float height, bool flipX, bool flipY, const float* customUV)
{
    const float left = x;
    const float right = x + width;
    const float top = y;
    const float bottom = y + height;

    std::array<VertexPT, 4> vertices;

    vertices[0].pos[0] = left;
    vertices[0].pos[1] = top;
    vertices[1].pos[0] = right;
    vertices[1].pos[1] = top;
    vertices[2].pos[0] = right;
    vertices[2].pos[1] = bottom;
    vertices[3].pos[0] = left;
    vertices[3].pos[1] = bottom;

    std::array<float, 8> uv = {0.0f, 0.0f,
                               1.0f, 0.0f,
                               1.0f, 1.0f,
                               0.0f, 1.0f};

    if (customUV)
    {
        std::copy(customUV, customUV + 8, uv.begin());
    }

    if (flipX)
    {
        std::swap(uv[0], uv[2]);
        std::swap(uv[4], uv[6]);
    }
    if (flipY)
    {
        std::swap(uv[1], uv[5]);
        std::swap(uv[3], uv[7]);
    }

    for (int i = 0; i < 4; ++i)
    {
        vertices[i].uv[0] = uv[i * 2 + 0];
        vertices[i].uv[1] = uv[i * 2 + 1];
    }

    glUseProgram(m_textureProgram);
    glUniformMatrix4fv(m_texUniformMVP, 1, GL_FALSE, m_projection.data());
    glUniform1i(m_texUniformSampler, 0);
    glUniform4f(m_texUniformTint, 1.0f, 1.0f, 1.0f, 1.0f);

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPT) * vertices.size(), vertices.data(), GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glEnableVertexAttribArray(m_texAttribPos);
    glVertexAttribPointer(m_texAttribPos, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPT), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(m_texAttribUV);
    glVertexAttribPointer(m_texAttribUV, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPT), reinterpret_cast<void*>(sizeof(float) * 2));

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);

    glDisableVertexAttribArray(m_texAttribPos);
    glDisableVertexAttribArray(m_texAttribUV);
}

void Renderer::drawColoredQuad(float x, float y, float width, float height, float r, float g, float b, float a, bool outline)
{
    std::array<VertexP, 4> vertices;
    vertices[0].pos[0] = x;
    vertices[0].pos[1] = y;
    vertices[1].pos[0] = x + width;
    vertices[1].pos[1] = y;
    vertices[2].pos[0] = x + width;
    vertices[2].pos[1] = y + height;
    vertices[3].pos[0] = x;
    vertices[3].pos[1] = y + height;

    glUseProgram(m_colorProgram);
    glUniformMatrix4fv(m_colorUniformMVP, 1, GL_FALSE, m_projection.data());
    glUniform4f(m_colorUniformColor, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexP) * vertices.size(), vertices.data(), GL_STREAM_DRAW);

    glEnableVertexAttribArray(m_colorAttribPos);
    glVertexAttribPointer(m_colorAttribPos, 2, GL_FLOAT, GL_FALSE, sizeof(VertexP), reinterpret_cast<void*>(0));

    if (outline)
    {
        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }
    else
    {
        static const GLushort indices[6] = {0, 1, 2, 0, 2, 3};
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    }

    glDisableVertexAttribArray(m_colorAttribPos);
}

void Renderer::drawColoredLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a)
{
    VertexP verts[2];
    verts[0].pos[0] = x1;
    verts[0].pos[1] = y1;
    verts[1].pos[0] = x2;
    verts[1].pos[1] = y2;

    glUseProgram(m_colorProgram);
    glUniformMatrix4fv(m_colorUniformMVP, 1, GL_FALSE, m_projection.data());
    glUniform4f(m_colorUniformColor, r, g, b, a);

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(m_colorAttribPos);
    glVertexAttribPointer(m_colorAttribPos, 2, GL_FLOAT, GL_FALSE, sizeof(VertexP), reinterpret_cast<void*>(0));
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(m_colorAttribPos);
}

void Renderer::renderPageEx(const std::vector<uint8_t>& pixelData,
                            int srcWidth, int srcHeight,
                            int destX, int destY, int destWidth, int destHeight,
                            double angleDeg, SDL_RendererFlip flip)
{
    if (pixelData.empty() || srcWidth == 0 || srcHeight == 0)
    {
        std::cerr << "Warning: Attempted to render empty or zero-dimension pixel data." << std::endl;
        return;
    }

    std::vector<uint32_t> temp(static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight));
    for (int y = 0; y < srcHeight; ++y)
    {
        const uint8_t* srcRow = pixelData.data() + static_cast<size_t>(y) * srcWidth * 3;
        uint32_t* dst = temp.data() + static_cast<size_t>(y) * srcWidth;
        for (int x = 0; x < srcWidth; ++x)
        {
            dst[x] = rgb24_to_argb32(srcRow[x * 3], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
        }
    }

    renderPageExARGB(temp, srcWidth, srcHeight, destX, destY, destWidth, destHeight, angleDeg, flip, nullptr);
}

void Renderer::drawARGBImage(const uint32_t* argbData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight, bool flipX, bool flipY)
{
    ensureTextureCapacity(srcWidth, srcHeight);
    uploadTexture(argbData, srcWidth, srcHeight);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    drawQuad(static_cast<float>(destX), static_cast<float>(destY), static_cast<float>(destWidth), static_cast<float>(destHeight), flipX, flipY);
}

void Renderer::renderPageExARGB(const std::vector<uint32_t>& argbData,
                                int srcWidth, int srcHeight,
                                int destX, int destY, int destWidth, int destHeight,
                                double angleDeg, SDL_RendererFlip flip, const void* bufferToken)
{
    if (argbData.empty() || srcWidth == 0 || srcHeight == 0)
    {
        std::cerr << "Warning: Attempted to render empty or zero-dimension ARGB data." << std::endl;
        return;
    }

    ensureTextureCapacity(srcWidth, srcHeight);

    bool needsUpload = bufferToken == nullptr || bufferToken != m_lastBufferToken ||
                       srcWidth != m_lastBufferWidth || srcHeight != m_lastBufferHeight;

    if (needsUpload)
    {
        uploadTexture(argbData.data(), srcWidth, srcHeight);
        m_lastBufferToken = bufferToken;
        m_lastBufferWidth = srcWidth;
        m_lastBufferHeight = srcHeight;
    }

    const bool flipX = (flip & SDL_FLIP_HORIZONTAL) != 0;
    const bool flipY = (flip & SDL_FLIP_VERTICAL) != 0;

    float cx = destX + destWidth * 0.5f;
    float cy = destY + destHeight * 0.5f;
    float hw = destWidth * 0.5f;
    float hh = destHeight * 0.5f;

    constexpr float PI = 3.14159265358979323846f;
    const float radians = static_cast<float>(angleDeg * (PI / 180.0f));
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    std::array<VertexPT, 4> vertices;
    std::array<std::array<float, 2>, 4> corners = {{
        {-hw, -hh},
        { hw, -hh},
        { hw,  hh},
        {-hw,  hh},
    }};

    for (int i = 0; i < 4; ++i)
    {
        float rx = corners[i][0] * c - corners[i][1] * s;
        float ry = corners[i][0] * s + corners[i][1] * c;
        vertices[i].pos[0] = cx + rx;
        vertices[i].pos[1] = cy + ry;
    }

    std::array<float, 8> uv = {0.0f, 0.0f,
                               1.0f, 0.0f,
                               1.0f, 1.0f,
                               0.0f, 1.0f};

    if (flipX)
    {
        std::swap(uv[0], uv[2]);
        std::swap(uv[4], uv[6]);
    }
    if (flipY)
    {
        std::swap(uv[1], uv[5]);
        std::swap(uv[3], uv[7]);
    }

    glUseProgram(m_textureProgram);
    glUniformMatrix4fv(m_texUniformMVP, 1, GL_FALSE, m_projection.data());
    glUniform4f(m_texUniformTint, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1i(m_texUniformSampler, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    for (int i = 0; i < 4; ++i)
    {
        vertices[i].uv[0] = uv[i * 2 + 0];
        vertices[i].uv[1] = uv[i * 2 + 1];
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexPT) * vertices.size(), vertices.data(), GL_STREAM_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);

    glEnableVertexAttribArray(m_texAttribPos);
    glVertexAttribPointer(m_texAttribPos, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPT), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(m_texAttribUV);
    glVertexAttribPointer(m_texAttribUV, 2, GL_FLOAT, GL_FALSE, sizeof(VertexPT), reinterpret_cast<void*>(sizeof(float) * 2));

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);

    glDisableVertexAttribArray(m_texAttribPos);
    glDisableVertexAttribArray(m_texAttribUV);
}

void Renderer::drawFilledRect(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height),
                    r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f, false);
}

void Renderer::drawRect(int x, int y, int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    drawColoredQuad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height),
                    r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f, true);
}

void Renderer::drawLine(float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    drawColoredLine(x1, y1, x2, y2, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

int Renderer::getWindowWidth() const
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    return w;
}

int Renderer::getWindowHeight() const
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(m_window, &w, &h);
    return h;
}

void Renderer::toggleFullscreen()
{
    Uint32 fullscreen_flag = m_isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (SDL_SetWindowFullscreen(m_window, fullscreen_flag) < 0)
    {
        std::cerr << "Error toggling fullscreen: " << SDL_GetError() << std::endl;
    }
    else
    {
        m_isFullscreen = !m_isFullscreen;
    }
}

Uint32 Renderer::getRequiredSDLInitFlags()
{
    return SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER;
}

GLShaderHandle Renderer::compileShader(GLenum type, const char* source)
{
    GLShaderHandle shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        std::cerr << "Shader compilation failed: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLShaderHandle Renderer::createProgram(const char* vertexSrc, const char* fragmentSrc)
{
    GLShaderHandle vertex = compileShader(GL_VERTEX_SHADER, vertexSrc);
    if (!vertex)
        return 0;
    GLShaderHandle fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fragment)
    {
        glDeleteShader(vertex);
        return 0;
    }

    GLShaderHandle program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint status = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        std::cerr << "Program link failed: " << log << std::endl;
        glDeleteProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}
