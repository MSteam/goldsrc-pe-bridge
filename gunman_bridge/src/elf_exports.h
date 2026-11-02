#pragma once
#include <cstddef>
struct BridgeExport {const char *name;void *target;};
size_t bridge_publish_exports(const BridgeExport *exports,size_t count,unsigned *calls);
