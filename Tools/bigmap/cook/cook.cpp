// Cooks a triangle mesh with the game's PhysXCooking.dll (PhysX 2.8.4.4, x86).
// usage: cook <PhysXCooking.dll> <input> <output>
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <vector>

struct NxCookingParams
{
    int targetPlatform;
    float skinWidth;
    bool hintCollisionSpeed;
};

struct NxTriangleMeshDesc
{
    uint32_t numVertices;
    uint32_t numTriangles;
    uint32_t pointStrideBytes;
    uint32_t triangleStrideBytes;
    const void* points;
    const void* triangles;
    uint32_t flags;
    uint32_t materialIndexStride;
    const void* materialIndices;
    int heightFieldVerticalAxis;
    float heightFieldVerticalExtent;
    void* pmap;
    float convexEdgeThreshold;
};

class NxStream
{
public:
    virtual ~NxStream() {}
    virtual uint8_t readByte() const = 0;
    virtual uint16_t readWord() const = 0;
    virtual uint32_t readDword() const = 0;
    virtual float readFloat() const = 0;
    virtual double readDouble() const = 0;
    virtual void readBuffer(void* buffer, uint32_t size) const = 0;
    virtual NxStream& storeByte(uint8_t b) = 0;
    virtual NxStream& storeWord(uint16_t w) = 0;
    virtual NxStream& storeDword(uint32_t d) = 0;
    virtual NxStream& storeFloat(float f) = 0;
    virtual NxStream& storeDouble(double f) = 0;
    virtual NxStream& storeBuffer(const void* buffer, uint32_t size) = 0;
};

class MemoryWriteStream : public NxStream
{
public:
    std::vector<uint8_t> data;

    uint8_t readByte() const override { return 0; }
    uint16_t readWord() const override { return 0; }
    uint32_t readDword() const override { return 0; }
    float readFloat() const override { return 0; }
    double readDouble() const override { return 0; }
    void readBuffer(void*, uint32_t) const override {}
    NxStream& storeByte(uint8_t b) override { return storeBuffer(&b, sizeof b); }
    NxStream& storeWord(uint16_t w) override { return storeBuffer(&w, sizeof w); }
    NxStream& storeDword(uint32_t d) override { return storeBuffer(&d, sizeof d); }
    NxStream& storeFloat(float f) override { return storeBuffer(&f, sizeof f); }
    NxStream& storeDouble(double f) override { return storeBuffer(&f, sizeof f); }
    NxStream& storeBuffer(const void* buffer, uint32_t size) override
    {
        const auto* p = static_cast<const uint8_t*>(buffer);
        data.insert(data.end(), p, p + size);
        return *this;
    }
};

using InitCooking = bool(__cdecl*)(void*, void*);
using CloseCooking = void(__cdecl*)();
using SetCookingParams = bool(__cdecl*)(const NxCookingParams&);
using CookTriangleMesh = bool(__cdecl*)(const NxTriangleMeshDesc&, NxStream&);

static bool readFile(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f)
        return false;
    fseek(f, 0, SEEK_END);
    out.resize(static_cast<size_t>(ftell(f)));
    fseek(f, 0, SEEK_SET);
    const bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "usage: cook <PhysXCooking.dll> <input> <output>\n");
        return 2;
    }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll)
    {
        fprintf(stderr, "cannot load %s (%lu)\n", argv[1], GetLastError());
        return 3;
    }
    auto init = reinterpret_cast<InitCooking>(GetProcAddress(dll, "NxInitCooking"));
    auto close = reinterpret_cast<CloseCooking>(GetProcAddress(dll, "NxCloseCooking"));
    auto setParams = reinterpret_cast<SetCookingParams>(GetProcAddress(dll, "NxSetCookingParams"));
    auto cook = reinterpret_cast<CookTriangleMesh>(GetProcAddress(dll, "NxCookTriangleMesh"));
    if (!init || !close || !setParams || !cook)
    {
        fprintf(stderr, "missing exports\n");
        return 3;
    }

    std::vector<uint8_t> in;
    if (!readFile(argv[2], in) || in.size() < 20)
    {
        fprintf(stderr, "cannot read %s\n", argv[2]);
        return 4;
    }
    const auto* h = reinterpret_cast<const uint32_t*>(in.data());
    const uint32_t nv = h[0], nt = h[1], flags = h[2];
    float skin;
    memcpy(&skin, &h[3], 4);
    const size_t need = 20 + 12ull * nv + 12ull * nt;
    if (in.size() != need)
    {
        fprintf(stderr, "input size %zu, expected %zu\n", in.size(), need);
        return 4;
    }

    NxTriangleMeshDesc desc{};
    desc.numVertices = nv;
    desc.numTriangles = nt;
    desc.pointStrideBytes = 12;
    desc.triangleStrideBytes = 12;
    desc.points = in.data() + 20;
    desc.triangles = in.data() + 20 + 12ull * nv;
    desc.flags = flags;
    desc.heightFieldVerticalAxis = 0xFF;
    desc.convexEdgeThreshold = 0.001f;

    if (!init(nullptr, nullptr))
    {
        fprintf(stderr, "NxInitCooking failed\n");
        return 5;
    }
    NxCookingParams params{ 0, skin, h[4] != 0 };
    if (!setParams(params))
    {
        fprintf(stderr, "NxSetCookingParams failed\n");
        return 5;
    }
    MemoryWriteStream out;
    const bool ok = cook(desc, out);
    close();
    if (!ok)
    {
        fprintf(stderr, "NxCookTriangleMesh failed\n");
        return 6;
    }
    FILE* f = nullptr;
    if (fopen_s(&f, argv[3], "wb") != 0 || !f)
        return 7;
    fwrite(out.data.data(), 1, out.data.size(), f);
    fclose(f);
    printf("%zu\n", out.data.size());
    return 0;
}
