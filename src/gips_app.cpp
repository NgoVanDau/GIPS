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
#include "stb_image_write.h"
#include "stb_image_resize.h"

#include "string_util.h"
#include "file_util.h"

#include "patterns.h"

#include "gips_app.h"

namespace GIPS {

///////////////////////////////////////////////////////////////////////////////

bool App::isShaderFile(uint32_t extCode) {
    return (extCode == StringUtil::makeExtCode("glsl"))
        || (extCode == StringUtil::makeExtCode("frag"))
        || (extCode == StringUtil::makeExtCode("gips"));
}

bool App::isImageFile(uint32_t extCode) {
    return (extCode == StringUtil::makeExtCode("jpg"))
        || (extCode == StringUtil::makeExtCode("jpeg"))
        || (extCode == StringUtil::makeExtCode("jpe"))
        || (extCode == StringUtil::makeExtCode("png"))
        || (extCode == StringUtil::makeExtCode("tga"))
        || (extCode == StringUtil::makeExtCode("bmp"))
        || (extCode == StringUtil::makeExtCode("psd"))
        || (extCode == StringUtil::makeExtCode("gif"))
        || (extCode == StringUtil::makeExtCode("pgm"))
        || (extCode == StringUtil::makeExtCode("ppm"))
        || (extCode == StringUtil::makeExtCode("pnm"));
}

bool App::isSaveImageFile(uint32_t extCode) {
    return (extCode == StringUtil::makeExtCode("jpg"))
        || (extCode == StringUtil::makeExtCode("jpeg"))
        || (extCode == StringUtil::makeExtCode("jpe"))
        || (extCode == StringUtil::makeExtCode("png"))
        || (extCode == StringUtil::makeExtCode("tga"))
        || (extCode == StringUtil::makeExtCode("bmp"));
}

///////////////////////////////////////////////////////////////////////////////

int App::run(int argc, char *argv[]) {
    // get app's base directory
    char* cwd = FileUtil::getCurrentDirectory();
    char *me = StringUtil::pathJoin(cwd, argv[0]);
    ::free(cwd);
    StringUtil::pathRemoveBaseName(me);
    m_appDir = me;
    ::free(me);
    #ifndef NDEBUG
        fprintf(stderr, "application directory: '%s'\n", m_appDir.c_str());
    #endif
    m_appUIConfigFile = m_appDir + StringUtil::defaultPathSep + "gips_ui.ini";
    m_shaderDir = m_appDir + StringUtil::defaultPathSep + "shaders";

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

    m_window = SDL_CreateWindow("GLSL Image Processing System",
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

    ImGui::CreateContext();
    m_io = &ImGui::GetIO();
    m_io->IniFilename = m_appUIConfigFile.c_str();
    ImGui_ImplSDL2_InitForOpenGL(m_window, m_glctx);
    ImGui_ImplOpenGL3_Init(nullptr);

    glGenTextures(1, &m_imgTex);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    GLutil::checkError("texture setup");

    m_helperFBO.init();

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
        m_imgProgramAreaLoc = m_imgProgram.getUniformLocation("gips_pos2ndc");
        glUniform4f(m_imgProgram.getUniformLocation("gips_rel2map"), 0.0f, 0.0f, 1.0f, 1.0f);
        GLutil::checkError("uniform lookup");
    }
    fs.free();

    GLint maxTex, maxVP[2];
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, maxVP);
    m_imgMaxSize = std::min({maxTex, maxVP[0], maxVP[1]});
    #ifndef NDEBUG
        fprintf(stderr, "max tex size: %d, max VP size: %dx%d => max image size: %d\n", maxTex, maxVP[0], maxVP[1], m_imgMaxSize);
    #endif

    loadPattern();
    for (int i = 1;  i < argc;  ++i) {
        handleInputFile(argv[i]);
    }

