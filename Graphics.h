
// Graphics.h - drawing routines
// - prerendered text is stored in a texture
// 
// Created on 10/31/12.
// 

// Copyright (c) 2013 Arthur Danskin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef __Outlaws__Graphics__
#define __Outlaws__Graphics__

#include "RGB.h"
#include "Vertex.h"

extern uint graphicsDrawCount;
extern uint gpuMemoryUsed;
extern bool supports_ARB_Framebuffer_object;

GLenum glReportError1(const char *file, uint line, const char *function);
GLenum glReportFramebufferError1(const char *file, uint line, const char *function);
void   glReportValidateShaderError1(const char* file, uint line, const char* function, GLuint program);

#define glReportError() glReportError1(__FILE__, __LINE__, __func__)
#define glReportFramebufferError() glReportFramebufferError1(__FILE__, __LINE__, __func__)
#define glReportValidateShaderError(PROG) glReportValidateShaderError1(__FILE__, __LINE__, __func__, (PROG))

bool isGLExtensionSupported(const char *name);

class ShaderProgramBase;

struct GLEnableScope {
    const GLenum val;
    GLEnableScope(GLenum v) : val(v) { glEnable(v); }
    ~GLEnableScope() { glDisable(val); }
};

struct GLDisableScope {
    const GLenum val;
    GLDisableScope(GLenum v) : val(v) { glDisable(v); }
    ~GLDisableScope() { glEnable(val); }
};

#if OPENGL_ES
struct GLDisableAlphaTest {};
#else
struct GLDisableAlphaTest : public GLDisableScope {
    GLDisableAlphaTest() : GLDisableScope(GL_ALPHA_TEST) {}
};
#endif


// RAII for opengl buffers
template <typename Type, GLenum GLType>
class GLBuffer {
    GLuint m_id;
    GLenum m_usage;
    uint   m_size;
    
public:

    GLBuffer() : m_id(0), m_usage(0), m_size(0) {}

    GLBuffer(const GLBuffer& tex) = delete; // not copyable

    GLBuffer(GLBuffer &&o) : m_id(o.m_id), m_usage(o.m_usage), m_size(o.m_size)
    {
        o.m_id    = 0;
        o.m_usage = 0;
        o.m_size  = 0;
    }

    ~GLBuffer() { clear(); }

    bool empty() const { return m_size == 0; }
    uint size() const  { return m_size; }
    uint getElementSize() const { return sizeof(Type); }
    GLenum getType() const { return GLType; }

    void clear()
    {
        if (m_id) {
            glDeleteBuffers(1, &m_id);
            m_id = 0;
            gpuMemoryUsed -= m_size * sizeof(Type);
        }
        m_size = 0;
    }

    void Bind() const
    {
        ASSERT(m_id);
        glBindBuffer(GLType, m_id);
        glReportError();
#if DEBUG || DEVELOP
        GLint rsize = 0;
        glGetBufferParameteriv(GLType, GL_BUFFER_SIZE, &rsize);
        ASSERT(rsize == m_size * sizeof(Type));
#endif
    }

    void Unbind() const
    {
        glBindBuffer(GLType, 0);
        glReportError();
    }

    void BufferData(vector<Type> data, uint mode)
    {
        BufferData(data.size(), &data[0], mode);
    }

    void BufferData(uint size, Type* data, uint mode)
    {
        if (m_id == 0) {
            glGenBuffers(1, &m_id);
        } else if (m_size == size && m_usage == mode) {
            BufferSubData(0, size, data);
            return;
        }
        Bind();
        glBufferData(GLType, size * sizeof(Type), data, mode);
        glReportError();
        Unbind();
        gpuMemoryUsed += (size - m_size) * sizeof(Type);
        m_size = size;
        m_usage = mode;
    }

    void BufferSubData(uint offset, uint size, Type* data)
    {
        ASSERT(m_id);
        ASSERT(offset + size <= m_size);
        Bind();
        glBufferSubData(GLType, offset * sizeof(Type), size * sizeof(Type), data);
        glReportError();
        Unbind();
    }
};

template <typename T>
struct VertexBuffer : public GLBuffer<T, GL_ARRAY_BUFFER> {
};

template <typename T>
struct IndexBuffer : public GLBuffer<T, GL_ELEMENT_ARRAY_BUFFER>
{
};

class GLTexture;

class PixImage {

    int2         m_size;
    vector<uint> m_data;

public:

    PixImage() {}
    PixImage(int2 size, uint def=0xffffffff) { resize(size, def); }
    
    void resize(int2 size, uint def=0xffffffff)
    {
        m_size = size;
        m_data.resize(size.x * size.y, def);
    }

    void setData(uint* data, int2 size)
    {
        resize(size);
        memcpy(&m_data[0], data, m_data.size() * sizeof(uint));
    }

    uint&       operator()(int x, int y)       { return m_data[y * m_size.x + x]; }
    const uint& operator()(int x, int y) const { return m_data[y * m_size.x + x]; }
    uint&       operator()(int2 p)             { return m_data[p.y * m_size.x + p.x]; }
    const uint& operator()(int2 p)       const { return m_data[p.y * m_size.x + p.x]; }
    uint&       operator[](uint i)             { return m_data[i]; }
    const uint& operator[](uint i)       const { return m_data[i]; }

    int2   size()        const { return m_size; }
    size_t pixel_count() const { return m_data.size(); }
    
    GLTexture uploadTexture() const;
};

struct ShaderState;

class GLTexture {

protected:
    float2 m_size;              // in pixels
    float2 m_texsize;
    GLuint m_texname = 0;
    GLint  m_format  = GL_RGB;

    void DrawFSBegin(ShaderState &ss) const;
    void DrawFSEnd() const;

public:

    GLTexture(const GLTexture& tex) = delete; // not copyable

    GLTexture(GLuint texn, float2 sz, GLint fmt) : m_texname(texn), m_size(sz), m_format(fmt) {}
    GLTexture() {}
    GLTexture(GLTexture&& o)
    {
        *this = std::move(o);
    }
    
    GLTexture& operator=(GLTexture&& o)
    {
        std::swap(m_size, o.m_size);
        std::swap(m_texsize, o.m_texsize);
        std::swap(m_texname, o.m_texname);
        std::swap(m_format, o.m_format);
        return *this;
    }
    
    ~GLTexture() { clear(); }

    void clear();
    
    void setFormat(GLint format) { m_format = format; }
    GLint getFormat() const { return m_format; }
    float2 size() const { return m_size; }
    bool  empty() const { return m_texname == 0; } 

