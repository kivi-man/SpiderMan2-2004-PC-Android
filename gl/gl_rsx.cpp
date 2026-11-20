// The renderer's OpenGL 4.3 calls on RSXGL, the PS3's OpenGL 3.1 subset (ps3/README.md). Like the GLES stand ins in
// gl_api.cpp, the renderer keeps calling the desktop names and what RSXGL lacks goes through stand ins:
//   - GLSL 4.30 is rewritten to the GLSL 1.30 RSXGL's compiler (Mesa's) takes: the uniform blocks become plain
//     uniforms, the stages' interface blocks plain varyings, layout() qualifiers go (attribute locations and sampler
//     units are set by name instead), textureSize/texelFetch read through a size uniform, integer bit tricks become
//     float arithmetic, gl_VertexID (the full screen triangle) becomes an attribute;
//   - uniform buffers live on the CPU: a draw sends the program the fields of its blocks that changed since that
//     program last drew;
//   - the 4.3 vertex buffer bindings are kept and set as glVertexAttribPointer at each draw (base vertex included);
//   - S3TC is decoded on the CPU, 32-bit float depth textures become 24-bit ones, image copies go through two
//     framebuffers.
#if XBE_RSXGL

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "gl/gl_api.h"

namespace glapi {

namespace {

// RSXGL's own functions the stand ins call
void(APIENTRY* r_ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
void(APIENTRY* r_AttachShader)(GLuint, GLuint);
void(APIENTRY* r_LinkProgram)(GLuint);
void(APIENTRY* r_UseProgram)(GLuint);
void(APIENTRY* r_DeleteProgram)(GLuint);
void(APIENTRY* r_DeleteShader)(GLuint);
void(APIENTRY* r_BindBuffer)(GLenum, GLuint);
void(APIENTRY* r_BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
void(APIENTRY* r_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
void*(APIENTRY* r_MapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
GLboolean(APIENTRY* r_UnmapBuffer)(GLenum);
void(APIENTRY* r_DeleteBuffers)(GLsizei, const GLuint*);
void(APIENTRY* r_EnableVertexAttribArray)(GLuint);
void(APIENTRY* r_DisableVertexAttribArray)(GLuint);
void(APIENTRY* r_DrawArrays)(GLenum, GLint, GLsizei);
void(APIENTRY* r_DrawElements)(GLenum, GLsizei, GLenum, const void*);
void(APIENTRY* r_ActiveTexture)(GLenum);
void(APIENTRY* r_BindTexture)(GLenum, GLuint);
void(APIENTRY* r_DeleteTextures)(GLsizei, const GLuint*);
void(APIENTRY* r_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void(APIENTRY* r_TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void(APIENTRY* r_TexStorage2D)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
void(APIENTRY* r_BindFramebuffer)(GLenum, GLuint);
void(APIENTRY* VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
void(APIENTRY* BindAttribLocation)(GLuint, GLuint, const GLchar*);
void(APIENTRY* Uniform4fv)(GLint, GLsizei, const GLfloat*);
void(APIENTRY* UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);
void(APIENTRY* Uniform2f)(GLint, GLfloat, GLfloat);
void(APIENTRY* ClearDepthf)(GLfloat);

// the attribute the full screen triangle's corner comes in (gl_VertexID's stand in)
constexpr GLuint kCornerAttrib = 15;

// GLSL 4.30 -> 1.30 -------------------------------------------------------------------------------------------------

// a uniform block's field: name, vec4 slots, a matrix (mat4) or vectors (vec4), the elements of an array
struct Field {
    std::string name;
    uint32_t offset = 0;  // bytes into the block
    uint32_t slots = 1;   // vec4s
    bool matrix = false;
};

// what a shader's text said before its layout() qualifiers went
struct ShaderInfo {
    std::vector<std::pair<std::string, int>> attribs;   // in vec4 v3 at location 3
    std::vector<std::pair<std::string, int>> samplers;  // tex0 at unit 0
    std::vector<std::string> sized;                     // samplers read through rsx_size_<name>
    bool corner = false;                                // reads the full screen triangle's corner
};

std::unordered_map<GLuint, ShaderInfo> g_shaders;
std::vector<Field> g_blocks[2];  // the uniform blocks at bindings 0 and 1 (the same in every program)

bool Ident(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
}

// every whole-word `word` (not part of a longer name) replaced
void ReplaceWord(std::string& s, const std::string& word, const std::string& to) {
    for (size_t p = 0; (p = s.find(word, p)) != std::string::npos;) {
        bool before = p > 0 && (Ident(s[p - 1]) || s[p - 1] == '.');
        bool after = p + word.size() < s.size() && Ident(s[p + word.size()]);
        if (before || after) {
            p += word.size();
            continue;
        }
        s.replace(p, word.size(), to);
        p += to.size();
    }
}

// the arguments of the call whose '(' is at open: their text, and where the ')' is
std::vector<std::string> Args(const std::string& s, size_t open, size_t& close) {
    std::vector<std::string> out;
    int depth = 0;
    size_t from = open + 1;
    for (size_t i = open; i < s.size(); i++) {
        char c = s[i];
        if (c == '(') depth++;
        else if (c == ')' && --depth == 0) {
            out.push_back(s.substr(from, i - from));
            close = i;
            return out;
        } else if (c == ',' && depth == 1) {
            out.push_back(s.substr(from, i - from));
            from = i + 1;
        }
    }
    close = std::string::npos;
    return out;
}

std::string Trim(std::string t) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\n')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
    return t;
}

// "layout(std140, binding = N) uniform NAME { fields };" -> the fields as plain uniforms, kept as block N's
void UniformBlocks(std::string& s) {
    for (size_t p = 0; (p = s.find("layout(std140, binding = ", p)) != std::string::npos;) {
        int binding = std::atoi(s.c_str() + p + 25);
        size_t open = s.find('{', p), close = s.find("};", p);
        if (open == std::string::npos || close == std::string::npos || binding < 0 || binding > 1) break;
        std::string body = s.substr(open + 1, close - open - 1), plain;
        std::vector<Field> fields;
        uint32_t at = 0;
        for (size_t l = 0; l < body.size();) {
            size_t semi = body.find(';', l);
            if (semi == std::string::npos) break;
            std::string decl = Trim(body.substr(l, semi - l));
            l = semi + 1;
            if (decl.empty()) continue;
            Field f;
            f.matrix = decl.rfind("mat4", 0) == 0;
            std::string name = Trim(decl.substr(4));
            uint32_t n = 1;
            size_t br = name.find('[');
            if (br != std::string::npos) {
                n = uint32_t(std::atoi(name.c_str() + br + 1));
                name.resize(br);
            }
            f.name = name;
            f.offset = at;
            f.slots = n * (f.matrix ? 4 : 1);
            at += f.slots * 16;
            fields.push_back(f);
            plain += "uniform " + decl + ";\n";
        }
        if (g_blocks[binding].empty()) g_blocks[binding] = fields;
        s.replace(p, close + 2 - p, plain);
    }
}

// "out NAME { [qualifier] type field; ... } inst;" -> "[qualifier] out type NAME_field;", inst.field -> NAME_field
void InterfaceBlocks(std::string& s) {
    for (const char* dir : {"out ", "in "}) {
        for (size_t p = 0; (p = s.find(dir, p)) != std::string::npos; p++) {
            if (p > 0 && Ident(s[p - 1])) continue;
            size_t name_at = p + std::strlen(dir), brace = s.find(' ', name_at);
            if (brace == std::string::npos || brace + 1 >= s.size() || s[brace + 1] != '{') continue;
            std::string block = s.substr(name_at, brace - name_at);
            if (block.empty() || !Ident(block[0])) continue;
            size_t close = s.find('}', brace);
            size_t semi = close == std::string::npos ? std::string::npos : s.find(';', close);
            if (semi == std::string::npos) break;
            std::string inst = Trim(s.substr(close + 1, semi - close - 1));
            std::string body = s.substr(brace + 2, close - brace - 2), flat;
            std::vector<std::string> members;
            for (size_t l = 0; l < body.size();) {
                size_t e = body.find(';', l);
                if (e == std::string::npos) break;
                std::string decl = Trim(body.substr(l, e - l));
                l = e + 1;
                if (decl.empty()) continue;
                size_t sp = decl.rfind(' ');
                std::string member = decl.substr(sp + 1), lead = decl.substr(0, sp);  // "[qualifier] type"
                size_t q = lead.rfind(' ');
                std::string qual = q == std::string::npos ? "" : lead.substr(0, q + 1), type = q == std::string::npos ? lead : lead.substr(q + 1);
                flat += qual + std::string(dir) + type + " " + block + "_" + member + ";\n";
                members.push_back(member);
            }
            s.replace(p, semi + 1 - p, flat);
            for (const std::string& m : members) ReplaceAll(s, inst + "." + m, block + "_" + m);
        }
    }
}

// "layout(location = N) in TYPE NAME;" and "layout(binding = N) uniform TYPE NAME;": the qualifier gone, kept by name
void Layouts(std::string& s, ShaderInfo& info) {
    for (size_t p = 0; (p = s.find("layout(", p)) != std::string::npos;) {
        size_t close = s.find(')', p);
        if (close == std::string::npos) break;
        std::string q = s.substr(p + 7, close - p - 7);
        size_t eq = q.find('=');
        int n = eq == std::string::npos ? 0 : std::atoi(q.c_str() + eq + 1);
        size_t semi = s.find(';', close);
        std::string decl = Trim(s.substr(close + 1, semi - close - 1));  // "in vec4 v3", "uniform sampler2D tex0"
        std::string name = decl.substr(decl.rfind(' ') + 1);
        if (q.find("location") != std::string::npos && decl.rfind("in ", 0) == 0) info.attribs.push_back({name, n});
        if (q.find("binding") != std::string::npos && decl.rfind("uniform ", 0) == 0) info.samplers.push_back({name, n});
        size_t from = close + 1;
        while (from < s.size() && s[from] == ' ') from++;
        s.erase(p, from - p);
    }
}

bool IsSampler(const ShaderInfo& info, const std::string& name) {
    for (auto& [n, unit] : info.samplers)
        if (n == name) return true;
    return false;
}

// textureSize(t, 0) and texelFetch(t, p, 0) through a uniform with t's size (set at each draw from the texture
// bound to t's unit); a sampler passed into a function has no unit: a stand in size (those paths are not used here)
void TextureSizes(std::string& s, ShaderInfo& info) {
    auto sized = [&](const std::string& t) {
        if (!IsSampler(info, t)) return std::string("vec2(256.0)");
        if (std::find(info.sized.begin(), info.sized.end(), t) == info.sized.end()) info.sized.push_back(t);
        return "rsx_size_" + t;
    };
    for (size_t p = 0; (p = s.find("textureSize(", p)) != std::string::npos;) {
        size_t close;
        std::vector<std::string> a = Args(s, p + 11, close);
        if (close == std::string::npos || a.empty()) break;
        std::string to = "ivec2(" + sized(Trim(a[0])) + ")";
        s.replace(p, close + 1 - p, to);
        p += to.size();
    }
    for (size_t p = 0; (p = s.find("texelFetch(", p)) != std::string::npos;) {
        size_t close;
        std::vector<std::string> a = Args(s, p + 10, close);
        if (close == std::string::npos || a.size() < 2) break;
        std::string t = Trim(a[0]);
        std::string to = "textureLod(" + t + ", (vec2(" + Trim(a[1]) + ") + 0.5) / " + sized(t) + ", 0.0)";
        s.replace(p, close + 1 - p, to);
        p += to.size();
    }
}

// calls the RSX's compiler has no instruction for: round(x) -> floor(x + 0.5), ceil(x) -> -floor(-x)
void Rounding(std::string& s) {
    for (const char* fn : {"round(", "ceil("}) {
        size_t n = std::strlen(fn);
        for (size_t p = 0; (p = s.find(fn, p)) != std::string::npos;) {
            if (p > 0 && (Ident(s[p - 1]) || s[p - 1] == '.')) {
                p += n;
                continue;
            }
            size_t close;
            std::vector<std::string> a = Args(s, p + n - 1, close);
            if (close == std::string::npos || a.size() != 1) break;
            std::string to = fn[0] == 'r' ? "floor((" + a[0] + ") + 0.5)" : "(-floor(-(" + a[0] + ")))";
            s.replace(p, close + 1 - p, to);
            p += to.size();
        }
    }
}

std::string ToGlsl130(std::string s, ShaderInfo& info) {
    ReplaceAll(s, "#version 430 core", "#version 130");
    ReplaceAll(s, "precise ", "");
    ReplaceAll(s, "uintBitsToFloat(0x7F800000u)", "(1.0e30 * 1.0e30)");
    // the alpha test's lowest bit, the depth words as colour: float arithmetic (no integers on the RSX)
    ReplaceAll(s, "(uint(r0.a * 255.0) & 1u) == 1u", "mod(floor(r0.a * 255.0), 2.0) == 1.0");
    ReplaceAll(s, "uint z = uint(", "float z = floor(");
    ReplaceAll(s, "o = vec4(float((z >> 8) & 255u), float(z & 255u), 0.0, float(z >> 16)) / 255.0;",
               "o = vec4(mod(floor(z / 256.0), 256.0), mod(z, 256.0), 0.0, floor(z / 65536.0)) / 255.0;");
    // the RSX runs 512 vertex program instructions at most: the Xbox's multiply (0 times anything is 0) as a plain one
    // (it took ~6 instructions a multiply: past 512 in the longest programs) and the constant index without its
    // bounds (2 instructions a read; the RSX's 468 constants hold the 192 read past their end)
    ReplaceAll(s, "vec4 mac_mul(vec4 a, vec4 b) { return mix(a * b, vec4(0.0), bvec4(vec4(equal(a, vec4(0.0))) + vec4(equal(b, vec4(0.0))))); }",
               "vec4 mac_mul(vec4 a, vec4 b) { return a * b; }");
    for (size_t p = 0; (p = s.find("c[clamp(a0 + ", p)) != std::string::npos;) {
        size_t end = s.find(", 0, 191)]", p);
        if (end == std::string::npos) break;
        s.replace(end, 10, ")]");
        s.replace(p, 13, "c[(a0 + ");
        p += 8;
    }
    if (s.find("gl_VertexID") != std::string::npos) {
        ReplaceAll(s, "vec2 t = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);", "vec2 t = rsx_corner;");
        info.corner = true;
        info.attribs.push_back({"rsx_corner", int(kCornerAttrib)});
    }
    UniformBlocks(s);
    InterfaceBlocks(s);
    Layouts(s, info);
    TextureSizes(s, info);
    Rounding(s);
    // the declarations the rewrite added, after the version line
    std::string added;
    if (info.corner) added += "in vec2 rsx_corner;\n";
    for (const std::string& t : info.sized) added += "uniform vec2 rsx_size_" + t + ";\n";
    size_t eol = s.find('\n');
    if (!added.empty() && eol != std::string::npos) s.insert(eol + 1, added);
    for (const char* left : {"uint", "<<", ">>", "gl_VertexID"})
        if (s.find(left) != std::string::npos) LOG_FIRST(8, "gl: GLSL 1.30 rewrite left '%s' in a shader", left);
    return s;
}

// programs ------------------------------------------------------------------------------------------------------------

struct UniformField {
    GLint loc;
    const Field* field;
    uint8_t block;
};
struct SizeUniform {
    GLint loc;
    int unit;
    float w = -1, h = -1;  // last sent
};
struct Program {
    std::vector<GLuint> shaders;
    std::vector<UniformField> fields;
    std::vector<uint8_t> last[2];  // each block's bytes as last sent to this program
    std::vector<SizeUniform> sizes;
    bool corner = false;
};
std::unordered_map<GLuint, Program> g_programs;
GLuint g_program = 0;

void APIENTRY ShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string s;
    for (GLsizei i = 0; i < count; i++) s += lengths && lengths[i] >= 0 ? std::string(strings[i], size_t(lengths[i])) : std::string(strings[i]);
    ShaderInfo& info = g_shaders[shader];
    info = ShaderInfo{};
    s = ToGlsl130(std::move(s), info);
    const GLchar* src = s.c_str();
    r_ShaderSource(shader, 1, &src, nullptr);
}

void APIENTRY DeleteShader(GLuint shader) {
    g_shaders.erase(shader);
    r_DeleteShader(shader);
}

void APIENTRY AttachShader(GLuint program, GLuint shader) {
    g_programs[program].shaders.push_back(shader);
    r_AttachShader(program, shader);
}

void APIENTRY LinkProgram(GLuint program) {
    Program& p = g_programs[program];
    std::vector<std::pair<std::string, int>> samplers;
    std::vector<std::string> sized;
    for (GLuint sh : p.shaders) {
        auto it = g_shaders.find(sh);
        if (it == g_shaders.end()) continue;
        for (auto& [name, loc] : it->second.attribs) BindAttribLocation(program, GLuint(loc), name.c_str());
        samplers.insert(samplers.end(), it->second.samplers.begin(), it->second.samplers.end());
        sized.insert(sized.end(), it->second.sized.begin(), it->second.sized.end());
        p.corner |= it->second.corner;
    }
    r_LinkProgram(program);
    // the blocks' fields this program has, the samplers' units, the size uniforms
    p.fields.clear();
    for (uint8_t b = 0; b < 2; b++) {
        size_t bytes = 0;
        for (const Field& f : g_blocks[b]) {
            GLint loc = glGetUniformLocation(program, f.name.c_str());
            if (loc >= 0) p.fields.push_back({loc, &f, b});
            bytes = std::max<size_t>(bytes, f.offset + f.slots * 16);
        }
        p.last[b].assign(bytes, 0xFF);  // NaN everywhere: the first draw sends everything
    }
    r_UseProgram(program);
    for (auto& [name, unit] : samplers) {
        GLint loc = glGetUniformLocation(program, name.c_str());
        if (loc >= 0) glUniform1i(loc, unit);
    }
    r_UseProgram(g_program);
    p.sizes.clear();
    for (const std::string& t : sized) {
        GLint loc = glGetUniformLocation(program, ("rsx_size_" + t).c_str());
        int unit = 0;
        for (auto& [name, u] : samplers)
            if (name == t) unit = u;
        if (loc >= 0) p.sizes.push_back({loc, unit});
    }
}

void APIENTRY UseProgram(GLuint program) {
    g_program = program;
    r_UseProgram(program);
}

void APIENTRY DeleteProgram(GLuint program) {
    g_programs.erase(program);
    if (g_program == program) g_program = 0;
    r_DeleteProgram(program);
}

GLuint APIENTRY GetUniformBlockIndex(GLuint, const GLchar* name) { return std::strcmp(name, "DC") == 0 ? 1 : 0; }
void APIENTRY UniformBlockBinding(GLuint, GLuint, GLuint) {}  // the blocks keep their layout() bindings

// uniform buffers, on the CPU -------------------------------------------------------------------------------------

std::unordered_map<GLuint, std::vector<uint8_t>> g_uniform_buffers;
GLuint g_uniform_bound = 0;
struct Slot {
    GLuint buf = 0;
    GLintptr offset = 0;
};
Slot g_slots[2];

void APIENTRY BindBuffer(GLenum target, GLuint buf) {
    if (target == GL_UNIFORM_BUFFER) g_uniform_bound = buf;
    else r_BindBuffer(target, buf);
}
void APIENTRY BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (target != GL_UNIFORM_BUFFER) return r_BufferData(target, size, data, usage);
    std::vector<uint8_t>& b = g_uniform_buffers[g_uniform_bound];
    b.assign(size_t(size), 0);
    if (data) std::memcpy(b.data(), data, size_t(size));
}
void APIENTRY BufferSubData(GLenum target, GLintptr at, GLsizeiptr size, const void* data) {
    if (target != GL_UNIFORM_BUFFER) return r_BufferSubData(target, at, size, data);
    std::vector<uint8_t>& b = g_uniform_buffers[g_uniform_bound];
    if (size_t(at + size) <= b.size()) std::memcpy(b.data() + at, data, size_t(size));
}
void* APIENTRY MapBufferRange(GLenum target, GLintptr at, GLsizeiptr size, GLbitfield access) {
    if (target != GL_UNIFORM_BUFFER) return r_MapBufferRange(target, at, size, access);
    std::vector<uint8_t>& b = g_uniform_buffers[g_uniform_bound];
    return size_t(at + size) <= b.size() ? b.data() + at : nullptr;
}
GLboolean APIENTRY UnmapBuffer(GLenum target) { return target == GL_UNIFORM_BUFFER ? GLboolean(GL_TRUE) : r_UnmapBuffer(target); }
void APIENTRY DeleteBuffers(GLsizei n, const GLuint* bufs) {
    for (GLsizei i = 0; i < n; i++) g_uniform_buffers.erase(bufs[i]);
    r_DeleteBuffers(n, bufs);
}
void APIENTRY BindBufferRange(GLenum target, GLuint index, GLuint buf, GLintptr at, GLsizeiptr) {
    if (target == GL_UNIFORM_BUFFER && index < 2) g_slots[index] = {buf, at};
}
void APIENTRY BindBufferBase(GLenum target, GLuint index, GLuint buf) { BindBufferRange(target, index, buf, 0, 0); }

// the fields of the program's blocks that changed since it last drew
void SendUniforms(Program& p) {
    const uint8_t* src[2] = {};
    for (int b = 0; b < 2; b++) {
        auto it = g_uniform_buffers.find(g_slots[b].buf);
        if (it != g_uniform_buffers.end() && size_t(g_slots[b].offset) + p.last[b].size() <= it->second.size())
            src[b] = it->second.data() + g_slots[b].offset;
    }
    for (const UniformField& u : p.fields) {
        const uint8_t* from = src[u.block];
        if (!from) continue;
        const Field& f = *u.field;
        uint8_t* last = p.last[u.block].data() + f.offset;
        size_t bytes = size_t(f.slots) * 16;
        if (std::memcmp(last, from + f.offset, bytes) == 0) continue;
        std::memcpy(last, from + f.offset, bytes);
        const GLfloat* v = reinterpret_cast<const GLfloat*>(from + f.offset);
        if (f.matrix) UniformMatrix4fv(u.loc, GLsizei(f.slots / 4), GL_FALSE, v);
        else Uniform4fv(u.loc, GLsizei(f.slots), v);
    }
}

// textures: sizes for rsx_size_* -----------------------------------------------------------------------------------

struct TexInfo {
    GLsizei w = 0, h = 0;
    GLint internal = GL_RGBA8;
};
std::unordered_map<GLuint, TexInfo> g_textures;
GLuint g_bound2d[16];
GLuint g_unit = 0;

void APIENTRY ActiveTexture(GLenum unit) {
    g_unit = (unit - GL_TEXTURE0) & 15;
    r_ActiveTexture(unit);
}
void APIENTRY BindTexture(GLenum target, GLuint tex) {
    if (target == GL_TEXTURE_2D) g_bound2d[g_unit] = tex;
    r_BindTexture(target, tex);
}
void APIENTRY DeleteTextures(GLsizei n, const GLuint* t) {
    for (GLsizei i = 0; i < n; i++) g_textures.erase(t[i]);
    r_DeleteTextures(n, t);
}
void Sized(GLenum target, GLint level, GLsizei w, GLsizei h, GLint internal) {
    if (target == GL_TEXTURE_2D && level == 0) g_textures[g_bound2d[g_unit]] = {w, h, internal};
}

void APIENTRY TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format,
                         GLenum type, const void* data) {
    Sized(target, level, w, h, internal);
    if (internal == GL_DEPTH_COMPONENT32F) internal = GL_DEPTH_COMPONENT24;  // the RSX's depth textures: 24 or 16 bits
    r_TexImage2D(target, level, internal, w, h, border, format, type, data);
}
void APIENTRY TexStorage2D(GLenum target, GLsizei levels, GLenum internal, GLsizei w, GLsizei h) {
    Sized(target, 0, w, h, GLint(internal));
    r_TexStorage2D(target, levels, internal == GL_DEPTH_COMPONENT32F ? GL_DEPTH_COMPONENT24 : internal, w, h);
}

bool IsS3tc(GLenum f) { return f >= GL_COMPRESSED_RGBA_S3TC_DXT1_EXT && f <= GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; }

void APIENTRY CompressedTexImage2D(GLenum target, GLint level, GLenum format, GLsizei w, GLsizei h, GLint, GLsizei size,
                                   const void* data) {
    if (!IsS3tc(format)) return;
    std::vector<uint8_t> px = DecodeS3tc(format, w, h, static_cast<const uint8_t*>(data), size);
    TexImage2D(target, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}
void APIENTRY CompressedTexImage3D(GLenum target, GLint level, GLenum format, GLsizei w, GLsizei h, GLsizei d, GLint,
                                   GLsizei size, const void* data) {
    if (!IsS3tc(format)) return;
    size_t slice = size_t((w + 3) / 4) * size_t((h + 3) / 4) * (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16);
    std::vector<uint8_t> px;
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (GLsizei z = 0; z < d; z++) {
        size_t at = slice * size_t(z);
        GLsizei left = at < size_t(size) ? GLsizei(size_t(size) - at) : 0;
        std::vector<uint8_t> one = DecodeS3tc(format, w, h, src + at, left);
        px.insert(px.end(), one.begin(), one.end());
    }
    r_TexImage3D(target, level, GL_RGBA8, w, h, d, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

void SendSizes(Program& p) {
    for (SizeUniform& s : p.sizes) {
        auto it = g_textures.find(g_bound2d[s.unit & 15]);
        float w = it == g_textures.end() ? 1.0f : float(it->second.w), h = it == g_textures.end() ? 1.0f : float(it->second.h);
        if (w == s.w && h == s.h) continue;
        s.w = w, s.h = h;
        Uniform2f(s.loc, w, h);
    }
}

// framebuffers: the bindings kept (RSXGL answers few glGet queries), image copies through two of them
GLuint g_read_fb = 0, g_draw_fb = 0;
void APIENTRY BindFramebuffer(GLenum target, GLuint fb) {
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) g_read_fb = fb;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) g_draw_fb = fb;
    r_BindFramebuffer(target, fb);
}

void APIENTRY CopyImageSubData(GLuint src, GLenum src_target, GLint src_level, GLint sx, GLint sy, GLint, GLuint dst,
                               GLenum dst_target, GLint dst_level, GLint dx, GLint dy, GLint, GLsizei w, GLsizei h, GLsizei) {
    static GLuint fbo[2];
    if (!fbo[0]) glGenFramebuffers(2, fbo);
    GLuint read = g_read_fb, draw = g_draw_fb;
    auto it = g_textures.find(src);
    GLint format = it == g_textures.end() ? GL_RGBA8 : it->second.internal;
    bool depth = format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH_COMPONENT32F || format == GL_DEPTH_COMPONENT24 ||
                 format == GL_DEPTH_COMPONENT16 || format == GL_DEPTH32F_STENCIL8;
    bool stencil = format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH32F_STENCIL8;
    GLenum at = stencil ? GL_DEPTH_STENCIL_ATTACHMENT : depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
    r_BindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, at, src_target, src, src_level);
    r_BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, at, dst_target, dst, dst_level);
    GLbitfield mask = depth ? (GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0)) : GL_COLOR_BUFFER_BIT;
    glBlitFramebuffer(sx, sy, sx + w, sy + h, dx, dy, dx + w, dy + h, mask, GL_NEAREST);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, at, src_target, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, at, dst_target, 0, 0);
    r_BindFramebuffer(GL_READ_FRAMEBUFFER, read);
    r_BindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
}

void APIENTRY ClearDepth(GLdouble d) { ClearDepthf(GLfloat(d)); }

// vertex buffer bindings ------------------------------------------------------------------------------------------

struct VertexBinding {
    GLuint buf = 0;
    GLintptr offset = 0;
    GLsizei stride = 0;
};
struct AttribFormat {
    GLint size = 4;
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    GLuint relative = 0, binding = 0;
    bool on = false;
};
VertexBinding g_vbind[16];
AttribFormat g_attr[16];
GLuint g_corner_buf = 0;

void APIENTRY BindVertexBuffer(GLuint index, GLuint buf, GLintptr offset, GLsizei stride) {
    if (index < 16) g_vbind[index] = {buf, offset, stride};
}
void APIENTRY VertexAttribFormat(GLuint a, GLint size, GLenum type, GLboolean normalized, GLuint relative) {
    if (a >= 16) return;
    g_attr[a].size = size, g_attr[a].type = type, g_attr[a].normalized = normalized, g_attr[a].relative = relative;
}
// integer attributes (packed formats read raw): the PS3 build draws converted vertices, these are read as floats
void APIENTRY VertexAttribIFormat(GLuint a, GLint size, GLenum type, GLuint relative) {
    VertexAttribFormat(a, size, type, GL_FALSE, relative);
}
void APIENTRY VertexAttribBinding(GLuint a, GLuint binding) {
    if (a < 16) g_attr[a].binding = binding & 15;
}
void APIENTRY EnableVertexAttribArray(GLuint a) {
    if (a < 16) g_attr[a].on = true;
    r_EnableVertexAttribArray(a);
}
void APIENTRY DisableVertexAttribArray(GLuint a) {
    if (a < 16) g_attr[a].on = false;
    r_DisableVertexAttribArray(a);
}

// what a draw needs set first: the attributes from their bindings (base vertex: from that vertex on), the program's
// changed uniforms, its textures' sizes, the full screen triangle's corners
void Prepare(GLint base) {
    for (GLuint a = 0; a < 16; a++) {
        const AttribFormat& f = g_attr[a];
        if (!f.on || a == kCornerAttrib) continue;
        const VertexBinding& b = g_vbind[f.binding];
        if (!b.buf) continue;
        r_BindBuffer(GL_ARRAY_BUFFER, b.buf);
        VertexAttribPointer(a, f.size, f.type, f.normalized, b.stride,
                            reinterpret_cast<const void*>(uintptr_t(b.offset + GLintptr(f.relative) + GLintptr(base) * b.stride)));
    }
    auto it = g_programs.find(g_program);
    if (it == g_programs.end()) return;
    Program& p = it->second;
    SendUniforms(p);
    SendSizes(p);
    if (p.corner) {
        if (!g_corner_buf) {
            static const float corners[6] = {0, 0, 2, 0, 0, 2};
            glGenBuffers(1, &g_corner_buf);
            r_BindBuffer(GL_ARRAY_BUFFER, g_corner_buf);
            r_BufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
        }
        r_BindBuffer(GL_ARRAY_BUFFER, g_corner_buf);
        VertexAttribPointer(kCornerAttrib, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
        r_EnableVertexAttribArray(kCornerAttrib);
    } else {
        r_DisableVertexAttribArray(kCornerAttrib);
    }
}

void APIENTRY DrawArrays(GLenum mode, GLint first, GLsizei count) {
    Prepare(0);
    r_DrawArrays(mode, first, count);
}
void APIENTRY DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    Prepare(0);
    r_DrawElements(mode, count, type, indices);
}
void APIENTRY DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base) {
    Prepare(base);
    r_DrawElements(mode, count, type, indices);
}

template <class F>
void Need(F& f, void* (*get)(const char*), const char* name, bool& ok) {
    f = reinterpret_cast<F>(get(name));
    if (!f) {
        LOGE("gl: RSXGL has no %s", name);
        ok = false;
    }
}

}  // namespace

