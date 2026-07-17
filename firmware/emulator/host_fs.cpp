// Host FileSystem backed by a real local directory (emulator/sdcard/).
#include "app/FileSystem/FileSystem.h"
#include <string>
#include <sys/stat.h>
#include <cstdio>

#include "host_fs.h"

static std::string g_root = "sdcard";

void host_fs_set_root(const char *root) { g_root = root; }

static std::string full(const char *path)
{
    std::string p = path ? path : "";
    if (!p.empty() && p[0] == '/') return g_root + p;
    return g_root + "/" + p;
}

class HostFileSystem : public FileSystem
{
public:
    bool begin() override
    {
        ::mkdir(g_root.c_str(), 0755);
        return true;
    }

    File open(const char *path, const char *mode) override
    {
        FILE *f = fopen(full(path).c_str(), mode);
        return File(f);
    }

    bool exists(const char *path) override
    {
        struct stat st;
        return ::stat(full(path).c_str(), &st) == 0;
    }

    bool remove(const char *path) override { return ::remove(full(path).c_str()) == 0; }

    bool rename(const char *from, const char *to) override
    {
        return ::rename(full(from).c_str(), full(to).c_str()) == 0;
    }
};

FileSystem *host_fs_instance()
{
    static HostFileSystem fs;
    return &fs;
}