    void loadFile(const char* fname);

    OutlawTexture getTexture() const;

    void BindTexture(int slot) const
    {
        ASSERT(m_texname);
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_texname);
        glReportError();
    }

    void UnbindTexture()
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void TexImage2D(int2 size, GLenum format, const uint *data);

    void SetTexWrap(bool enable);
    void SetTexMagFilter(GLint filter);
    void GenerateMipmap();

    template <typename Shader>
    void DrawFullscreen(uint color=0xffffffff) const { DrawFullscreen(Shader::instance(), color); }

    template <typename Shader>
    void DrawFullscreen(const Shader& shader, uint color=0xffffffff) const;
};

const GLTexture &getDitherTex();


// encapsulate the projection/modelview matrix and some related state
struct ShaderState {

    glm::mat4            uTransform;
    uint                 uColor = 0xffffffff;
    uint                 uColor1 = 0xffffffff;

    void translate(float2 t) { uTransform = glm::translate(uTransform, float3(t.x, t.y, 0)); }
    void translateZ(float z) { uTransform = glm::translate(uTransform, float3(0, 0, z)); }
    void translate(const float3 &t) { uTransform = glm::translate(uTransform, t); }
    void rotate(float a)     { uTransform = glm::rotate(uTransform, a, float3(0, 0, 1)); }
    void scale(const float3 &s) { uTransform = glm::scale(uTransform, s); }

    void translateRotate(float2 t, float a)
    {
        translate(t);
        rotate(a);
    }

    void color(uint c, float a=1)   { uColor = argb2abgr(0xff000000|c, a); }
    void color32(uint c, float a=1) { uColor = argb2abgr(c, a); }

    void color2(uint c, float ca, uint c1, float c1a)  
    { 
        uColor = argb2abgr(0xff000000|c, ca); 
        uColor1 = argb2abgr(0xff000000|c1, c1a); 
    }

    void DrawElements(uint dt, size_t ic, const ushort* i) const;
    void DrawElements(uint dt, size_t ic, const uint* i) const;

    template <typename T>
    void DrawElements(uint dt, const IndexBuffer<T>& indices) const
    {
        indices.Bind();
        DrawElements(dt, indices.size(), (T*)0);
        indices.Unbind();
    }

    void DrawArrays(uint dt, size_t count) const;
};

template <typename Shader>
void GLTexture::DrawFullscreen(const Shader& shader, uint color) const
{
    ShaderState ss;
    ss.uColor = color;
    DrawFSBegin(ss);
    shader.DrawRectCorners(ss, getTexture(), float2(0.f), float2(1.f));
    DrawFSEnd();
}

// RAII for a render target
class GLRenderTexture : public GLTexture {

public:
    enum ZFlags { HASZ=1, KEEPZ=3, NOZ=4 };

private:
    GLuint m_fbname  = 0;
    GLuint m_zrbname = 0;
    uint   m_zflags  = 0;
    
    static vector<GLRenderTexture*> s_bound;
    
    void Generate(ZFlags zflags);
    
public:
    
    ~GLRenderTexture() { clear(); }
    
    void clear();
    
    bool empty() const { return !(m_fbname && m_texname && m_zrbname); }
    
    void BindFramebuffer(float2 sizePixels, ZFlags zflags=HASZ);
    void RebindFramebuffer();
    void UnbindFramebuffer() const;

    // return bound texture, with idx 0 being the currently bound
    static GLRenderTexture *getBound(int idx);
};

// camera/view data
struct View {
    float2 sizePixels;
    float2 sizePoints;
    float2 position;            // in world coordinates
    float2 velocity;            // change in position
    float  scale = 1.f;               // larger values are more zoomed out
    float  z = 0.f;                   // depth of camera (related to scale)
    float  angle = 0.f;               // rotation is applied after position
    float  zfar = 3500.f;
    
    // interpolation support
    friend View operator+(const View& a, const View& b);
    friend View operator*(float a, const View& b);

    float getScale() const;
    float2 toWorld(float2 p) const;
    float2 toScreen(float2 p) const;

    float getPointSize() const { return (sizePixels.y / sizePoints.y); }

    float2 toScreenPixels(float2 p) const { return toScreen(p) * (sizePixels / sizePoints); }

    float  toScreenSize(float  p) const { return p / getScale(); }
    float2 toScreenSize(float2 p) const { return p / getScale(); }

    float  toScreenSizePixels(float  p) const { return toScreenSize(p) * getPointSize(); }
    float2 toScreenSizePixels(float2 p) const { return toScreenSize(p) * getPointSize(); }
    
    float2 getAspect() const { return float2(sizePoints.x / sizePoints.y, 1.f); }

    float  toWorldSize(float  p) const { return p * getScale(); }
    float2 toWorldSize(float2 p) const { return p * getScale(); }

    // get size of screen in world coordinates
    float2 getWorldSize(float z2=0.2) const
    {
        float2 zPlaneSize = (scale * sizePoints) - 2.f * (z+z2) * getAspect();
        return max(zPlaneSize, float2(0));
    }

    // intersectX functions are in world coordinates
    bool intersectSegment(float2 a, float2 b, float width=0) const
    {
        return intersectRectangleSegment(position, 0.5f * scale * sizePoints + width, a, b);
    }

    bool intersectPoint(const float2 &a) const
    {
        return intersectPointRectangle(a, position, 0.5f * scale * sizePoints);
    }

    bool intersectCircle(const float2 &a, float r) const
    {
        return intersectCircleRectangle(a, r, position, 0.5f * scale * sizePoints);
    }

    bool intersectCircle(const float3 &a, float r) const
    {
        float2 zPlaneSize = (0.5f * scale * sizePoints) - getAspect() * a.z;
        return intersectCircleRectangle(float2(a.x, a.y), r, position, zPlaneSize);
    }

    float getScreenPointSizeInPixels() const { return sizePixels.x / ((sizePoints.x) - z); }
    float getWorldPointSizeInPixels() const  { return sizePixels.x / ((scale * sizePoints.x) - z); }

    uint getCircleVerts(float worldRadius, int mx=24) const;

    ShaderState getWorldShaderState() const;
    ShaderState getScreenShaderState() const;

    float3 getScreenCameraPos(float3 offset) const { return float3(0.f, 0.f, 0.5f * sizePoints.y); }

    bool intersectRectangle(const float3 &a, const float2 &r) const;

    // intersectScreenX functions are in screen coordinages
    bool intersectScreenCircle(float2 a, float r) const
    {
        return intersectCircleRectangle(a, r, 0.5f * sizePoints, 0.5f * sizePoints);
    }

