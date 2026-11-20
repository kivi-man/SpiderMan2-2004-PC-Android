#include "gl/gl_api.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace glapi {

#define GLAPI_DEFINE(ret, name, params) ret(APIENTRY* name) params = nullptr;
GL_FUNCTIONS(GLAPI_DEFINE)
GL_OPTIONAL_FUNCTIONS(GLAPI_DEFINE)
#undef GLAPI_DEFINE

#if XBE_GLES || XBE_RSXGL
namespace {

// S3TC blocks into RGBA8, for GPUs without the extension (most phones but Adreno)
void Rgb565(uint16_t c, uint8_t* o) {
    o[0] = uint8_t((c >> 11 & 31) * 255 / 31);
    o[1] = uint8_t((c >> 5 & 63) * 255 / 63);
    o[2] = uint8_t((c & 31) * 255 / 31);
    o[3] = 255;
}

}  // namespace

std::vector<uint8_t> DecodeS3tc(GLenum format, GLsizei w, GLsizei h, const uint8_t* src, GLsizei size) {
    std::vector<uint8_t> out(size_t(w) * size_t(h) * 4);
    const bool bc1 = format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    const uint32_t block = bc1 ? 8 : 16;
    const uint32_t bw = uint32_t(w + 3) / 4, bh = uint32_t(h + 3) / 4;
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            size_t at = (size_t(by) * bw + bx) * block;
            if (at + block > size_t(size)) return out;
            const uint8_t* b = src + at;
            const uint8_t* cb = bc1 ? b : b + 8;
            uint16_t e0 = uint16_t(cb[0] | cb[1] << 8), e1 = uint16_t(cb[2] | cb[3] << 8);
            uint8_t pal[4][4];
            Rgb565(e0, pal[0]);
            Rgb565(e1, pal[1]);
            if (!bc1 || e0 > e1) {
                for (int c = 0; c < 3; c++) {
                    pal[2][c] = uint8_t((2 * pal[0][c] + pal[1][c]) / 3);
                    pal[3][c] = uint8_t((pal[0][c] + 2 * pal[1][c]) / 3);
                }
                pal[2][3] = pal[3][3] = 255;
            } else {
                for (int c = 0; c < 3; c++) pal[2][c] = uint8_t((pal[0][c] + pal[1][c]) / 2);
                pal[2][3] = 255;
                pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
            }
            uint32_t idx = uint32_t(cb[4] | cb[5] << 8 | cb[6] << 16 | uint32_t(cb[7]) << 24);
            uint8_t alpha[16];
            if (format == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT) {
                for (int i = 0; i < 16; i++) alpha[i] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
            } else if (format == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
                uint8_t a0 = b[0], a1 = b[1], ap[8] = {a0, a1};
                if (a0 > a1)
                    for (int i = 1; i < 7; i++) ap[i + 1] = uint8_t(((7 - i) * a0 + i * a1) / 7);
                else {
                    for (int i = 1; i < 5; i++) ap[i + 1] = uint8_t(((5 - i) * a0 + i * a1) / 5);
                    ap[6] = 0, ap[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = ap[(bits >> (3 * i)) & 7];
            }
            for (int i = 0; i < 16; i++) {
                uint32_t x = bx * 4 + uint32_t(i & 3), y = by * 4 + uint32_t(i >> 2);
                if (x >= uint32_t(w) || y >= uint32_t(h)) continue;
                uint8_t* o = &out[(size_t(y) * size_t(w) + x) * 4];
                std::memcpy(o, pal[(idx >> (2 * i)) & 3], 4);
                if (!bc1) o[3] = alpha[i];
            }
        }
    return out;
}
#endif

#if XBE_GLES
// OpenGL ES 3.2 (Android): the renderer keeps calling the desktop names; what ES lacks or does differently goes through
// these stand ins, set over the loaded pointers. BGRA pixels are swapped to RGBA on the CPU, S3TC blocks are decoded
// when the GPU has no S3TC, shaders get an ES header, and the desktop only calls become their ES forms or nothing.
namespace {

bool g_s3tc = false, g_noperspective = false;
bool g_es31 = false;  // OpenGL ES 3.1 (the emulator, older GPUs): no base vertex draws, image copies, border clamp, precise

void(APIENTRY* real_BindVertexBuffer)(GLuint, GLuint, GLintptr, GLsizei);
void(APIENTRY* real_SamplerParameteri)(GLuint, GLenum, GLint);
void(APIENTRY* real_SamplerParameterfv)(GLuint, GLenum, const GLfloat*);
void(APIENTRY* GetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*);

void(APIENTRY* real_TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void(APIENTRY* real_TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void(APIENTRY* real_TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
void(APIENTRY* real_ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
void(APIENTRY* real_CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
void(APIENTRY* real_CompressedTexImage3D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const void*);
void(APIENTRY* real_ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
void(APIENTRY* real_BeginQuery)(GLenum, GLuint);
void(APIENTRY* real_EndQuery)(GLenum);
void(APIENTRY* real_GetQueryObjectuiv)(GLuint, GLenum, GLuint*);
void(APIENTRY* real_SamplerParameterf)(GLuint, GLenum, GLfloat);
void(APIENTRY* real_Enable)(GLenum);
void(APIENTRY* real_Disable)(GLenum);
void(APIENTRY* ClearDepthf)(GLfloat);
void(APIENTRY* DrawBuffers)(GLsizei, const GLenum*);

constexpr GLenum kAnySamplesConservative = 0x8D6A;  // GL_ANY_SAMPLES_PASSED_CONSERVATIVE

// RGBA8 rows, red and blue swapped
std::vector<uint8_t> Swapped(const void* src, size_t pixels) {
    std::vector<uint8_t> out(pixels * 4);
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < pixels; i++) {
        out[i * 4 + 0] = s[i * 4 + 2];
        out[i * 4 + 1] = s[i * 4 + 1];
        out[i * 4 + 2] = s[i * 4 + 0];
        out[i * 4 + 3] = s[i * 4 + 3];
    }
    return out;
}

void APIENTRY TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format,
                         GLenum type, const void* data) {
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        if (!data) return real_TexImage2D(target, level, internal, w, h, border, GL_RGBA, type, nullptr);
        std::vector<uint8_t> px = Swapped(data, size_t(w) * size_t(h));
        return real_TexImage2D(target, level, internal, w, h, border, GL_RGBA, type, px.data());
    }
    real_TexImage2D(target, level, internal, w, h, border, format, type, data);
}

void APIENTRY TexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d, GLint border,
                         GLenum format, GLenum type, const void* data) {
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        if (!data) return real_TexImage3D(target, level, internal, w, h, d, border, GL_RGBA, type, nullptr);
        std::vector<uint8_t> px = Swapped(data, size_t(w) * size_t(h) * size_t(d));
        return real_TexImage3D(target, level, internal, w, h, d, border, GL_RGBA, type, px.data());
    }
    real_TexImage3D(target, level, internal, w, h, d, border, format, type, data);
}

void APIENTRY TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                            const void* data) {
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE && data) {
        std::vector<uint8_t> px = Swapped(data, size_t(w) * size_t(h));
        return real_TexSubImage2D(target, level, x, y, w, h, GL_RGBA, type, px.data());
    }
    real_TexSubImage2D(target, level, x, y, w, h, format, type, data);
}

void APIENTRY ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) {
    if (format == GL_BGRA && type == GL_UNSIGNED_BYTE) {
        real_ReadPixels(x, y, w, h, GL_RGBA, type, data);
        uint8_t* p = static_cast<uint8_t*>(data);
        for (size_t i = 0, n = size_t(w) * size_t(h); i < n; i++) std::swap(p[i * 4], p[i * 4 + 2]);
        return;
    }
    real_ReadPixels(x, y, w, h, format, type, data);
}


