// Host FileSystem backed by a real local directory (emulator/sdcard/).
#include "app/FileSystem/FileSystem.h"
#include <string>
#include <sys/stat.h>
#include <dirent.h>
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

    // The host folder has no partition size of its own, so report the rev_8 FAT
    // partition (8 MB, partition_esp32s3_rev8_16mb.csv) and sum the files in it -
    // the Storage screen then previews with the same shape of numbers as the device.
    size_t totalBytes() override { return 8 * 1024 * 1024; }

    size_t usedBytes() override
    {
        size_t used = 0;
        if (DIR *d = ::opendir(g_root.c_str()))
        {
            while (struct dirent *e = ::readdir(d))
            {
                struct stat st;
                if (::stat((g_root + "/" + e->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode))
                    used += (size_t)st.st_size;
            }
            ::closedir(d);
        }
        return used;
    }
};

FileSystem *host_fs_instance()
{
    static HostFileSystem fs;
    return &fs;
}