    void setScreenLineWidth(float scl=1.f) const;
    void setWorldLineWidth() const;

    void setWorldRadius(float rad)
    {
        scale = max_dim((2.f * rad) / sizePoints);
    }

    float getWorldRadius() const
    {
        return max_dim(getWorldSize()) / 2.f;
    }

    float worldRadiusToScale(float rad) const
    {
        return max_dim((2.f * rad) / sizePoints);
    }

    float worldRadiusToScale(float2 rad) const
    {
        return max_dim((2.f * rad) / sizePoints);
    }
};

#define GET_ATTR_LOC(NAME) NAME = getAttribLocation(#NAME)
#define GET_UNIF_LOC(NAME) NAME = getUniformLocation(#NAME)

// encapsulates a GLSL shader, c++ style attribute passing, etc.
class ShaderProgramBase {
    
private:    
    GLuint m_programHandle    = 0;
    GLint  m_transformUniform = -1;
    GLint  m_positionSlot     = -1;
    string m_name;
    
    mutable vector<GLuint> m_enabledAttribs;

    static void vap1(uint slot, uint size, const float* ptr)  { glVertexAttribPointer(slot, 1, GL_FLOAT, GL_FALSE, size, ptr); }
    static void vap1(uint slot, uint size, const float2* ptr) { glVertexAttribPointer(slot, 2, GL_FLOAT, GL_FALSE, size, ptr); }
    static void vap1(uint slot, uint size, const float3* ptr) { glVertexAttribPointer(slot, 3, GL_FLOAT, GL_FALSE, size, ptr); }
    static void vap1(uint slot, uint size, const float4* ptr) { glVertexAttribPointer(slot, 4, GL_FLOAT, GL_FALSE, size, ptr); }
    static void vap1(uint slot, uint size, const uint* ptr)   { glVertexAttribPointer(slot, 4, GL_UNSIGNED_BYTE, GL_TRUE, size, ptr); }

protected:

    string m_header;
    string m_argstr;

    ShaderProgramBase();
    virtual ~ShaderProgramBase() { reset(); }
    virtual void LoadTheProgram() = 0;

    void reset();
    
    GLuint getProgram() const { return m_programHandle; }
    
    GLuint createShader(const char* txt, GLenum type) const;
    
    bool LoadProgram(const char* name, const char* shared, const char *vert, const char *frag);

    // load based on file
    bool LoadProgram(const char* name);

    GLint getAttribLocation(const char *name) const;
    GLint getUniformLocation(const char *name) const;
    
    template <typename V, typename T>
    void vertexAttribPointer(GLuint slot, const V *ptr, const T* base) const
    {
        if (slot == -1)
            return;
        glEnableVertexAttribArray(slot);
        glReportError();
        m_enabledAttribs.push_back(slot);
        vap1(slot, sizeof(T), (const V*) ((const char*) ptr - (const char*) base));
        glReportError();
    }


    template <typename V, typename T>
    void UseProgramBase(const ShaderState& ss, const V* ptr, const T* base) const
    {
        UseProgramBase(ss, sizeof(T), (const V*) ((const char*) ptr - (const char*) base));
    }

    void UseProgramBase(const ShaderState& ss, uint size, const float3* pos) const;
    void UseProgramBase(const ShaderState& ss, uint size, const float2* pos) const;
    void UseProgramBase(const ShaderState& ss) const;

public:

    bool ReloadProgram() { LoadTheProgram(); return isLoaded(); }
    void UnuseProgram() const;

    bool isLoaded() const { return m_programHandle != 0; }
    string getName() const { return m_name; }
};

template <typename T>
struct ShaderBase {

    string m_typeName;

    const char* getTypeName() const
    {
        m_typeName = TYPE_NAME(T);
        return m_typeName.c_str();
    }

    static const T& instance()
    {
        static T* p = new T();
        if (!p->isLoaded()) {
            p->LoadTheProgram();
            glReportError();
        }
        return *p;
    }
    
};

struct Transform2D {

    glm::mat3 transform;

    Transform2D &translateRotate(float2 t, float a) { return translateRotate(t, angleToVector(a)); }

    Transform2D &translateRotate(float2 t, float2 rot)
    {
        glm::mat3 m(rot.x, rot.y, 0,
                    -rot.y, rot.x, 0,
                    t.x, t.y, 1);
        transform *=  m;
        return *this;
    }
    
    Transform2D &translate(float2 t)
    {
        glm::mat3 m(1, 0, 0,
                    0, 1, 0,
                    t.x , t.y, 1);
        transform *= m;
        return *this;
    }

    Transform2D &rotate(float a) { return rotate(angleToVector(a)); }
    
    Transform2D &rotate(float2 rot)
    {
        glm::mat3 m(rot.x, rot.y, 0,
                    -rot.y, rot.x, 0,
                    0, 0, 1);
        transform *=  m;
        return *this;
    }

    Transform2D &scale(float2 s)
    {
        glm::mat3 m(s.x, 0, 0,
                    0, s.y, 0,
                    0, 0,   1);
        transform *= m;
        return *this;
    }
    
    template <typename R>
    void apply(R &result, const cpVect &vec) const
    {
        result.x += transform[0].x * vec.x + transform[1].x * vec.y + transform[2].x;
        result.y += transform[0].y * vec.x + transform[1].y * vec.y + transform[2].y;
    }

    template <typename R, typename T>
    void apply(R &result, const glm::detail::tvec2<T, glm::defaultp> &vec) const
    {
        result.x += transform[0].x * vec.x + transform[1].x * vec.y + transform[2].x;
        result.y += transform[0].y * vec.x + transform[1].y * vec.y + transform[2].y;
    }

    template <typename R, typename T>
    void apply(R& result, const glm::detail::tvec3<T, glm::defaultp> &vec) const
    {
        result.x += transform[0].x * vec.x + transform[1].x * vec.y + transform[2].x;
        result.y += transform[0].y * vec.x + transform[1].y * vec.y + transform[2].y;
        result.z += vec.z;
    }

    float2 apply(const float2 &vec) const
    {
        float2 result;
        apply(result, vec);
        return result;
    }

    float3 apply(const float3 &vec) const
    {
        float3 result;
        apply(result, vec);
        return result;
    }

    template <typename T>
    void apply(T* arr, int count) const
    {
        for (int i=0; i<count; i++)
        {
            T res;
            apply(res, arr[i]);
            arr[i] = res;
        }
    }

};