bool IsS3tc(GLenum f) { return f >= GL_COMPRESSED_RGBA_S3TC_DXT1_EXT && f <= GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; }

void APIENTRY CompressedTexImage2D(GLenum target, GLint level, GLenum format, GLsizei w, GLsizei h, GLint border,
                                   GLsizei size, const void* data) {
    if (g_s3tc || !IsS3tc(format)) return real_CompressedTexImage2D(target, level, format, w, h, border, size, data);
    std::vector<uint8_t> px = DecodeS3tc(format, w, h, static_cast<const uint8_t*>(data), size);
    real_TexImage2D(target, level, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

void APIENTRY CompressedTexImage3D(GLenum target, GLint level, GLenum format, GLsizei w, GLsizei h, GLsizei d,
                                   GLint border, GLsizei size, const void* data) {
    if (g_s3tc || !IsS3tc(format)) return real_CompressedTexImage3D(target, level, format, w, h, d, border, size, data);
    // slices one after another, each a whole number of blocks
    size_t slice = size_t((w + 3) / 4) * size_t((h + 3) / 4) * (format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 8 : 16);
    std::vector<uint8_t> px;
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (GLsizei z = 0; z < d; z++) {
        size_t at = slice * size_t(z);
        GLsizei left = at < size_t(size) ? GLsizei(size_t(size) - at) : 0;
        std::vector<uint8_t> one = DecodeS3tc(format, w, h, src + at, left);
        px.insert(px.end(), one.begin(), one.end());
    }
    real_TexImage3D(target, level, GL_RGBA8, w, h, d, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
}

// "#version 430 core" into ES 3.20 with default precisions; noperspective only where the GPU has it
void APIENTRY ShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string s;
    for (GLsizei i = 0; i < count; i++) s += lengths && lengths[i] >= 0 ? std::string(strings[i], size_t(lengths[i])) : std::string(strings[i]);
    static const std::string kDesktop = "#version 430 core";
    std::string head =
        // 3.1 has the stages' interface blocks only as an extension
        std::string(g_es31 ? "#version 310 es\n#extension GL_EXT_shader_io_blocks : enable\n" : "#version 320 es\n") +
        // extensions before any code
        (g_noperspective ? "#extension GL_NV_shader_noperspective_interpolation : require\n" : "") +
        "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\nprecision highp sampler3D;\n"
        "precision highp samplerCube;\nprecision highp sampler2DShadow;\nprecision highp usampler2D;\n"
        "precision highp isampler2D;\nprecision highp sampler2DArray;\n";
    // no precise before 3.2: positions may differ in their last bit between passes
    if (g_es31)
        for (size_t p = 0; (p = s.find("precise ", p)) != std::string::npos;) s.erase(p, 8);
    size_t v = s.find(kDesktop);
    if (v != std::string::npos) {
        size_t eol = s.find('\n', v);
        s.replace(v, (eol == std::string::npos ? s.size() : eol + 1) - v, head);
    }
    if (!g_noperspective)
        for (size_t p = 0; (p = s.find("noperspective", p)) != std::string::npos;) s.replace(p, 13, "smooth");
    const GLchar* src = s.c_str();
    real_ShaderSource(shader, 1, &src, nullptr);
}

// ES counts no samples, it only tells whether any passed: a passing test reads as plenty of pixels
void APIENTRY BeginQuery(GLenum target, GLuint id) {
    real_BeginQuery(target == GL_SAMPLES_PASSED ? kAnySamplesConservative : target, id);
}
void APIENTRY EndQuery(GLenum target) { real_EndQuery(target == GL_SAMPLES_PASSED ? kAnySamplesConservative : target); }
void APIENTRY GetQueryObjectuiv(GLuint id, GLenum pname, GLuint* v) {
    real_GetQueryObjectuiv(id, pname, v);
    if (pname == GL_QUERY_RESULT && *v) *v = 1u << 20;
}

void APIENTRY SamplerParameterf(GLuint s, GLenum pname, GLfloat v) {
    if (pname != GL_TEXTURE_LOD_BIAS) real_SamplerParameterf(s, pname, v);
}
void APIENTRY Enable(GLenum cap) {
    if (cap != GL_DEPTH_CLAMP) real_Enable(cap);
}
void APIENTRY Disable(GLenum cap) {
    if (cap != GL_DEPTH_CLAMP) real_Disable(cap);
}
// ES 3.1 ----------------------------------------------------------------------------------------------------------
// the vertex buffer bindings, to draw with a base vertex by moving them
struct Binding {
    GLuint buffer = 0;
    GLintptr offset = 0;
    GLsizei stride = 0;
};
Binding g_bindings[16];

void APIENTRY BindVertexBuffer(GLuint index, GLuint buffer, GLintptr offset, GLsizei stride) {
    if (index < 16) g_bindings[index] = {buffer, offset, stride};
    real_BindVertexBuffer(index, buffer, offset, stride);
}

void APIENTRY DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices, GLint base) {
    if (base)
        for (GLuint i = 0; i < 16; i++)
            if (g_bindings[i].buffer)
                real_BindVertexBuffer(i, g_bindings[i].buffer, g_bindings[i].offset + GLintptr(base) * g_bindings[i].stride, g_bindings[i].stride);
    glDrawElements(mode, count, type, indices);
    if (base)
        for (GLuint i = 0; i < 16; i++)
            if (g_bindings[i].buffer) real_BindVertexBuffer(i, g_bindings[i].buffer, g_bindings[i].offset, g_bindings[i].stride);
}

// a texture level onto another through two framebuffers (2D textures, colour or depth)
void APIENTRY CopyImageSubData(GLuint src, GLenum src_target, GLint src_level, GLint sx, GLint sy, GLint, GLuint dst,
                               GLenum dst_target, GLint dst_level, GLint dx, GLint dy, GLint, GLsizei w, GLsizei h, GLsizei) {
    static GLuint fbo[2];
    if (!fbo[0]) glGenFramebuffers(2, fbo);
    GLint read = 0, draw = 0, bound = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    GLint format = GL_RGBA8;
    glBindTexture(GL_TEXTURE_2D, src);
    if (GetTexLevelParameteriv) GetTexLevelParameteriv(src_target, src_level, GL_TEXTURE_INTERNAL_FORMAT, &format);
    glBindTexture(GL_TEXTURE_2D, GLuint(bound));
    bool depth = format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH_COMPONENT32F || format == GL_DEPTH_COMPONENT24 ||
                 format == GL_DEPTH_COMPONENT16 || format == GL_DEPTH32F_STENCIL8;
    bool stencil = format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH32F_STENCIL8;
    GLenum at = stencil ? GL_DEPTH_STENCIL_ATTACHMENT : depth ? GL_DEPTH_ATTACHMENT : GL_COLOR_ATTACHMENT0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, at, src_target, src, src_level);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, at, dst_target, dst, dst_level);
    GLbitfield mask = depth ? (GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0)) : GL_COLOR_BUFFER_BIT;
    glBlitFramebuffer(sx, sy, sx + w, sy + h, dx, dy, dx + w, dy + h, mask, GL_NEAREST);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, at, src_target, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, at, dst_target, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw));
}