    // main loop
    bool hadEvents = true;
    while (m_active) {
        // make sure that we render *two* consecutive frames after an event
        // before "sleeping" again; ImGui sometimes needs a frame to settle things
        hadEvents = handleEvents(!hadEvents);
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

        // process pipeline changes
        if (handlePCR()) {
            hadEvents = true;
        }

        // image processing
        if (m_pipeline.changed()) {
            m_pipeline.render(m_imgTex, m_imgWidth, m_imgHeight, m_showIndex);
        }

        // request to save?
        if (m_pcr.type == PipelineChangeRequest::Type::SaveResult) {
            saveResult(m_pcr.path.c_str());
            m_pcr.type = PipelineChangeRequest::Type::None;
            m_pcr.path.clear();
        }

        // start display rendering
        GLutil::clearError();
        glViewport(0, 0, int(m_io->DisplaySize.x), int(m_io->DisplaySize.y));
        glClearColor(0.125f, 0.125f, 0.125f, 1.0f);
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

bool App::handleEvents(bool wait) {
    SDL_Event ev;
    bool hadEvents = false;
    if (wait) {
        SDL_WaitEvent(nullptr);
    }
    while (SDL_PollEvent(&ev)) {
        hadEvents = true;
        ImGui_ImplSDL2_ProcessEvent(&ev);
        switch (ev.type) {
            case SDL_QUIT:
                m_active = false;
                break;
            case SDL_KEYUP:
                switch (ev.key.keysym.sym) {
                    case SDLK_q:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            m_active = false;
                        }
                        break;
                    case SDLK_F5:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            updateImage();
                        }
                        m_pipeline.reload();
                        break;
                    case SDLK_F9:
                        m_showDemo = !m_showDemo;
                        break;
                    default:
                        break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!m_io->WantCaptureMouse && ((ev.button.button == SDL_BUTTON_LEFT) || (ev.button.button == SDL_BUTTON_MIDDLE))) {
                    panStart(ev.button.x, ev.button.y);
                }
                break;
            case SDL_MOUSEMOTION:
                if (m_panning && (ev.motion.state & (SDL_BUTTON_LMASK | SDL_BUTTON_MMASK))) {
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
                handleInputFile(ev.drop.file);
                SDL_free(ev.drop.file);
                break;
            default:
                break;
        }
    }
    return hadEvents;
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
    const auto sanitizePos = [this] (int pos, float dispSizef, int imgSizeUnscaled) -> int {
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
        } else if (m_imgZoom >= 0.5f) {
            m_imgZoom = 1.0f;  // avoid overflow in computation below
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

bool App::handlePCR() {
    if (m_pcr.type == PipelineChangeRequest::Type::None) { return false; }
    bool done = false;
    bool validNodeIndex = (m_pcr.nodeIndex > 0) && (m_pcr.nodeIndex <= m_pipeline.nodeCount());
    #ifndef NDEBUG
        fprintf(stderr, "handling PCR of type %d on node %d\n", static_cast<int>(m_pcr.type), m_pcr.nodeIndex);
    #endif
    switch (m_pcr.type) {

        case PipelineChangeRequest::Type::InsertNode:
            if (validNodeIndex) {
                if (m_pipeline.addNode(m_pcr.path.c_str(), m_pcr.nodeIndex - 1)) {
                    if (m_showIndex >= m_pcr.nodeIndex) { ++m_showIndex; }
                }
            } else {  // invalid node index -> insert at end
                int oldNodeCount = m_pipeline.nodeCount();
                if (m_pipeline.addNode(m_pcr.path.c_str())) {
                    if (m_showIndex == oldNodeCount) { ++m_showIndex; }
                }
            }
            done = true;
            break;


        case PipelineChangeRequest::Type::ReloadNode:
            if (validNodeIndex) {
                m_pipeline.node(m_pcr.nodeIndex - 1).reload(m_pipeline.vs());
                done = true;
            }
            break;

        case PipelineChangeRequest::Type::RemoveNode:
            if (validNodeIndex) {
                m_pipeline.removeNode(m_pcr.nodeIndex - 1);
                if (m_showIndex >= m_pcr.nodeIndex) { --m_showIndex; }
                done = true;
            }
            break;

        case PipelineChangeRequest::Type::MoveNode:
            if (validNodeIndex && (m_pcr.nodeIndex != m_pcr.targetIndex)
            && (m_pcr.targetIndex > 0) && (m_pcr.targetIndex <= m_pipeline.nodeCount())) {
                m_pipeline.moveNode(m_pcr.nodeIndex - 1, m_pcr.targetIndex - 1);
                if ((m_showIndex > std::min(m_pcr.nodeIndex, m_pcr.targetIndex))
                &&  (m_showIndex < std::max(m_pcr.nodeIndex, m_pcr.targetIndex))) {
                    if (m_pcr.nodeIndex < m_pcr.targetIndex) { --m_showIndex; }
                                                        else { ++m_showIndex; }
                }
                done = true;
            }
            break;

        case PipelineChangeRequest::Type::UpdateSource:
            if (updateImage()) {
                m_pipeline.markAsChanged();
            }
            break;

        case PipelineChangeRequest::Type::LoadImage:
            if (loadImage(m_pcr.path.c_str())) {
                m_pipeline.markAsChanged();
            }
            break;

        case PipelineChangeRequest::Type::SaveResult:
            if (isSaveImageFile(m_pcr.path.c_str())) {
                return true;  // don't clear the PCR yet
            }
            break;

        default:
            break;
    }

    // reset PCR
    m_pcr.type = PipelineChangeRequest::Type::None;
    m_pcr.nodeIndex = m_pcr.targetIndex = 0;
    m_pcr.path.clear();
    return done;
}

void App::handleInputFile(const char* filename) {
    uint32_t extCode = StringUtil::extractExtCode(filename);
    if (isShaderFile(extCode)) {
        if (m_pipeline.addNode(filename, m_showIndex)) {
            m_showIndex++;
        }
    } else if (isImageFile(extCode)) {
            loadImage(filename);
    } else {
        setError("can't open file: unrecognized file type");
    }
}

///////////////////////////////////////////////////////////////////////////////

bool App::uploadImageTexture(uint8_t* data, int width, int height, ImageSource src) {
    GLutil::clearError();
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    GLenum error = GLutil::checkError("texture upload");
    glBindTexture(GL_TEXTURE_2D, 0);
    glFlush();
    glFinish();
    ::free(data);
    m_imgWidth = width;
    m_imgHeight = height;
    m_imgSource = src;
    switch (error) {
        case GL_INVALID_ENUM:  return setError("unsupported texture format");
        case GL_INVALID_VALUE: return setError("unsupported texture size");
        case GL_OUT_OF_MEMORY: return setError("insufficient video memory");
        default: break;
    }
    if (!error) {
        m_pipeline.markAsChanged();
        return setSuccess();
    }
    return false;
}

bool App::loadColor() {
    if ((m_targetImgWidth != m_imgWidth) || (m_targetImgHeight != m_imgHeight)) {
        if (!uploadImageTexture(nullptr, m_targetImgWidth, m_targetImgHeight, ImageSource::Color)) {
            return false;
        }
    }
    if (!m_helperFBO.begin(m_imgTex)) { return setError("failed to render solid color image"); }
    glClearColor(m_imgColor[0], m_imgColor[1], m_imgColor[2], m_imgColor[3]);
    glClear(GL_COLOR_BUFFER_BIT);
    m_helperFBO.end();
    return setSuccess();
}

bool App::loadImage(const char* filename) {
    if (!filename || !filename[0]) {
        m_imgFilename.clear();
        return false;
    }
    #ifndef NDEBUG
        fprintf(stderr, "loading image file '%s'\n", filename);
    #endif
    m_imgFilename = filename;
    int rawWidth = 0, rawHeight = 0;
    uint8_t* rawData = stbi_load(filename, &rawWidth, &rawHeight, nullptr, 4);
    if (!rawData) { return setError("could not read image file"); }
    int targetWidth  = m_imgResize ? m_targetImgWidth  : m_imgMaxSize;
    int targetHeight = m_imgResize ? m_targetImgHeight : m_imgMaxSize;
    if ((rawWidth <= targetWidth) && (rawHeight <= targetHeight)) {
        return uploadImageTexture(rawData, rawWidth, rawHeight, ImageSource::Image);
    }
    int scaledWidth  = targetWidth;
    int scaledHeight = (rawHeight * scaledWidth + (rawWidth / 2)) / rawWidth;
    if (scaledHeight > targetHeight) {
        scaledHeight = targetHeight;
        scaledWidth = (rawWidth * scaledHeight + (rawHeight / 2)) / rawHeight;
    }
    #ifndef NDEBUG
        fprintf(stderr, "downscaling %dx%d -> %dx%d\n", rawWidth, rawHeight, scaledWidth, scaledHeight);
    #endif
    uint8_t* scaledData = (uint8_t*) malloc(scaledWidth * scaledHeight * 4);
    if (!scaledData) { ::free(rawData); return setError("out of memory"); }
    if (!stbir_resize_uint8(
           rawData,    rawWidth,    rawHeight, 0,
        scaledData, scaledWidth, scaledHeight, 0,
        4)) { ::free(rawData); return setError("could not downscale image"); }
    ::free(rawData);
    return uploadImageTexture(scaledData, scaledWidth, scaledHeight, ImageSource::Image);
}

bool App::loadPattern() {
    if ((m_imgPatternID < 0) || (m_imgPatternID >= NumPatterns)) {
        #ifndef NDEBUG
            fprintf(stderr, "requested invalid pattern ID %d\n", m_imgPatternID);
        #endif
        return setError("invalid pattern");
    }
    const PatternDefinition& pat = Patterns[m_imgPatternID];
    #ifndef NDEBUG
        fprintf(stderr, "creating %dx%d '%s' pattern image %s alpha\n",
                m_targetImgWidth, m_targetImgHeight,
                pat.name, m_imgPatternNoAlpha ? "without" : "with");
    #endif
    uint8_t* data = (uint8_t*)malloc(m_targetImgWidth * m_targetImgHeight * 4);
    if (!data) { return setError("out of memory"); }
    pat.render(data, m_targetImgWidth, m_targetImgHeight, !m_imgPatternNoAlpha);
    if (m_imgPatternNoAlpha && !pat.alwaysWritesAlpha) {
        uint8_t *p = &data[3];
        for (int i = m_targetImgWidth * m_targetImgHeight;  i;  --i) {
            *p = 0xFF;
            p += 4;
        }
    }
    return uploadImageTexture(data, m_targetImgWidth, m_targetImgHeight, ImageSource::Pattern);
}

bool App::updateImage() {
    switch (m_imgSource) {
        case ImageSource::Color:   return loadColor();
        case ImageSource::Image:   return loadImage(m_imgFilename.c_str());
        case ImageSource::Pattern: return loadPattern();
        default: return setError("unkown image source type");
    }
}

///////////////////////////////////////////////////////////////////////////////

bool App::saveResult(const char* filename) {
    if (!filename || !filename[0]) { return false;} 
    // assumes that filename is already validated to be one of the supported
    // formats! (otherwise we'll fail, and very lately so!)
    #ifndef NDEBUG
        fprintf(stderr, "saving '%s'\n", filename);
    #endif
    m_lastSaveFilename = filename;

    // create staging texture
    GLuint tex;
    GLutil::clearError();
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_imgWidth, m_imgHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (GLutil::checkError("saving texture creation")) { return setError("failed to create temporary texture for saving"); }
    uint8_t *data = (uint8_t*) malloc(m_imgWidth * m_imgHeight * 4);
    if (!data) { return setError("out of memory"); }

    // copy result into staging texture
    m_imgProgram.use();
    glBindTexture(GL_TEXTURE_2D, m_pipeline.resultTex());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    float scaleX =  2.0f / m_io->DisplaySize.x;
    float scaleY = -2.0f / m_io->DisplaySize.y;
    glUniform4f(m_imgProgramAreaLoc, -1.0f, -1.0f, 2.0f, 2.0f);
    glViewport(0, 0, m_imgWidth, m_imgHeight);
    if (GLutil::checkError("saving render preparation")) { ::free(data); return setError("image retrieval failed"); }
    m_helperFBO.begin(tex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_helperFBO.end();
    if (GLutil::checkError("saving render draw operation")) { ::free(data); return setError("image retrieval failed"); }

    // read and delete staging texture
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    if (GLutil::checkError("saving texture readback")) { ::free(data); return setError("image retrieval failed"); }

    // save the image
    int res;
    switch (StringUtil::extractExtCode(filename)) {
        case StringUtil::makeExtCode("jpg"):
        case StringUtil::makeExtCode("jpeg"):
        case StringUtil::makeExtCode("jpe"):
            res = stbi_write_jpg(filename, m_imgWidth, m_imgHeight, 4, data, 98);
            break;
        case StringUtil::makeExtCode("png"):
            res = stbi_write_png(filename, m_imgWidth, m_imgHeight, 4, data, 0);
            break;
        case StringUtil::makeExtCode("tga"):
            res = stbi_write_tga(filename, m_imgWidth, m_imgHeight, 4, data);
            break;
        case StringUtil::makeExtCode("bmp"):
            res = stbi_write_bmp(filename, m_imgWidth, m_imgHeight, 4, data);
            break;
        default:
            ::free(data); return setError("unrecognized output file format");
    }
    ::free(data);
    if (res == 0) { return setError("image saving failed"); }
    return setSuccess();
}

///////////////////////////////////////////////////////////////////////////////

}  // namespace GIPS