template <uint Verts>
const float2 &getCircleVertOffset(uint idx)
{
    static float2 offsets[Verts];
    static bool initialized = false;
    if (!initialized)
    {
        for (uint i=0; i<Verts; i++)
        {
            const float angle = (float) i * (M_TAOf / (float) Verts);
            offsets[i] = angleToVector(angle);
        }
        initialized = true;
    }
    return offsets[idx];
}

inline float2 getCircleVertOffset(uint idx, uint verts)
{
    const float angle = (float) idx * (M_TAOf / (float) verts);
    return angleToVector(angle);
}


struct MeshBase : public Transform2D {

    typedef uint IndexType;

};

// aka VertexPusher
// store a bunch of geometry
// lots of routines for drawing shapes
// tracks the current vertex and transform for stateful OpenGL style drawing
template <typename Vtx>
struct Mesh : public MeshBase {

protected:

    typedef std::vector<IndexType> IndexVector;

    Vtx                    m_curVert;
    VertexBuffer<Vtx>      m_vbo;
    IndexBuffer<IndexType> m_ibo;
    std::vector<Vtx>       m_vl;
    IndexVector            m_il;

public:

    size_t getSizeof() const
    {
        return sizeof(Vtx) * m_vl.size() + 
            sizeof(IndexType) * m_il.size() +
            sizeof(*this);
    }

    struct scope {
        
        Mesh &p;
        Vtx    curVert;
        mat3   transform;

        scope(Mesh &p_) : p(p_), curVert(p_.m_curVert), transform(p_.transform) {}
        ~scope()
        {
            p.m_curVert = curVert;
            p.transform = transform;
        }
    };

    Mesh()
    {
        clear();
    }

    void clear()
    {
        // do not clear vbo / ibo!
        m_vl.clear();
        m_il.clear();
        transform = glm::mat3();
        m_curVert.pos.z = 0.f;
        //m_curVert.color = 0xffffffff;
    }

    uint indexCount() const { return m_il.size(); }
    bool empty() const { return m_vl.empty(); }

    Vtx& cur() { return m_curVert; }
    void color(uint c, float a=1)   { m_curVert.color = argb2abgr(c|0xff000000, a); }
    void color32(uint c, float a=1) { m_curVert.color = argb2abgr(c, a); }

    uint getColor() const { return m_curVert.color; }

    void translateZ(float z)
    {
        m_curVert.pos.z += z;
    }

    void PushI(IndexType start, const IndexType *pidx, uint ic)
    {
        ASSERT(start + ic < std::numeric_limits<IndexType>::max());
        for (uint i=0; i<ic; i++)
        {
            DASSERT(start + pidx[i] < m_vl.size());
            m_il.push_back(start + pidx[i]);
        }
    }

    template <typename Vec>
    IndexType Push(const Vec *pv, uint vc, const IndexType *pidx, uint ic)
    {
        IndexType start = PushV(pv, vc);
        PushI(start, pidx, ic);
        return start;
    }

    static bool checkOverflow(size_t val, ushort t)
    {
        if (val > std::numeric_limits<IndexType>::max()) {
            ASSERT(false && "vertex overflow!");
            return true;
        }
        return false;
    }

    static bool checkOverflow(size_t val, uint t)
    {
        return false;           // never happens for ints
    }

    Vtx& getVertex(uint idx) { return m_vl[idx]; }
    uint getVertexCount() const { return m_vl.size(); }

    template <typename T>
    IndexType PushV(const T *pv, size_t vc)
    {
        const IndexType start = (IndexType) m_vl.size();
        if (checkOverflow(m_vl.size() + vc, (IndexType) 0))
            return start;

        Vtx v = m_curVert;
        for (uint i=0; i<vc; i++)
        {
            v.pos = m_curVert.pos;
            apply(v.pos, pv[i]);
            m_vl.push_back(v);
        }
        return start;
    }

    IndexType PushV(const Vtx *pv, size_t vc)
    {
        const IndexType start = (IndexType) m_vl.size();
        if (checkOverflow(m_vl.size() + vc, (IndexType) 0))
            return start;

        Vtx v;
        for (uint i=0; i<vc; i++)
        {
            v = pv[i];
            v.pos = m_curVert.pos;
            apply(v.pos, pv[i].pos);
            m_vl.push_back(v);
        }
        return start;
    }

    // pre-transformed vertices
    template <typename T>
    IndexType PushVTrans(const T *pv, size_t vc)
    {
        const IndexType start = (IndexType) m_vl.size();
        if (checkOverflow(m_vl.size() + vc, (IndexType) 0))
            return start;

        Vtx v = m_curVert;
        for (uint i=0; i<vc; i++)
        {
            v.pos = float3(pv[i]);
            m_vl.push_back(v);
        }
        return start;
    }

    IndexType Push(const Mesh<Vtx> &pusher)
    {
        const IndexType start = m_vl.size();
        if (checkOverflow(start + pusher.m_vl.size(), (IndexType)0))
            return start;

        m_vl.reserve(m_vl.size() + pusher.m_vl.size());
        m_il.reserve(m_il.size() + pusher.m_il.size());
        
        Vtx v;
        foreach (const Vtx& vtx, pusher.m_vl)
        {
            v = vtx;
            v.pos = m_curVert.pos;
            apply(v.pos, vtx.pos);
            m_vl.push_back(v);
        }

        foreach (IndexType idx, pusher.m_il)
        {
            m_il.push_back(start + idx);
        }
        return start;
    }

    IndexType PushArrayIndexes(IndexType start, size_t vc)
    {
        for (uint i=0; i<vc; i++) {
            m_il.push_back(start + i);
        }
        return start;
    }

    template <typename Vec>
    IndexType PushArray(const Vec *pv, size_t vc)
    {
        IndexType start = PushV(pv, vc);
        return PushArrayIndexes(start, vc);
    }

    void UpdateBuffers(bool use)
    {
        if (use && m_vl.size()) {
            m_vbo.BufferData(m_vl, GL_STATIC_DRAW);
            m_ibo.BufferData(m_il, GL_STATIC_DRAW);
        } else {
            m_vbo.clear();
            m_ibo.clear();
        }
    }

    bool BuffersEmpty() const { return m_vbo.empty(); }

    template <typename Prog>
    void Draw(const ShaderState &s, uint type, const Prog &program) const
    {
        if (!program.isLoaded())
            return;

        if (!m_vbo.empty())
        {
            ASSERT(m_ibo.size());
            m_vbo.Bind();
            program.UseProgram(s, &m_vl[0], &m_vl[0]);
            s.DrawElements(type, m_ibo);
            program.UnuseProgram();
            m_vbo.Unbind();
        }
        else if (!m_vl.empty())
        {
            ASSERT(m_il.size() > 1);
            
            program.UseProgram(s, &m_vl[0], (Vtx*)NULL);
            s.DrawElements(type, m_il.size(), &m_il[0]);
            program.UnuseProgram();
        }
    }
};

