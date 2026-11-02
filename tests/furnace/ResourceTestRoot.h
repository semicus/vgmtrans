/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#pragma once
#include "Root.h"

class ResourceTestRoot : public VGMRoot {
public:
  void UI_setRootPtr(VGMRoot** root) override { *root = this; }
  std::filesystem::path UI_getSaveFilePath(const std::string&, const std::string&) override { return {}; }
  std::filesystem::path UI_getSaveDirPath(const std::filesystem::path&) override { return {}; }
  std::filesystem::path UI_getResourceDirPath() override { return FURNACE_TEST_RESOURCE_DIR; }
  ~ResourceTestRoot() override { removeAllFilesAndCollections(); }
};