bool Load(void* (*get)(const char*), const char** missing) {
    bool ok = true;
    Need(VertexAttribPointer, get, "glVertexAttribPointer", ok);
    Need(BindAttribLocation, get, "glBindAttribLocation", ok);
    Need(Uniform4fv, get, "glUniform4fv", ok);
    Need(UniformMatrix4fv, get, "glUniformMatrix4fv", ok);
    Need(Uniform2f, get, "glUniform2f", ok);
    Need(ClearDepthf, get, "glClearDepthf", ok);
    if (!ok) {
        if (missing) *missing = "an RSXGL function";
        return false;
    }
    // what RSXGL lacks first, so the required loop finds them
    glBindVertexBuffer = BindVertexBuffer;
    glVertexAttribFormat = VertexAttribFormat;
    glVertexAttribIFormat = VertexAttribIFormat;
    glVertexAttribBinding = VertexAttribBinding;
    glGetUniformBlockIndex = GetUniformBlockIndex;
    glUniformBlockBinding = UniformBlockBinding;
    glCopyImageSubData = CopyImageSubData;
    glClearDepth = ClearDepth;
#define GLAPI_LOAD(ret, name, params)                                           \
    if (!name) name = reinterpret_cast<ret(APIENTRY*) params>(get(#name));      \
    if (!name) {                                                                \
        if (missing) *missing = #name;                                          \
        return false;                                                           \
    }
    GL_FUNCTIONS(GLAPI_LOAD)
#undef GLAPI_LOAD
    // optional: none (no persistent mapping, no transform feedback)
#define WRAP(name, stand_in) r_##stand_in = name, name = stand_in;
    WRAP(glShaderSource, ShaderSource)
    WRAP(glDeleteShader, DeleteShader)
    WRAP(glAttachShader, AttachShader)
    WRAP(glLinkProgram, LinkProgram)
    WRAP(glUseProgram, UseProgram)
    WRAP(glDeleteProgram, DeleteProgram)
    WRAP(glBindBuffer, BindBuffer)
    WRAP(glBufferData, BufferData)
    WRAP(glBufferSubData, BufferSubData)
    WRAP(glMapBufferRange, MapBufferRange)
    WRAP(glUnmapBuffer, UnmapBuffer)
    WRAP(glDeleteBuffers, DeleteBuffers)
    WRAP(glEnableVertexAttribArray, EnableVertexAttribArray)
    WRAP(glDisableVertexAttribArray, DisableVertexAttribArray)
    WRAP(glDrawArrays, DrawArrays)
    WRAP(glDrawElements, DrawElements)
    WRAP(glActiveTexture, ActiveTexture)
    WRAP(glBindTexture, BindTexture)
    WRAP(glDeleteTextures, DeleteTextures)
    WRAP(glTexImage2D, TexImage2D)
    WRAP(glTexStorage2D, TexStorage2D)
    WRAP(glBindFramebuffer, BindFramebuffer)
#undef WRAP
    // these replace RSXGL's own outright
    r_TexImage3D = glTexImage3D;
    glBindBufferRange = BindBufferRange;
    glBindBufferBase = BindBufferBase;
    glDrawElementsBaseVertex = DrawElementsBaseVertex;
    glCompressedTexImage2D = CompressedTexImage2D;
    glCompressedTexImage3D = CompressedTexImage3D;
    return true;
}

}  // namespace glapi

#endif  // XBE_RSXGL
