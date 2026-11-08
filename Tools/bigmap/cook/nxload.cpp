// Loads cooked NXS triangle meshes the way the game does: PhysXLoader.dll 2.8.4 SDK, then
// NxPhysicsSDK::createTriangleMesh (vtable slot 8, called at 0xE50A5E) on a memory stream.
// usage: nxload <PhysXLoader.dll> <mesh.nxs>...
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

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

class MemoryReadStream : public NxStream
{
public:
    explicit MemoryReadStream(const std::vector<uint8_t>& d) : data(d) {}
    const std::vector<uint8_t>& data;
    mutable size_t pos = 0;
    mutable bool overrun = false;

    template <typename T> T get() const
    {
        T v{};
        readBuffer(&v, sizeof v);
        return v;
    }
    uint8_t readByte() const override { return get<uint8_t>(); }
    uint16_t readWord() const override { return get<uint16_t>(); }
    uint32_t readDword() const override { return get<uint32_t>(); }
    float readFloat() const override { return get<float>(); }
    double readDouble() const override { return get<double>(); }
    void readBuffer(void* buffer, uint32_t size) const override
    {
        if (pos + size > data.size())
        {
            overrun = true;
            memset(buffer, 0, size);
            return;
        }
        memcpy(buffer, data.data() + pos, size);
        pos += size;
    }
    NxStream& storeByte(uint8_t) override { return *this; }
    NxStream& storeWord(uint16_t) override { return *this; }
    NxStream& storeDword(uint32_t) override { return *this; }
    NxStream& storeFloat(float) override { return *this; }
    NxStream& storeDouble(double) override { return *this; }
    NxStream& storeBuffer(const void*, uint32_t) override { return *this; }
};

struct NxPhysicsSDKDesc
{
    uint32_t hwPageSize = 65536;
    uint32_t hwPageMax = 256;
    uint32_t hwConvexMax = 2048;
    uint32_t cookerThreadMask = 0;
    uint32_t flags = 2;  // NX_SDKF_NO_HARDWARE
    uint32_t gpuHeapSize = 32;
    uint32_t meshCacheSize = 0;
    uint32_t reserved[16] = {};
};

using CreateSDK = void*(__cdecl*)(uint32_t, void*, void*, const NxPhysicsSDKDesc&, int*);
using CreateMesh = void*(__thiscall*)(void*, const NxStream&);
using GetCount = uint32_t(__thiscall*)(void*, uint32_t, uint32_t);

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

static void* createMesh(void* sdk, const NxStream& s, bool& crashed)
{
    crashed = false;
    __try
    {
        auto fn = (*reinterpret_cast<CreateMesh**>(sdk))[8];
        return fn(sdk, s);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        crashed = true;
        return nullptr;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: nxload <PhysXLoader.dll> <mesh.nxs>...\n");
        return 2;
    }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll)
    {
        fprintf(stderr, "cannot load %s (%lu)\n", argv[1], GetLastError());
        return 3;
    }
    auto create = reinterpret_cast<CreateSDK>(GetProcAddress(dll, "NxCreatePhysicsSDK"));
    if (!create)
    {
        fprintf(stderr, "missing NxCreatePhysicsSDK\n");
        return 3;
    }
    NxPhysicsSDKDesc desc;
    int err = -1;
    void* sdk = create(0x02080400, nullptr, nullptr, desc, &err);
    if (!sdk)
    {
        fprintf(stderr, "NxCreatePhysicsSDK failed (%d)\n", err);
        return 4;
    }
    int bad = 0;
    for (int i = 2; i < argc; ++i)
    {
        std::vector<uint8_t> blob;
        if (!readFile(argv[i], blob) || blob.size() < 36)
        {
            printf("%s: unreadable\n", argv[i]);
            ++bad;
            continue;
        }
        uint32_t nv = 0, nt = 0;
        memcpy(&nv, blob.data() + 28, 4);
        memcpy(&nt, blob.data() + 32, 4);
        MemoryReadStream s(blob);
        bool crashed = false;
        void* mesh = createMesh(sdk, s, crashed);
        if (!mesh)
        {
            printf("%s: REJECTED%s\n", argv[i], crashed ? " (exception)" : "");
            ++bad;
            continue;
        }
        printf("%s: loaded, read %zu of %zu bytes%s (%u vertices, %u triangles)\n", argv[i], s.pos, blob.size(),
               s.overrun ? " OVERRUN" : "", nv, nt);
        if (s.overrun || s.pos != blob.size())
            ++bad;
    }
    printf("meshes %d, problems %d\n", argc - 2, bad);
    return bad ? 1 : 0;
}
