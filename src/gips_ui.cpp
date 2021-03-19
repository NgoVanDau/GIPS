#include <algorithm>

#include "imgui.h"

#define PFD_SKIP_IMPLEMENTATION 1
#include "portable-file-dialogs.h"

#include "string_util.h"
#include "dirlist.h"

#include "gips_app.h"

///////////////////////////////////////////////////////////////////////////////

struct StatusWindow {
    StatusWindow(const char* name, float ax, float ay) {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(
            vp->WorkPos.x + ax * vp->WorkSize.x,
            vp->WorkPos.y + ay * vp->WorkSize.y
        ), ImGuiCond_Always, ImVec2(ax, ay));
        ImGui::SetNextWindowBgAlpha(0.375f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(16.0f, 16.0f));
        ImGui::Begin(name, nullptr,
                     ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoMove);
    }
    ~StatusWindow() {
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
};

///////////////////////////////////////////////////////////////////////////////

struct ButtonColorOverride {
    ButtonColorOverride(float r, float g, float b) {
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(r, g, b, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(r * 1.125f, g * 1.125f, b * 1.125f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.25f + 0.75f * r, 0.25f + 0.75f * g, 0.25f + 0.75f * b, 1.0f));
    }
    ButtonColorOverride(int rgb24) : ButtonColorOverride((rgb24 >> 16) / 255.0f, ((rgb24 >> 8) & 0xFF) / 255.0f, (rgb24 & 0xFF) / 255.0f) {}
    ~ButtonColorOverride() {
        ImGui::PopStyleColor(3);
    }
};

///////////////////////////////////////////////////////////////////////////////

static void ShaderBrowserMenu(GIPS::App& app, int nodeIndex, const char* dir) {
    const DirList& list = getCachedDirList(dir);
    for (const auto& item : list.items) {
        if (item.isDir) {
            if (ImGui::BeginMenu(item.nameNoExt.c_str())) {
                ShaderBrowserMenu(app, nodeIndex, item.fullPath.c_str());
                ImGui::EndMenu();
            }
        } else if (app.isShaderFile(item.fullPath.c_str())) {
            if (ImGui::Selectable(item.nameNoExt.c_str())) {
                app.requestInsertNode(item.fullPath.c_str(), nodeIndex);
            }
        }
    }
}

static bool TreeNodeForGIPSNode(GIPS::App& app, int nodeIndex=0, GIPS::Node* node=nullptr) {
    ImGui::AlignTextToFramePadding();
    bool open = ImGui::TreeNodeEx(node ? node->name() : "Input Image",
        ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);

    // add context menu
    if (node && ImGui::BeginPopupContextItem("node context menu popup")) {
        if (ImGui::BeginMenu("insert")) {
            ShaderBrowserMenu(app, nodeIndex, app.getShaderDir());
            ImGui::EndMenu();
        }
        if ((nodeIndex > 1) && ImGui::Selectable("move up")) {
            app.requestMoveNode(nodeIndex, nodeIndex - 1);
        }
        if ((nodeIndex < app.getNodeCount()) && ImGui::Selectable("move down")) {
            app.requestMoveNode(nodeIndex, nodeIndex + 1);
        }
        if (ImGui::BeginMenu("filename")) {
            ImGui::Text("%s", node->filename());
            ImGui::EndMenu();
        }
        if (ImGui::Selectable("reload")) {
            app.requestReloadNode(nodeIndex);
        }
        if (ImGui::Selectable("remove")) {
            app.requestRemoveNode(nodeIndex);
        }
        ImGui::EndPopup();
    }

    // add node toggle and show index buttons
    if (node) {
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 55.0f);
        ButtonColorOverride _(node->enabled() ? 0x208020 : 0x802020);
        if (ImGui::Button(node->enabled() ? "On" : "Off")) { node->toggle(); }
    }
    {
        ImGui::SameLine(ImGui::GetWindowContentRegionWidth() - 24.0f);
        ButtonColorOverride _((app.getShowIndex() == nodeIndex) ? 0xC0C040 : 0x405060);
        if (ImGui::Button("Show")) { app.setShowIndex(nodeIndex); }
    }
    return open;
}

///////////////////////////////////////////////////////////////////////////////

