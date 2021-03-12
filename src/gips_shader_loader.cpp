#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cassert>

#include <string>
#include <sstream>
#include <vector>

#include "gl_header.h"
#include "gl_util.h"
#include "string_util.h"

#include "gips_core.h"

namespace GIPS {

///////////////////////////////////////////////////////////////////////////////

enum class GLSLToken : int {
    Other       = 0,
    Ignored     = -1,
    Uniform     = 10,
    Float       = 1,
    Vec2        = 2,
    Vec3        = 3,
    Vec4        = 4,
    RunSingle   = 99,
    RunPass1    = 100,
    RunPass2    = 200,
    RunPass3    = 300,
    RunPass4    = 400,
    OpenParens  = 90,
    CloseParens = 91,
};

enum class PassInput { Coord, RGB, RGBA };
enum class PassOutput { RGB, RGBA };

static const StringUtil::LookupEntry<GLSLToken> tokenMap[] = {
    { "in",        GLSLToken::Ignored },
    { "uniform",   GLSLToken::Uniform },
    { "float",     GLSLToken::Float },
    { "vec2",      GLSLToken::Vec2 },
    { "vec3",      GLSLToken::Vec3 },
    { "vec4",      GLSLToken::Vec4 },
    { "run",       GLSLToken::RunSingle },
    { "run_pass1", GLSLToken::RunPass1 },
    { "run_pass2", GLSLToken::RunPass2 },
    { "run_pass3", GLSLToken::RunPass3 },
    { "run_pass4", GLSLToken::RunPass4 },
    { "(",         GLSLToken::OpenParens },
    { ")",         GLSLToken::CloseParens },
    { "){",        GLSLToken::CloseParens },
    { nullptr,     GLSLToken::Other },
};

///////////////////////////////////////////////////////////////////////////////

bool Node::load(const char* filename, const GLutil::Shader& vs) {
    // initialize local variables to pessimistic defaults
    m_passCount = 0;
    m_filename = filename;
    m_name = filename;
    std::vector<Parameter> newParams;
    std::stringstream shader;
    std::stringstream err;
    StringUtil::Tokenizer tok;
    GLutil::Shader fs;
    GLutil::Program* prog = nullptr;
    Parameter* param = nullptr;
    GLSLToken paramDataType = GLSLToken::Other;
    int paramValueIndex = -1;
    bool inParamStatement = false;
    int currentPass = 0;
    int passMask = 0;
    bool singlePass = false;
    PassInput inputs[MaxPasses];
    PassOutput outputs[MaxPasses];
    static constexpr int GLSLTokenHistorySize = 4;
    GLSLToken tt[GLSLTokenHistorySize] = { GLSLToken::Other, };

    const char *code = "uniform float saturation = 1.0; // @min=0 @max=5"
                  "\n" "uniform vec3 key = vec3(.299, .587, .114);  // @rgb key color"
                  "\n" "vec3 run(vec3 c) {"
                  "\n" "  float gray = dot(c, key / (key.r + key.g + key.b));"
                  "\n" "  return mix(vec3(gray), c, saturation);"
                  "\n" "}"
                  "\n";

    // analyze the GLSL code
    tok.init(code);
    while (tok.next()) {
        // check for comment
        bool singleLineComment = tok.isToken("//");
        bool multiLineComment  = tok.isToken("/*");
        if (singleLineComment || multiLineComment) {
            // extract comment
            if (singleLineComment) { tok.extendUntil("\n"); }
            if (multiLineComment)  { tok.extendUntil("*/"); }
            char* comment = tok.extractToken();
            if (multiLineComment) { comment[strlen(comment) - 2] = '\0'; }  // strip '*/' at end
            char* content = &comment[2];  // skip '//' or '/*'
            if (content[0] == '!') { ++content; }  // handle '//!' Doxygen-style comment

            // search for @key=value tokens
            char *pos = content;
            for (;;) {
                pos = strchr(pos, '@');
                if (!pos) { break; }  // no token found
                if (isalnum(pos[-1])) { ++pos; continue; }  // ignore token in the middle of a word
                // extract key
                char *key = &pos[1];
                char *value = nullptr;
                for (pos = key;  StringUtil::isident(*pos);  ++pos) { *pos = tolower(*pos); }
                if (*pos == '=') {
                    // extract value
                    *pos = '\0';
                    value = &pos[1];
                    for (pos = value;  StringUtil::isident(*pos);  ++pos) { *pos = tolower(*pos); }
                }
                // terminate key/value and move pos to character after token
                if (*pos) { *pos++ = '\0'; }

                // at this point, key and value have been extracted; now parse them
                bool isNum = false;
                float fval = 0.0f;
                if (value) {
                    char *end = nullptr;
                    fval = strtof(value, &end);
                    isNum = (end && !*end);
                }

                // define a few parsing helper functions
                bool keyMatched = false;
                static const auto isKey = [&] (const char *t) -> bool {
                    bool match = !strcmp(key, t);
                    keyMatched = keyMatched || match;
                    return match;
                };
                static const auto needParam = [&] () -> bool {
                    if (!param) {
                        err << "(GIPS) '@" << key << "' token is only valid inside a parameter comment\n";
                    }
                    return !!param;
                };
                static const auto needNum = [&] () -> bool {
                    if (!isNum) {
                        err << "(GIPS) '@" << key << "' token requires a numeric value\n";
                    }
                    return isNum;
                };
                static const auto setParamType = [&] (GLSLToken dt, ParameterType pt) -> void {
                    if (paramDataType != dt) {
                        err << "(GIPS) '@" << key << "' format is incompatible with uniform data type of parameter '" << param->m_name << "'\n";
                    } else {
                        param->m_type = pt;
                    }
                };

                // evaluate the tokens
                     if ((isKey("min") || isKey("off")) && needParam() && needNum()) { param->m_minValue = fval; }
                else if ((isKey("max") || isKey("on"))  && needParam() && needNum()) { param->m_maxValue = fval; }
                else if (isKey("rgb")  && needParam()) { setParamType(GLSLToken::Vec3, ParameterType::RGB); }
                else if (isKey("rgba") && needParam()) { setParamType(GLSLToken::Vec4, ParameterType::RGBA); }
                else if (!keyMatched) {
                    err << "(GIPS) unrecognized token '@" << key << "'\n";
                }

                // delete the token and continue searching for the next token
                memmove(&key[-1], pos, strlen(pos) + 1);
                pos = &key[-1];
            }   // END comment token extraction loop

            // if this is a parameter comment, trim and store it
            if (param) {
                content = StringUtil::skipWhitespace(content);
                StringUtil::trimTrailingWhitespace(content);
                if (content[0]) { param->m_desc = content; }
            }

            // done with comment processing
            ::free(comment);
            param = nullptr;  // parameter comment handled, forget about the parameter
            continue;
        }

        // add token type to history
        GLSLToken newTT = StringUtil::lookup(tokenMap,tok.token());
        if (newTT == GLSLToken::Ignored) {
            continue;
        }
        for (int i = GLSLTokenHistorySize - 1;  i;  --i) { tt[i] = tt[i-1]; }
        tt[0] = newTT;

        // check for new uniform
        // pattern: [2]="uniform", [1]="float"|"vec3"|"vec4", [0]=name
        if (tt[2] == GLSLToken::Uniform) {
            if ((tt[1] == GLSLToken::Float) || (tt[1] == GLSLToken::Vec3) || (tt[1] == GLSLToken::Vec4)) {
                newParams.emplace_back();
                param = &newParams.back();
                param->m_name = std::string(tok.stringFromStart(), tok.length());
                // set a default parameter type based on the data type
                paramDataType = tt[1];
                switch (paramDataType) {
                    case GLSLToken::Float: param->m_type = ParameterType::Value; break;
                    case GLSLToken::Vec3:  param->m_type = ParameterType::RGB;   break;
                    case GLSLToken::Vec4:  param->m_type = ParameterType::RGBA;  break;
                    default: break;
                }
                paramValueIndex = -1;
                inParamStatement = true;
            } else {  // uniform detected, but unsupported type
                err << "(GIPS) uniform variable '" << tok.token() << "' has unsupported data type\n";
                inParamStatement = false;
            }
            continue;
        }

        // check for begin of uniform default value assignment
        if (param && inParamStatement && (paramValueIndex < 0) && tok.contains('=')) {
            paramValueIndex = 0;
            continue;
        }

        // if we're inside a uniform default assignment,
        // check if we can parse the token as a number
        if (param && inParamStatement && (paramValueIndex >= 0) && (paramValueIndex < 4)) {
            char* end = nullptr;
            float f = strtof(tok.token(), &end);
            if (end && !*end) {
                param->m_value[paramValueIndex++] = f;
            }
        }

        // check for end of uniform statement (and, hence, assignment)
        if (tok.contains(';')) {
            inParamStatement = false;
            continue;
        }

        // check pass definition
        // pattern: [3]="vec3/4", [2]="run[_passX]", [1]="(", [0]="vec2/3/4"
        if (((tt[3] == GLSLToken::Vec3) || (tt[3] == GLSLToken::Vec4))
        &&  ((tt[2] == GLSLToken::RunSingle) || (tt[2] == GLSLToken::RunPass1) || (tt[2] == GLSLToken::RunPass2) || (tt[2] == GLSLToken::RunPass3) || (tt[2] == GLSLToken::RunPass4))
        &&   (tt[1] == GLSLToken::OpenParens)
        &&  ((tt[0] == GLSLToken::Vec2) || (tt[0] == GLSLToken::Vec3) || (tt[0] == GLSLToken::Vec4)))
        {
            switch (tt[2]) {
                case GLSLToken::RunSingle: currentPass = 0; singlePass = true; break;
                case GLSLToken::RunPass1:  currentPass = 0; singlePass = false; break;
                case GLSLToken::RunPass2:  currentPass = 1; break;
                case GLSLToken::RunPass3:  currentPass = 2; break;
                case GLSLToken::RunPass4:  currentPass = 3; break;
                default: assert(0);
            }
            if (currentPass >= MaxPasses) { continue; }
            switch (tt[0]) {
                case GLSLToken::Vec2: inputs[currentPass] = PassInput::Coord; break;
                case GLSLToken::Vec3: inputs[currentPass] = PassInput::RGB;   break;
                case GLSLToken::Vec4: inputs[currentPass] = PassInput::RGBA;  break;
                default: assert(0);
            }
            switch (tt[3]) {
                case GLSLToken::Vec3: outputs[currentPass] = PassOutput::RGB;  break;
                case GLSLToken::Vec4: outputs[currentPass] = PassOutput::RGBA; break;
                default: assert(0);
            }
            passMask |= (1 << currentPass);
            continue;
        }
    }

    // first pass defined?
    if (!(passMask & 1)) {
        err << "(GIPS) no valid 'run' or 'run_pass1' function found\n";
        goto load_finalize;
    }

    // generate code for the passes
    for (currentPass = 0;  (currentPass < MaxPasses) && ((passMask >> currentPass) & 1);  ++currentPass) {
        passMask &= ~(1 << currentPass);
        PassInput input = inputs[currentPass];
        PassOutput output = outputs[currentPass];

        // fragment shader assembly: boilerplate
        shader.clear();
        shader << "#version 330 core\n"
                  "#line 8000 0\n"
                  "in vec2 gips_pos;\n"
                  "out vec4 gips_frag;\n"
                  "uniform sampler2D gips_tex;\n";

        // fragment shader assembly: add user code
        shader << "#line 1 " << (currentPass + 1) << "\n" << code;

        // fragment shader assembly: main() function prologue
        shader << "\n#line 9000 0\n"
                  "void main() {\n";
        if (input != PassInput::Coord) {
            // Coord->RGB(A) case: implicit texture lookup
            shader << "  vec4 color = texture(gips_tex, gips_pos);\n";
        }

        // fragment shader assembly: output statement generation
        shader << "  gips_frag = ";
        if (output == PassOutput::RGB) {
            shader << "vec4(";  // if output isn't already RGBA, convert it
        }
        shader << "run";
        if (currentPass || !singlePass) { shader << "_pass" << (currentPass + 1); }
        switch (input) {
            case PassInput::Coord: shader << "(gips_pos)"; break;
            case PassInput::RGB:   shader << "(color.rgb)"; break;
            case PassInput::RGBA:  shader << "(color)"; break;
            default: assert(0);
        }
        if (output == PassOutput::RGB) {
            if (input == PassInput::Coord) {
                shader << ", 1.0)";  // Coord->RGB case: set alpha to 1
            } else {
                shader << ", color.a)";  // RGB(A)->RGB case: keep source alpha
            }
        }
        shader << ";\n}\n";

#if 0  // DEBUG
puts("------------------------------------");
puts(shader.str().c_str());
puts("------------------------------------");
#endif

        // compile shader and link program
        fs.compile(GL_FRAGMENT_SHADER, shader.str().c_str());
        if (fs.haveLog()) { err << fs.getLog() << "\n"; }
        if (!fs.good()) { goto load_finalize; }
        prog = &m_passes[currentPass].program;
        prog->link(vs, fs);
        if (prog->haveLog()) { err << prog->getLog() << "\n"; }
        fs.free();
        if (!prog->good()) { goto load_finalize; }

        // get uniform locations
        prog->use();
        GLutil::checkError("node setup");
        glUniform4f(prog->getUniformLocation("gips_area"), -1.0f, -1.0f, 2.0f, 2.0f);
        for (auto& p : newParams) {
            p.m_location[currentPass] = prog->getUniformLocation(p.m_name.c_str());
        }
        GLutil::checkError("node uniform lookup");

        // pass program setup done
        glUseProgram(0);
    }

    // setup done, proclaim success
    m_passCount = currentPass;

load_finalize:
    m_errors = err.str();
    m_params = newParams;
    return (m_passCount > 0);
}

///////////////////////////////////////////////////////////////////////////////

}  // namespace GIPS