template <typename Vtx1, uint PrimSize>
struct PrimMesh : public Mesh<Vtx1> {

    struct IndxPrim {
        typename Mesh<Vtx1>::IndexType indxs[PrimSize];
        friend bool operator==(const IndxPrim &a, const IndxPrim &b) {
            return !memcmp(a.indxs, b.indxs, sizeof(a.indxs));
        }
    };

    IndxPrim* primBegin() { return (IndxPrim*) &this->m_il[0]; }
    IndxPrim* primEnd()   { return (IndxPrim*) (&this->m_il[0] + this->m_il.size()); }
    
    void primErase(IndxPrim* beg, IndxPrim* end) 
    {
        typename Mesh<Vtx1>::IndexVector::iterator ilbeg = this->m_il.begin() + (beg - primBegin()) * PrimSize;
        typename Mesh<Vtx1>::IndexVector::iterator ilend = this->m_il.begin() + (end - primBegin()) * PrimSize;
        this->m_il.erase(ilbeg, ilend);
    }

    uint primSize() const { return this->m_il.size() / PrimSize; }

    static bool SortByFirstIndex(const IndxPrim& a, const IndxPrim& b) {
        return a.indxs[0] < b.indxs[0];
    };

    void SortByZ()
    {
        if (this->m_il.empty())
            return;
        // just re-arrange the indices
        // the other option is to leave the indices and rearange vertices
        std::sort(primBegin(), primEnd(), lambda((const IndxPrim&a, const IndxPrim &b),
                                                 this->m_vl[a.indxs[0]].pos.z <
                                                 this->m_vl[b.indxs[0]].pos.z ));
    }

#define DBG_OPT 0
#if DBG_OPT
#define OPT_DBG(X) X
#else
#define OPT_DBG(X)
#endif

    // remove redundant vertices and then redundant primitives
    void Optimize()
    {
        if (!this->m_il.size())
            return;

        static const float kUnifyDist = 0.1f;
        std::set<uint> replacedIndices;
        int maxIndex = 0;
        spatial_hash<int> verthash(10.f, this->m_vl.size() * 5);
        for (int i=0; i<this->m_il.size(); i++)
        {
            const typename Mesh<Vtx1>::IndexType index = this->m_il[i];
            const float3 vert = this->m_vl[index].pos;
            const float2 vert2(vert.x, vert.y);
            const uint   col  = this->m_vl[index].color;

            bool replaced = false;
            vector<int> indices;
            verthash.intersectCircle(&indices, vert2, kUnifyDist);
            foreach (int idx, indices)
            {
                if (idx != index && 
                    this->m_vl[idx].color == col && 
                    fabsf(this->m_vl[idx].pos.z - vert.z) < kUnifyDist)
                {
                    OPT_DBG(replacedIndices.insert(this->m_il[i]));
                    replaced = true;
                    this->m_il[i] = idx; 
                    break;
                }
            }

            if (!replaced) {
                verthash.insertPoint(vert2, index);
                maxIndex = max(maxIndex, (int) index);
            }
        }
        

        // 1. sort indices within each primitive
        // 2. sort primitives by first index
        // 3. remove redundant primitives
        for (IndxPrim *it=primBegin(), *end=primEnd(); it != end; it++) {
            std::sort(it->indxs, it->indxs + PrimSize);
        }
        std::sort(primBegin(), primEnd(), SortByFirstIndex);
        IndxPrim* newEnd = std::unique(primBegin(), primEnd());

        OPT_DBG(ReportMessagef("optimized out %d(%d removed)/%d verts, and %d/%d prims(%d)",
                               replacedIndices.size(), this->m_vl.size() - maxIndex, this->m_vl.size(), 
                               (primEnd() - newEnd), primSize(), PrimSize));

        primErase(newEnd, primEnd());
        this->m_vl.resize(maxIndex + 1);

        // TODO compact vertices
    }
};

enum SpiralType {
    SPRIAL_ARCHIMEDEAN,
    SPIRAL_FERMAT,
    SPRIAL_HYPERBOLIC,
    SPIRAL_LOG
};



template <typename Vtx>
struct PointMesh : public Mesh<Vtx> {

    typedef typename Mesh<Vtx>::IndexType Index;

    void PushPoint(float2 pos)
    {
        this->PushV(&pos, 1);
    }

    void PushPoint(const Vtx &pos)
    {
        this->PushV(&pos, 1);
    }

    template <typename Prog>
    void Draw(const ShaderState &s, const Prog &program) const
    {
        if (!program.isLoaded())
            return;

        if (!this->m_vbo.empty())
        {
            this->m_vbo.Bind();
            program.UseProgram(s, &this->m_vl[0], &this->m_vl[0]);
            s.DrawArrays(GL_POINTS, this->m_vl.size());
            program.UnuseProgram();
            this->m_vbo.Unbind();
        }
        else if (!this->m_vl.empty())
        {
            program.UseProgram(s, &this->m_vl[0], (Vtx*)NULL);
            s.DrawArrays(GL_POINTS, this->m_vl.size());
            program.UnuseProgram();
        }
    }
};
    
template <typename Vtx>
struct LineMesh : public PrimMesh<Vtx, 2> {

    typedef typename Mesh<Vtx>::IndexType Index;

    template <typename Vec>
    Index PushLoop(const Vec *pv, uint c)
    {
        Index start = this->PushV(pv, c);
        for (uint i=0; i<c; i++)
        {
            this->m_il.push_back(start + i);
            this->m_il.push_back(i == (c-1) ? start : (start + i + 1));
        }
        return start;
    }

    void PushStrip(const float2 *pv, uint c)
    {
        Index start = this->PushV(pv, c);
        for (uint i=1; i<c; i++)
        {
            this->m_il.push_back(start + i - 1);
            this->m_il.push_back(start + i);
        }
    }

    void PushLoopIndexes(Index start, uint vc, uint ls)
    {
        ASSERT(vc % ls == 0);
        for (int i=0; i<vc; i++) {
            this->m_il.push_back(start + i);
            this->m_il.push_back(start + i + (((i+1) % ls == 0) ? 1 - ls : 1));
        }
    }