void GIPS::App::drawUI() {
    // mouse position status
    bool mouseValid = ImGui::IsMousePosValid();
    float mpx = 0.0f, mpy = 0.0f;
    if (mouseValid) {
        mpx = (m_io->MousePos.x - float(m_imgX0)) / m_imgZoom;
        mpy = (m_io->MousePos.y - float(m_imgY0)) / m_imgZoom;
        if ((mpx < 0.0f) || (mpy < 0.0f) || (mpx >= float(m_imgWidth)) || (mpy >= float(m_imgHeight))) {
            mouseValid = false;
        }
    }
    if (mouseValid) {
        StatusWindow _("Mouse Position", 0.0f, 1.0f);
        ImGui::Text("%d,%d", int(mpx), int(mpy));
    }

    // zoom status
    if (!m_imgAutofit || (m_imgZoom >= 0.99f)) {
        StatusWindow _("Zoom", 1.0f, 1.0f);
        if (m_imgZoom >= 0.99f) {
            ImGui::Text("%.0fx", m_imgZoom);
        } else {
            ImGui::Text("1/%.0fx", 1.0f / m_imgZoom);
        }
    }

    // main window begin
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->WorkPos, ImGuiCond_Once, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Filters")) {
        int oldShowIndex = m_showIndex;

        // input image
        if (TreeNodeForGIPSNode(*this)) {
            // source
            int src = static_cast<int>(m_imgSource);
            ImGui::RadioButton("Image",   &src, static_cast<int>(ImageSource::Image));
            ImGui::SameLine();
            ImGui::RadioButton("Color",   &src, static_cast<int>(ImageSource::Color));
            ImGui::SameLine();
            ImGui::RadioButton("Pattern", &src, static_cast<int>(ImageSource::Pattern));
            if (src != static_cast<int>(m_imgSource)) {
                m_imgSource = static_cast<ImageSource>(src);
                requestUpdateSource();
            }

            // image source
            if (m_imgSource == ImageSource::Image) {
                if (ImGui::Button("Load ...")) {
                    auto path = pfd::open_file("Load Image", m_imgFilename.c_str(),
                        { "Image Files", "*.jpg *.jpeg *.png *.bmp *.tga *.pgm *.ppm *.gif *.psd",
                          "All Files", "*" }
                    ).result();
                    if (!path.empty()) {
                        requestLoadImage(path[0].c_str());
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%s", m_imgFilename.c_str());
                if (ImGui::Checkbox("resize to target size if larger", &m_imgResize)) {
                    requestUpdateSource();
                }
            }

            // color source
            if (m_imgSource == ImageSource::Color) {
                if (ImGui::ColorEdit4("", m_imgColor)) {
                    requestUpdateSource();
                }
            }

            // target size
            if ((m_imgSource == ImageSource::Image) && !m_imgResize) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Target Size:");
            ImGui::SameLine();
            ImGui::PushItemWidth(40.0f);
            ImGui::InputInt("##tw", &m_editTargetWidth, 0);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextUnformatted("x");
            ImGui::SameLine();
            ImGui::PushItemWidth(40.0f);
            ImGui::InputInt("##th", &m_editTargetHeight, 0);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Set")) {
                m_targetImgWidth = m_editTargetWidth;
                m_targetImgHeight = m_editTargetHeight;
                requestUpdateSource();
            }
            if ((m_imgSource == ImageSource::Image) && !m_imgResize) {
                ImGui::PopStyleVar(1);
            }

            ImGui::Text("Current Size: %dx%d", m_imgWidth, m_imgHeight);
            ImGui::TreePop();
        }

        // processing nodes
        for (int nodeIndex = 1;  nodeIndex <= m_pipeline.nodeCount();  ++nodeIndex) {
            auto& node = m_pipeline.node(nodeIndex - 1);
            ImGui::PushID(nodeIndex);
            if (TreeNodeForGIPSNode(*this, nodeIndex, &node)) {
                // parameters
                for (int paramIndex = 0 ;  paramIndex < node.paramCount();  ++paramIndex) {
                    auto& param = node.param(paramIndex);
                    switch (param.type()) {
                        case ParameterType::Toggle: {
                            bool checked = std::abs(param.value()[0] - param.maxValue())
                                         < std::abs(param.value()[0] - param.minValue());
                            if (ImGui::Checkbox(param.desc(), &checked)) {
                                param.value()[0] = checked ? param.maxValue() : param.minValue();
                            }
                            break; }
                        case ParameterType::Value:
                            ImGui::SliderFloat(param.desc(), param.value(), param.minValue(), param.maxValue(), param.format());
                            break;
                        case ParameterType::Value2:
                            ImGui::SliderFloat2(param.desc(), param.value(), param.minValue(), param.maxValue(), param.format());
                            break;
                        case ParameterType::Value3:
                            ImGui::SliderFloat3(param.desc(), param.value(), param.minValue(), param.maxValue(), param.format());
                            break;
                        case ParameterType::Value4:
                            ImGui::SliderFloat4(param.desc(), param.value(), param.minValue(), param.maxValue(), param.format());
                            break;
                        case ParameterType::RGB:
                            ImGui::ColorEdit3(param.desc(), param.value());
                            break;
                        case ParameterType::RGBA:
                            ImGui::ColorEdit4(param.desc(), param.value());
                            break;
                        default:
                            ImGui::PushStyleColor(ImGuiCol_Text, 0xFF0000FF);
                            ImGui::Text("parameter '%s' has unsupported type", param.name());
                            ImGui::PopStyleColor(1);
                            break;
                    }
                }

                // error messages (if present)
                if (node.errors()[0]) {
                    if (node.passCount()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, 0xFF0000FF);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, 0xFF202020);
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_Text, 0xFF80C0FF);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, 0x800000FF);
                    }
                    ImGui::InputTextMultiline("errors",
                        const_cast<char*>(node.errors()),
                        StringUtil::stringLengthWithoutTrailingWhitespace(node.errors()),
                        ImVec2(-FLT_MIN, ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() * (StringUtil::countLines(node.errors()) - 1)),
                        ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor(2);
                }

                // end of processing node
                ImGui::TreePop();
            }   // END node UI block
            ImGui::PopID();
        }   // END node iteration

        // force re-rendering when the show index changed
        if (m_showIndex != oldShowIndex) {
            m_pipeline.markAsChanged();
        }

        // "Add Filter" button
        if (ImGui::Button("Add Filter ...")) {
            ImGui::OpenPopup("add_filter");
        }
        if (ImGui::BeginPopup("add_filter")) {
            ShaderBrowserMenu(*this, 0, getShaderDir());
            ImGui::EndPopup();
        }

        // "Save" button
        ImGui::SameLine();
        if (ImGui::Button("Save ...")) {
            auto path = pfd::save_file(
                "Save Image", m_lastSaveFilename,
                { "Image Files", "*.jpg *.png *.bmp *.tga",
                  "All Files", "*" }
            ).result();
            if (!path.empty()) {
                requestSaveResult(path.c_str());
            }
        }
    }   // END main window
    ImGui::End();
}

///////////////////////////////////////////////////////////////////////////////