// no border colour: the edge's
void APIENTRY SamplerParameteri(GLuint s, GLenum pname, GLint v) {
    if (v == GL_CLAMP_TO_BORDER) v = GL_CLAMP_TO_EDGE;
    real_SamplerParameteri(s, pname, v);
}
void APIENTRY SamplerParameterfv(GLuint s, GLenum pname, const GLfloat* v) {
    if (pname != GL_TEXTURE_BORDER_COLOR) real_SamplerParameterfv(s, pname, v);
}

void APIENTRY ClearDepth(GLdouble d) { ClearDepthf(GLfloat(d)); }
void APIENTRY DrawBuffer(GLenum b) { DrawBuffers(1, &b); }
void APIENTRY PolygonMode(GLenum, GLenum) {}  // wireframe only; ES has none
void APIENTRY PointSize(GLfloat) {}           // ES sizes points in the shader

// a desktop name, else its ES or extension forms
void* Get(void* (*get)(const char*), const char* name) {
    if (void* f = get(name)) return f;
    for (const char* suffix : {"OES", "EXT"}) {
        std::string n = std::string(name) + suffix;
        if (void* f = get(n.c_str())) return f;
    }
    return nullptr;
}

}  // namespace

bool Load(void* (*get)(const char*), const char** missing) {
    if (auto get_string = reinterpret_cast<const GLubyte*(APIENTRY*)(GLenum)>(get("glGetString")))
        if (const char* v = reinterpret_cast<const char*>(get_string(GL_VERSION))) g_es31 = std::strstr(v, "OpenGL ES 3.1") != nullptr;
    if (g_es31) {
        glDrawElementsBaseVertex = DrawElementsBaseVertex;
        glCopyImageSubData = CopyImageSubData;
    }
    GetTexLevelParameteriv = reinterpret_cast<decltype(GetTexLevelParameteriv)>(get("glGetTexLevelParameteriv"));
    ClearDepthf = reinterpret_cast<decltype(ClearDepthf)>(get("glClearDepthf"));
    DrawBuffers = reinterpret_cast<decltype(DrawBuffers)>(get("glDrawBuffers"));
    // what ES lacks first, so the required loop finds them (a driver may still answer these names from another API)
    glClearDepth = ClearDepthf ? ClearDepth : nullptr;
    glDrawBuffer = DrawBuffers ? DrawBuffer : nullptr;
    glPolygonMode = PolygonMode;
    glPointSize = PointSize;
#define GLAPI_LOAD(ret, name, params)                                                         \
    if (!name) name = reinterpret_cast<ret(APIENTRY*) params>(Get(get, #name));               \
    if (!name && std::strcmp(#name, "glPolygonMode") == 0) name = reinterpret_cast<ret(APIENTRY*) params>(&PolygonMode); \
    if (!name && std::strcmp(#name, "glPointSize") == 0) name = reinterpret_cast<ret(APIENTRY*) params>(&PointSize);     \
    if (!name) {                                                                              \
        if (missing) *missing = #name;                                                        \
        return false;                                                                         \
    }
    GL_FUNCTIONS(GLAPI_LOAD)
#undef GLAPI_LOAD
#define GLAPI_LOAD_OPTIONAL(ret, name, params) name = reinterpret_cast<ret(APIENTRY*) params>(Get(get, #name));
    GL_OPTIONAL_FUNCTIONS(GLAPI_LOAD_OPTIONAL)
#undef GLAPI_LOAD_OPTIONAL
    g_s3tc = HasExtension("GL_EXT_texture_compression_s3tc") || HasExtension("GL_NV_texture_compression_s3tc");
    g_noperspective = HasExtension("GL_NV_shader_noperspective_interpolation");
#define WRAP(name, stand_in) real_##stand_in = name, name = stand_in;
    WRAP(glTexImage2D, TexImage2D)
    WRAP(glTexImage3D, TexImage3D)
    WRAP(glTexSubImage2D, TexSubImage2D)
    WRAP(glReadPixels, ReadPixels)
    WRAP(glCompressedTexImage2D, CompressedTexImage2D)
    WRAP(glCompressedTexImage3D, CompressedTexImage3D)
    WRAP(glShaderSource, ShaderSource)
    WRAP(glBeginQuery, BeginQuery)
    WRAP(glEndQuery, EndQuery)
    WRAP(glGetQueryObjectuiv, GetQueryObjectuiv)
    WRAP(glSamplerParameterf, SamplerParameterf)
    WRAP(glEnable, Enable)
    WRAP(glDisable, Disable)
    if (g_es31) {
        WRAP(glBindVertexBuffer, BindVertexBuffer)
        WRAP(glSamplerParameteri, SamplerParameteri)
        WRAP(glSamplerParameterfv, SamplerParameterfv)
    }
#undef WRAP
    return true;
}

#elif !XBE_RSXGL  // RSXGL's Load: gl/gl_rsx.cpp

bool Load(void* (*get)(const char*), const char** missing) {
#define GLAPI_LOAD(ret, name, params)                                       \
    name = reinterpret_cast<ret(APIENTRY*) params>(get(#name));             \
    if (!name) {                                                            \
        if (missing) *missing = #name;                                      \
        return false;                                                       \
    }
    GL_FUNCTIONS(GLAPI_LOAD)
#undef GLAPI_LOAD
#define GLAPI_LOAD_OPTIONAL(ret, name, params) name = reinterpret_cast<ret(APIENTRY*) params>(get(#name));
    GL_OPTIONAL_FUNCTIONS(GLAPI_LOAD_OPTIONAL)
#undef GLAPI_LOAD_OPTIONAL
    return true;
}

#endif

bool HasExtension(const char* want) {
#if XBE_RSXGL
    (void)want;
    return false;  // RSXGL lists none (and answers few glGet queries)
#endif
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (e && std::strcmp(e, want) == 0) return true;
    }
    return false;
}

}  // namespace glapi