    void PushLoopIndexes(Index start, const Index *il, uint ic, uint ls)
    {
        ASSERT(ic % ls == 0);
        for (int i=1; i<=ic; i++) {
            this->m_il.push_back(start + il[(i % ls == 0) ? i - ls : i]);
            this->m_il.push_back(start + il[i-1]);
        }
    }

    template <typename Vec>
    void PushLoops(const Vec *pv, uint vc, const Index *il, uint ic, uint ls)
    {
        const Index start = this->PushV(pv, vc);
        PushLoopIndexes(start, il, ic, ls);
    }


    template <typename Vec>
    void PushLines(const Vec *pv, uint c)
    {
        const Index start = this->PushV(pv, c);
        for (uint i=1; i<c; i+=2)
        {
            this->m_il.push_back(start + i - 1);
            this->m_il.push_back(start + i);
        }
    }

    #include "Polyhedra.h"

    // first and last points are control points/tangents
    void PushCardinalSpline(const float2 *pv, uint count, uint icount, float c=1.f)
    {
        if (count < 4)
            return;
        // FIXME transform control points, then expand...
        vector<float2> ipv(icount);
        const float interval = (float) (count-3) / (float) (icount-1);
        ASSERTF(fabsf((1 + (icount-1) * interval) - (count-2)) < 0.001,
                "(%d * %g == %g) != %d", (icount-1), interval, (icount-1) * interval, count-3);
        for (uint i=0; i<icount; i++) {
            ipv[i] = cardinal(pv, count, 1 + interval * i, c);
        }
        this->PushStrip(&ipv[0], icount);
    }

    void PushSpiral(const float2 &pos, SpiralType type, float maxTheta, float a, float b,
                    int numVerts=32, float startAngle=0.f)
    {
        static const int maxVerts = 64;
        ASSERT(numVerts >= 3);
        numVerts = min(maxVerts, numVerts);
        float2 verts[maxVerts];

        const float angleIncr = maxTheta / (float) numVerts;
    
        for (int i=0; i != numVerts; ++i)
        {
            const float theta = startAngle + i * angleIncr;
            float r = 0.f;
            switch (type) {
            case SPRIAL_ARCHIMEDEAN: r = b + a * theta; break;
            case SPIRAL_FERMAT:      r = a * sqrt(theta); break;
            case SPRIAL_HYPERBOLIC:  r = a / theta; break;
            case SPIRAL_LOG:         r = a * exp(b * theta); break;
            }

            verts[i] = pos + r * angleToVector(theta);
        }

        PushStrip(verts, numVerts);
    }

    // widthRadians is on either side of angleStart
    void PushArc(float2 pos, float radius, float angleStart, float widthRadians, int numVerts=32)
    {
        ASSERT(numVerts >= 3);

        Index start = this->m_vl.size();

        for (int i=0; i < numVerts; ++i)
        {
            const float angle = angleStart + 2.f * widthRadians * (-0.5f + (float) i / (float) (numVerts-1));
            const uint  j     = (uint) this->m_vl.size();
            float2      p     = pos + radius * angleToVector(angle);

            this->PushV(&p, 1);
            if (j > start) {
                this->m_il.push_back(j-1);
                this->m_il.push_back(j);
            }
        }
    }

    void PushCircle(float radius, int numVerts=32)
    {
        this->PushCircle(float2(0), radius, numVerts);
    }

    void PushCircle(const float2 &pos, float radius, int numVerts=32, float startAngle=0.f)
    {
        static const int maxVerts = 64;
        ASSERT(numVerts >= 3);
        numVerts = min(maxVerts, numVerts);
        float2 verts[maxVerts];

        const float angleIncr = 2.f * M_PIf / (float) numVerts;
    
        for (int i=0; i != numVerts; ++i)
        {
            const float angle = startAngle + i * angleIncr;
            verts[i].x = pos.x + radius * cos(angle);
            verts[i].y = pos.y + radius * sin(angle);
        }

        PushLoop(verts, numVerts);
    }

    void PushLineCylinder(const float2 &pos, float2 radius, int numVerts=32, float startAngle=0.f)
    {
        static const int maxVerts = 64;
        ASSERT(numVerts >= 3);
        numVerts = min(maxVerts, numVerts);
        float3 verts[maxVerts];

        const float angleIncr = 2.f * M_PIf / (float) numVerts;

        for (uint j=0; j<2; j++) {
            for (int i=0; i != numVerts; ++i)
            {
                const float angle = startAngle + i * angleIncr;
                verts[i].x = pos.x + radius.x * cos(angle);
                verts[i].y = pos.y + radius.x * sin(angle);
                verts[i].z = (j == 0) ? radius.y : -radius.y;
                if (j == 0) {
                    float3 v2 = verts[i];
                    v2.z = -v2.z;
                    PushLine(verts[i], v2);
                }
            }
            PushLoop(verts, numVerts);
        }
    }

    void PushDashedLineCircle(const float2 &pos, float radius, int numVerts, float startAngle, int dashOn, int dashOff)
    {
        ASSERT(numVerts >= 3);

        const float angleIncr = 2.f * M_PIf / (float) numVerts;
    
        float2 firstVert;
        float2 prevVert;

        int  dashIdx = 0;
        bool on      = true;

        for (int i=0; i != numVerts; ++i)
        {
            const float angle = startAngle + i * angleIncr;
            float2 vert(pos.x + radius * cos(angle),
                        pos.y + radius * sin(angle));
            
            float2 overt;
            if (i > 0) {
                overt = prevVert;
            } else {
                firstVert = vert;
            }
                
            if (i == numVerts-1) {
                overt = firstVert;
            } else {
                prevVert = vert;
            }

            if (on && i > 0)
                this->PushLine(overt, vert);

            if (on && dashIdx > dashOn) {
                on      = false;
                dashIdx = 0;
            }
            else if (!on && dashIdx > dashOff) {
                on      = true;
                dashIdx = 0;
            }
            else {
                dashIdx++;
            }
        }
    }

    void PushBox(float2 pos, float3 rad)
    {
        float3 pos3(pos, 0.f);
        const float3 verts[] = { pos3 + float3(-rad.x, rad.y, rad.z), 
                                 pos3 + float3(rad.x, rad.y, rad.z), 
                                 pos3 + float3(rad.x, -rad.y, rad.z),
                                 pos3 + float3(-rad.x, -rad.y, rad.z),
                                 pos3 + float3(-rad.x, rad.y, -rad.z), 
                                 pos3 + float3(rad.x, rad.y, -rad.z), 
                                 pos3 + float3(rad.x, -rad.y, -rad.z),
                                 pos3 + float3(-rad.x, -rad.y, -rad.z) };
        const Index idxs[] = { 0,1, 1,2, 2,3, 3,0,
                                                       0,4, 1,5, 2,6, 3,7,
                                                       4,5, 5,6, 6,7, 7,4 };
        this->Push(verts, arraySize(verts), idxs, arraySize(idxs));
    }

