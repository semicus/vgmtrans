/* VGMTrans (c) 2026. Licensed under the zlib license; see LICENSE. */
#include "ResourceTestRoot.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  ResourceTestRoot root; root.init();
  if (!root.openRawFile(argv[1])) return 1;
  std::filesystem::create_directories(argv[2]);
  unsigned i = 0;
  for (auto* coll : root.vgmColls()) {
    if (!coll->seq()) continue;
    if (!coll->seq()->saveAsMidi(std::filesystem::path(argv[2]) / (std::to_string(i++) + ".mid"), coll)) return 1;
  }
  std::cout << i << " MIDI files\n";
}
