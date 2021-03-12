#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>

#include <algorithm>

#include <SDL.h>
#include "gl_header.h"
#include "gl_util.h"

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_opengl3.h"

#include "stb_image.h"

#include "gips_app.h"

namespace GIPS {

///////////////////////////////////////////////////////////////////////////////

bool App::loadImage(const std::string& filename) {
    bool isLoaded = false;
    uint8_t* image = nullptr;
    int w = 0, h = 0;
    if (filename.empty()) {
        // create dummy image
        w = m_targetImgWidth;
        h = m_targetImgHeight;
        #ifndef NDEBUG
            fprintf(stderr, "creating %dx%d dummy image\n", w, h);
        #endif
        image = (uint8_t*)malloc(w * h * 4);
        if (!image) { return false; }
        auto p = image;
        for (int y = 0;  y < h;  ++y) {
            for (int x = 0;  x < w;  ++x) {
                *p++ = uint8_t(x);
                *p++ = uint8_t(y);
                *p++ = uint8_t(x ^ y);
                *p++ = 255;
            }
        }
    } else {
        // non-empty file name -> load image from file
        #ifndef NDEBUG
            fprintf(stderr, "loading image file '%s'\n", filename.c_str());
        #endif
        image = stbi_load(filename.c_str(), &w, &h, nullptr, 4);
        if (!image) { return false; }
        isLoaded = true;
    }
    // upload image
    GLutil::clearError();
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    if (GLutil::checkError("texture upload")) { return false; }
    glBindTexture(GL_TEXTURE_2D, 0);
    glFlush();
    glFinish();
    ::free(image);
    m_imgWidth = w;
    m_imgHeight = h;
    m_imgLoaded = isLoaded;
    m_pipeline.markAsChanged();
    if (isLoaded) { m_imgFilename = filename; }
    return true;
}

///////////////////////////////////////////////////////////////////////////////

int App::run(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,     8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,    8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,   8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    #ifndef NDEBUG
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,        SDL_GL_CONTEXT_DEBUG_FLAG);
    #endif

    m_window = SDL_CreateWindow("GIPS",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1080, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (m_window == nullptr) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    m_glctx = SDL_GL_CreateContext(m_window);
    if (m_glctx == nullptr) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_MakeCurrent(m_window, m_glctx);
    SDL_GL_SetSwapInterval(1);

    #ifdef GL_HEADER_IS_GLAD
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            fprintf(stderr, "failed to load OpenGL 3.3 functions\n");
            return 1;
        }
    #else
        #error no valid GL header / loader
    #endif

    if (!GLutil::init()) {
        fprintf(stderr, "OpenGL initialization failed\n");
        return 1;
    }
    GLutil::enableDebugMessages();
    glClearColor(0.125f, 0.125f, 0.125f, 1.0f);

    ImGui::CreateContext();
    m_io = &ImGui::GetIO();
    m_io->IniFilename = "gips_ui.ini";
    ImGui_ImplSDL2_InitForOpenGL(m_window, m_glctx);
    ImGui_ImplOpenGL3_Init(nullptr);

    glGenTextures(1, &m_imgTex);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    GLutil::checkError("texture setup");

    if (!m_pipeline.init()) {
        fprintf(stderr, "failed to initialize the main pipeline\n");
        return 1;
    }

    GLutil::Shader fs(GL_FRAGMENT_SHADER,
         "#version 330 core"
    "\n" "uniform sampler2D gips_tex;"
    "\n" "in vec2 gips_pos;"
    "\n" "out vec4 gips_frag;"
    "\n" "void main() {"
    "\n" "  gips_frag = texture(gips_tex, gips_pos);"
    "\n" "}"
    "\n");
    if (!fs.good()) {
        fprintf(stderr, "failed to compile the main fragment shader:\n%s\n", fs.getLog());
        return 1;
    }

    if (!m_imgProgram.link(m_pipeline.vs(), fs)) {
        fprintf(stderr, "failed to compile the main shader program:\n%s\n", m_imgProgram.getLog());
        return 1;
    }
    if (m_imgProgram.use()) {
        m_imgProgramAreaLoc = m_imgProgram.getUniformLocation("gips_area");
        GLutil::checkError("uniform lookup");
    }
    fs.free();

    loadImage((argc > 1) ? argv[1] : "");
    m_pipeline.addNode("Test Node");
    //m_pipeline.addNode("Another Node");
    m_showIndex = m_pipeline.nodeCount();