    void PushTri(float2 a, float2 b, float2 c)
    {
        const float2 v[] = { a, b, c };
        PushLoop(v, arraySize(v));
    }

    void PushPointTri(float2 pos, float angle, float offset, float2 sz)
    {
        glm::mat3 trans = this->transform;
        this->translateRotate(pos, angle);
        this->translate(float2(offset, 0.f));
        PushTri(float2(sz.x, 0.f), flipX(sz), -sz);
        this->transform = trans;
    }

    void PushLineQuad(float2 a, float2 b, float2 c, float2 d)
    {
        const float2 v[] = { a, b, d, c };
        PushLoop(v, arraySize(v));
    }

    Index PushRect(float2 r)
    {
        const float2 verts[4] = { -r, float2(-r.x, r.y), r, float2(r.x, -r.y) };
        return PushLoop(verts, arraySize(verts));
    }

    Index PushRect(float2 p, float2 r)
    {
        const float2 verts[4] = { p-r, p + float2(-r.x, r.y), p + r, p + float2(r.x, -r.y) };
        return PushLoop(verts, arraySize(verts));
    }

    void PushRectCorners(float2 a, float2 b)
    {
        float2 ll(min(a.x, b.x), min(a.y, b.y));
        float2 ur(max(a.x, b.x), max(a.y, b.y));
        const float2 verts[4] = { ll, float2(ll.x, ur.y), ur, float2(ur.x, ll.y) };
        PushLoop(verts, arraySize(verts));
    }

    template <typename Vec>
    void PushLine(const Vec &a, const Vec &b)
    {
        const Vec x[] = {a, b};
        const Index i[] = {0, 1};
        this->Push(x, 2, i, 2);
    }
    
    void PushLineTri(float2 a, float2 b, float2 c)
    {
        const float2 x[] = {a, b, c};
        PushLoop(x, 3);
    }

    void PushLinePrism(float2 a, float2 b, float2 c, float d)
    {
        const float3 x[] = { float3(a, d), float3(b, d), float3(c, d),
                             float3(a, -d), float3(b, -d), float3(c, -d) };
        const Index il0[] = { 0,1,2, 3,4,5 };
        const Index il1[] = { 0,3, 1,4, 2,5 };
        const Index start = this->PushV(x, arraySize(x));
        PushLoopIndexes(start, il0, 6, 3);
        PushLoopIndexes(start, il1, 6, 2);
    }
    
    template <typename Prog>
    void Draw(const ShaderState &s, const Prog& prog) const
    {
        Mesh<Vtx>::Draw(s, GL_LINES, prog);
    }

};

template <typename Vtx>
struct TriMesh : public PrimMesh<Vtx, 3> {

    typedef typename Mesh<Vtx>::IndexType Index;

    void PushPoly(const float2* verts, uint vc)
    {
        ASSERT(vc > 2);
        const uint start = this->PushV(verts, vc);
        for (uint i=2; i<vc; i++) {
            this->m_il.push_back(start);
            this->m_il.push_back(start + i - 1);
            this->m_il.push_back(start + i);
        }
    }

    // a b
    // c d
    Index PushQuad(float2 a, float2 b, float2 c, float2 d)
    {
        const float2 v[] = { a, b, c, d };
        return this->PushQuadV(v);
    }

    template <typename Vec>
    Index PushQuadV(const Vec *v)
    {
        static const Index i[] = {0, 1, 2, 1, 3, 2};
        return this->Push(v, 4, i, arraySize(i));
    }

    Index PushRect(float2 p, float2 r)
    {
        return PushQuad(p + float2(-r.x, r.y), p + r, 
                        p-r, p + float2(r.x, -r.y));
    }

    Index PushRect(float3 p, float2 r)
    {
        const float3 v[] = { p + float3(-r.x,  r.y, 0),
                             p + float3( r.x,  r.y, 0),
                             p + float3(-r.x, -r.y, 0),
                             p + float3( r.x, -r.y, 0) };
        return PushQuadV(v);
    }

    void PushRectCorners(float2 a, float2 b)
    {
        float2 ll(min(a.x, b.x), min(a.y, b.y));
        float2 ur(max(a.x, b.x), max(a.y, b.y));
        PushQuad(float2(ll.x, ur.y), ur,
                 ll, float2(ur.x, ll.y));
    }

    void PushTri(float2 a, float2 b, float2 c)
    {
        float2 x[] = {a, b, c};
        Index i[] = {0, 1, 2};
        this->Push(x, 3, i, 3);
    }

    Index PushCircle(float radius, int numVerts=32) 
    {
        return PushCircle(float2(0), radius, numVerts); 
    }

    Index PushCircle(float2 pos, float radius, const View& view) 
    {
        return PushCircle(pos, radius, view.getCircleVerts(radius));
    }

    Index PushCircle(float2 pos, float radius, int numVerts=32)
    {
        ASSERT(numVerts >= 3);

        Index start = this->m_vl.size();

        for (int i=0; i < numVerts; ++i)
        {
            const float angle = (float) i * (M_TAOf / (float) numVerts);
            const uint  j     = (uint) this->m_vl.size();
            float2      p     = pos + radius * angleToVector(angle);

            this->PushV(&p, 1);

            if (j - start > 1)
            {
                this->m_il.push_back(start);
                this->m_il.push_back(j-1);
                this->m_il.push_back(j);
            }
        }
        return start;
    }

    // widthRadians is on either side of angleStart
    void PushSector(float2 pos, float radius, float angleStart, float widthRadians, int numVerts=32)
    {
        ASSERT(numVerts >= 3);

        Index start = this->m_vl.size();
        this->PushV(&pos, 1);

        for (int i=0; i < numVerts; ++i)
        {
            const float angle = angleStart + 2.f * widthRadians * (-0.5f + (float) i / (float) (numVerts-1));
            const uint  j     = (uint) this->m_vl.size();
            float2      p     = pos + radius * angleToVector(angle);

            this->PushV(&p, 1);

            if (j - start > 0)
            {
                this->m_il.push_back(start);
                this->m_il.push_back(j-1);
                this->m_il.push_back(j);
            }
        }
    }

