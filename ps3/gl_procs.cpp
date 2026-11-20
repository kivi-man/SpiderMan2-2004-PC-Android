// RSXGL's functions by name, for glapi::Load (gl/gl_rsx.cpp). RSXGL's eglGetProcAddress searches an unsorted table,
// so the names are resolved here at link time instead. The few the renderer calls that RSXGL lacks are weak (null:
// gl_rsx.cpp has stand ins for them); every other one is a plain reference, so the linker takes it from libGL.a.
#define GLAPI_NO_ALIAS 1
#include "gl/gl_api.h"

#include <cstring>

#define STR2(x) #x
#define STR(x) STR2(x)

// RSXGL has none of these
#define RSXGL_LACKS(X) \
    X(glBindVertexBuffer) X(glBufferStorage) X(glClearDepth) X(glCopyImageSubData) X(glGetUniformBlockIndex) \
    X(glUniformBlockBinding) X(glVertexAttribBinding) X(glVertexAttribFormat) X(glVertexAttribIFormat)
// the stand ins' own calls
#define RSXGL_EXTRA(X) X(glUniform4fv) X(glUniformMatrix4fv) X(glUniform2f) X(glBindAttribLocation) X(glClearDepthf)

extern "C" {
#define DECLARE(ret, name, params) void name();
GL_FUNCTIONS(DECLARE)
GL_OPTIONAL_FUNCTIONS(DECLARE)
#undef DECLARE
#define DECLARE1(name) void name();
RSXGL_EXTRA(DECLARE1)
#undef DECLARE1
#define WEAK(name) _Pragma(STR(weak name))
RSXGL_LACKS(WEAK)
#undef WEAK
}

namespace ps3 {

namespace {
struct Proc {
    const char* name;
    void (*fn)();
};
// the optional functions (transform feedback, persistent mapping) are left out: the PS3 build uses none
const Proc kProcs[] = {
#define ENTRY(ret, name, params) {#name, &name},
    GL_FUNCTIONS(ENTRY)
#undef ENTRY
#define ENTRY1(name) {#name, &name},
    RSXGL_EXTRA(ENTRY1)
#undef ENTRY1
};
}  // namespace

void* GlProc(const char* name) {
    for (const Proc& p : kProcs)
        if (std::strcmp(p.name, name) == 0) return reinterpret_cast<void*>(p.fn);
    return nullptr;
}

}  // namespace ps3