    // main loop
    int frameno = 0;
    while (m_active) {
        // wait for events, *except* right after the first frame,
        // where we need an additional redraw so the UI appears properly
        ++frameno;
        handleEvents(frameno != 2);
        updateImageGeometry();

        // process the UI
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(m_window);
        ImGui::NewFrame();
        drawUI();
        #ifndef NDEBUG
            if (m_showDemo) {
                ImGui::ShowDemoWindow();
            }
        #endif
        ImGui::Render();

        // image processing
        if (m_pipeline.changed()) {
            m_pipeline.render(m_imgTex, m_imgWidth, m_imgHeight, m_showIndex);
        }

        // start display rendering
        GLutil::clearError();
        glViewport(0, 0, int(m_io->DisplaySize.x), int(m_io->DisplaySize.y));
        glClear(GL_COLOR_BUFFER_BIT);

        // draw the main image
        updateImageGeometry();
        if (m_imgProgram.use()) {
            glBindTexture(GL_TEXTURE_2D, m_pipeline.resultTex());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            float scaleX =  2.0f / m_io->DisplaySize.x;
            float scaleY = -2.0f / m_io->DisplaySize.y;
            glUniform4f(m_imgProgramAreaLoc,
                scaleX * float(m_imgX0) - 1.0f,
                scaleY * float(m_imgY0) + 1.0f,
                scaleX * m_imgZoom * float(m_imgWidth),
                scaleY * m_imgZoom * float(m_imgHeight));
            GLutil::checkError("main image uniform setup");
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            GLutil::checkError("main image draw");
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // draw the GUI and finish the frame
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        GLutil::checkError("GUI draw");
        SDL_GL_SwapWindow(m_window);
    }

    // clean up
    GLutil::done();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(m_glctx);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

void App::handleEvents(bool wait) {
    SDL_Event ev;
    if (wait) {
        SDL_WaitEvent(nullptr);
    }
    while (SDL_PollEvent(&ev)) {
        ImGui_ImplSDL2_ProcessEvent(&ev);
        switch (ev.type) {
            case SDL_QUIT:
                m_active = false;
                break;
            case SDL_KEYUP:
                if ((ev.key.keysym.sym == SDLK_q) && (SDL_GetModState() & KMOD_CTRL)) {
                    m_active = false;
                }
                if (ev.key.keysym.sym == SDLK_F9) {
                    m_showDemo = !m_showDemo;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!m_io->WantCaptureMouse && (ev.button.button == SDL_BUTTON_LEFT)) {
                    panStart(ev.button.x, ev.button.y);
                }
                break;
            case SDL_MOUSEMOTION:
                if (m_panning && (ev.motion.state & SDL_BUTTON_LMASK)) {
                    panUpdate(ev.motion.x, ev.motion.y);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                m_panning = false;
                break;
            case SDL_MOUSEWHEEL:
                if (!m_io->WantCaptureMouse) {
                    int x = int(m_io->DisplaySize.x * 0.5f);
                    int y = int(m_io->DisplaySize.y * 0.5f);
                    SDL_GetMouseState(&x, &y);
                    zoomAt(x, y, ev.wheel.y);
                }
                m_panning = false;
                break;
            case SDL_DROPFILE:
                loadImage(ev.drop.file);
                SDL_free(ev.drop.file);
                break;
            default:
                break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

float App::getFitZoom() {
    return std::min(m_io->DisplaySize.x / m_imgWidth,
                    m_io->DisplaySize.y / m_imgHeight);
}

void App::updateImageGeometry() {
    float fitZoom = getFitZoom();
    if (m_imgAutofit) {
        m_imgZoom = (fitZoom <= 1.0f) ? fitZoom : std::floor(fitZoom);
    }
    static const auto sanitizePos = [this] (int pos, float dispSizef, int imgSizeUnscaled) -> int {
        int dispSize = int(dispSizef);
        int imgSize = int(float(imgSizeUnscaled) * m_imgZoom + 0.5f);
        if (imgSize < dispSize) {
            return (dispSize - imgSize) / 2;  // if the image fits the screen, center it
        } else {
            return std::max(std::min(pos, 0), dispSize - imgSize);  // restrict to screen edges
        }
    };
    m_imgX0 = sanitizePos(m_imgX0, m_io->DisplaySize.x, m_imgWidth);
    m_imgY0 = sanitizePos(m_imgY0, m_io->DisplaySize.y, m_imgHeight);
}

void App::panStart(int x, int y) {
    m_panRefX = m_imgX0 - x;
    m_panRefY = m_imgY0 - y;
    m_panning = true;
}

void App::panUpdate(int x, int y) {
    m_imgX0 = m_panRefX + x;
    m_imgY0 = m_panRefY + y;
}

void App::zoomAt(int x, int y, int delta) {
    float pixelX = float(x - m_imgX0) / m_imgZoom;
    float pixelY = float(y - m_imgY0) / m_imgZoom;
    if (delta > 0) {
        // zoom in
        if (m_imgZoom >= 1.0f) {
            m_imgZoom = std::ceil(m_imgZoom + 0.5f);
        } else {
            m_imgZoom = 1.0f / std::floor(1.0f / m_imgZoom - 0.5f);
        }
        m_imgAutofit = false;
    } else if (delta < 0) {
        // zoom out
        if (m_imgZoom > 1.5f) {
            m_imgZoom = std::floor(m_imgZoom - 0.5f);
        } else {
            m_imgZoom = 1.0f / std::ceil(1.0f / m_imgZoom + 0.5f);
        }
        // enable autozoom if zoomed out too far;
        // don't bother setting the new zoom and the X0/Y0 coordinates
        // correctly then, as they will be overridden in
        // updateImageGeometry() anyway
        m_imgAutofit = (m_imgZoom <= getFitZoom());
    }
    m_imgX0 = int(std::round(float(x) - m_imgZoom * pixelX));
    m_imgY0 = int(std::round(float(y) - m_imgZoom * pixelY));
}

///////////////////////////////////////////////////////////////////////////////

}  // namespace GIPS