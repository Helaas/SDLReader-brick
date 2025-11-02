#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <string.h>

int main() {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return 1;
    }
    
    if (!eglInitialize(display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return 1;
    }
    
    EGLConfig config;
    EGLint numConfigs;
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        fprintf(stderr, "Failed to choose EGL config\n");
        return 1;
    }
    
    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        return 1;
    }
    
    EGLint pbufferAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        return 1;
    }
    
    if (!eglMakeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "Failed to make context current\n");
        return 1;
    }
    
    printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
    printf("GL_VENDOR: %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    
    // Test shader compilation
    const char* vertexShaderSource = 
        "#version 100\n"
        "#ifdef GL_ES\n"
        "    precision highp float;\n"
        "#endif\n"
        "uniform mat4 ProjMtx;\n"
        "attribute vec2 Position;\n"
        "attribute vec2 UV;\n"
        "attribute vec4 Color;\n"
        "varying vec2 Frag_UV;\n"
        "varying vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";
    
    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertShader);
    
    GLint compiled = 0;
    glGetShaderiv(vertShader, GL_COMPILE_STATUS, &compiled);
    
    GLint logLength = 0;
    glGetShaderiv(vertShader, GL_INFO_LOG_LENGTH, &logLength);
    
    if (logLength > 1) {
        char* log = (char*)malloc(logLength);
        glGetShaderInfoLog(vertShader, logLength, NULL, log);
        printf("Vertex shader log:\n%s\n", log);
        free(log);
    }
    
    printf("Vertex shader compiled: %s\n", compiled ? "YES" : "NO");
    
    // Test without #version directive
    const char* vertexShaderSource2 = 
        "#ifdef GL_ES\n"
        "    precision highp float;\n"
        "#endif\n"
        "uniform mat4 ProjMtx;\n"
        "attribute vec2 Position;\n"
        "attribute vec2 UV;\n"
        "attribute vec4 Color;\n"
        "varying vec2 Frag_UV;\n"
        "varying vec4 Frag_Color;\n"
        "void main()\n"
        "{\n"
        "    Frag_UV = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
        "}\n";
    
    GLuint vertShader2 = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader2, 1, &vertexShaderSource2, NULL);
    glCompileShader(vertShader2);
    
    compiled = 0;
    glGetShaderiv(vertShader2, GL_COMPILE_STATUS, &compiled);
    
    logLength = 0;
    glGetShaderiv(vertShader2, GL_INFO_LOG_LENGTH, &logLength);
    
    if (logLength > 1) {
        char* log = (char*)malloc(logLength);
        glGetShaderInfoLog(vertShader2, logLength, NULL, log);
        printf("Vertex shader (no #version) log:\n%s\n", log);
        free(log);
    }
    
    printf("Vertex shader (no #version) compiled: %s\n", compiled ? "YES" : "NO");
    
    return 0;
}
