#pragma once
#include "app/FileSystem/FileSystem.h"
void host_fs_set_root(const char *root);
FileSystem *host_fs_instance();