    Index PushCircleCenterVert(float2 pos, float radius, int numVerts=32)
    {
        ASSERT(numVerts >= 3);

        Index start = this->m_vl.size();
        this->PushV(&pos, 1);

        for (int i=0; i < numVerts; ++i)
        {
            const float  angle = M_TAOf * (float) i / (float) numVerts;
            const uint   j     = (uint) this->m_vl.size();
            const float2 p     = pos + radius * angleToVector(angle);

            this->PushV(&p, 1);

            if (j - start > 0)
            {
                this->m_il.push_back(start);
                this->m_il.push_back(j-1);
                this->m_il.push_back(j);
            }
        }

        this->m_il.push_back(start);
        this->m_il.push_back(start + numVerts);
        this->m_il.push_back(start + 1);
        return start;
    }
    
    Index PushCircleCenterVert(float radius, int numVerts=32)
    {
        return PushCircleCenterVert(float2(0.f), radius, numVerts);
    }

    Index PushPolyCenterVert(float2 pos, float scale, float2* verts, int numVerts)
    {
        ASSERT(numVerts >= 3);

        Index start = this->m_vl.size();
        this->PushV(&pos, 1);

        for (int i=0; i < numVerts; ++i)
        {
            const uint   j = (uint) this->m_vl.size();
            const float2 p = pos + scale * verts[i];

            this->PushV(&p, 1);

            if (j - start > 0)
            {
                this->m_il.push_back(start);
                this->m_il.push_back(j-1);
                this->m_il.push_back(j);
            }
        }

        this->m_il.push_back(start);
        this->m_il.push_back(start + numVerts);
        this->m_il.push_back(start + 1);
        return start;
    }

    template <typename Prog>
    void Draw(const ShaderState &s, const Prog& prog) const
    {
        Mesh<Vtx>::Draw(s, GL_TRIANGLES, prog);
    }
};

template <typename TriV, typename LineV>
struct MeshPair {
    
    TriMesh<TriV>   tri;
    LineMesh<LineV> line;

    typedef TriV TriVertex;
    typedef LineV LineVertex;
    typedef typename Mesh<TriV>::scope TriScope;
    typedef typename Mesh<LineV>::scope LineScope;

    struct Scope {

        typedef MeshPair<TriV, LineV> MP;
        typename Mesh<TriV>::scope  s0;
        typename Mesh<LineV>::scope s1;

        Scope(MP &mp) : s0(mp.tri), s1(mp.line) {}
        
        Scope(MP &mp, float2 pos, float angle) : s0(mp.tri), s1(mp.line)
        {
            mp.tri.translateRotate(pos, angle);
            mp.line.transform = mp.tri.transform;
        }

        Scope(MP &mp, float2 pos) : s0(mp.tri), s1(mp.line)
        {
            mp.tri.translate(pos);
            mp.line.transform = mp.tri.transform;
        }
    };

    struct Handle {
        MeshPair &mp;
        Handle(MeshPair &m) : mp(m) { mp.start(); }
        ~Handle() { mp.finish(); }

        template <typename TriP, typename LineP>
        void Draw(const ShaderState& ss, const TriP &trip, const LineP &linep)
        {
            mp.Draw(ss, trip, linep);
        }

        void Draw(const ShaderState& ss);
    };

    void start()
    {
        ASSERT(tri.empty() && line.empty());
    }

    void finish()
    {
        clear();
    }

    void clear()
    {
        tri.clear();
        line.clear();
    }

    bool empty() const
    {
        return tri.empty() && line.empty();
    }

    size_t getSizeof() const
    {
        return tri.getSizeof() + line.getSizeof();
    }

    template <typename TriP, typename LineP>
    void Draw(const ShaderState& ss, const TriP &trip, const LineP &linep)
    {
        tri.Draw(ss, trip);
        line.Draw(ss, linep);
    }
};


typedef Mesh<VertexPosColor> VertexPusher;
typedef TriMesh<VertexPosColor> VertexPusherTri;
typedef LineMesh<VertexPosColor> VertexPusherLine;
typedef MeshPair<VertexPosColor, VertexPosColor>  DMesh;

// generic shared dmesh
inline DMesh& theDMesh()
{
    ASSERT_MAIN_THREAD();
    static DMesh mesh;
    return mesh;
}

enum ButtonStyle { S_BOX=1, S_CORNERS=2, S_FIXED=4, S_OVAL=8, S_3D=16 };

void PushButton(TriMesh<VertexPosColor>* triP, LineMesh<VertexPosColor>* lineP, float2 pos, float2 r, 
                uint bgColor, uint fgColor, float alpha);

void DrawButton(const ShaderState *data, float2 pos, float2 r, uint bgColor, uint fgColor, float alpha=1);

void PushRect(TriMesh<VertexPosColor>* triP, LineMesh<VertexPosColor>* lineP, float2 pos, float2 r, uint bgColor, uint fgColor, float alpha);
void DrawFilledRect(const ShaderState &data, float2 pos, float2 r, uint bgColor, uint fgColor, float alpha=1);

void fadeFullScreen(const ShaderState &ss, const View& view, uint color, float alpha);
void sexyFillScreen(const ShaderState &ss, const View& view, uint color, uint color1, float alpha);

// P is upper left corner, S is size
float2 DrawBar(const ShaderState &ss, uint fill, uint line, float alpha, float2 p, float2 s, float a);

void DrawAlignedGrid(ShaderState &wss, const View& view, float size, float z); 

template <typename Prog, typename Vtx>
void DrawElements(const Prog& prog, const ShaderState &ss, GLenum type, const Vtx *verts, const uint *il, uint ilc)
{
    prog.UseProgram(ss, verts, (Vtx*)NULL);
    ss.DrawElements(type, ilc, il);
    prog.UnuseProgram();
}


// encapsulate full screen blur 

struct PostProc {

private:
    GLRenderTexture m_tex[2];
    uint            m_rtIdx = 0;
    int             m_blur  = 0;
    float2          m_res;

    GLRenderTexture &getRead() { return m_tex[m_rtIdx]; }
    GLRenderTexture &getWrite() { return m_tex[!m_rtIdx]; }    
public:

    void setRes(float2 res) { m_res = res; }
    void setBlur(uint blur) { m_blur = blur; }
    
    void swapRW() { m_rtIdx = !m_rtIdx; }

    void BindWriteFramebuffer();
    void UnbindWriteFramebuffer();
    void Draw(bool drawFinal);

    bool isWriteReady() const { return !const_cast<PostProc*>(this)->getWrite().empty(); }

    void clear()
    {
        m_tex[0].clear();
        m_tex[1].clear();
    }
};



#endif /* defined(__Outlaws__Graphics__) */
