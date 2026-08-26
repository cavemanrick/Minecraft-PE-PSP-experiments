#include "world/level/pathfinder/path.h"
#include "world/entity/entity.h"

int Path::p = 0;

Path::Path() : length(0), index(0) { ++p; }
Path::~Path() { destroy(); }

bool Path::isEmpty() const { return length == 0; }

void Path::copyNodes(Node** src, int len) {
    if (len > MAX_PATH) len = MAX_PATH;
    length = len;
    index = 0;
    for (int i = 0; i < len; ++i) nodes[i] = *src[i];
}

void Path::destroy() { index = length = 0; }

Node* Path::currentPos() { return length ? (Node*)&nodes[index] : 0; }
Vec3  Path::currentPos(Entity* e) const { return getPos(e, index); }
void  Path::next() { index++; }
int   Path::getSize() const { return length; }
bool  Path::isDone() const { return index >= length; }
Node* Path::last() const { return length > 0 ? (Node*)&nodes[length - 1] : 0; }
Node* Path::get(int i) const { return (Node*)&nodes[i]; }
int   Path::getIndex() const { return index; }
void  Path::setIndex(int i) { index = i; }

Vec3 Path::getPos(Entity* e, int i) const {
    float x = nodes[i].x + (int)(e->bbWidth + 1) * 0.5f;
    float z = nodes[i].z + (int)(e->bbWidth + 1) * 0.5f;
    float y = nodes[i].y;
    return Vec3(x, y, z);
}

// --- Path pool ------------------------------------------------------------
// Static storage, no allocation, no fragmentation -- the same reasoning
// behind Entity's slot pool. A linear scan over twelve bools is not worth
// a free-list.

static Path s_pool[PATH_POOL_SIZE];
static bool s_poolUsed[PATH_POOL_SIZE];

namespace PathPool {

Path* acquire() {
    for (int i = 0; i < PATH_POOL_SIZE; ++i) {
        if (!s_poolUsed[i]) {
            s_poolUsed[i] = true;
            // Reset on the way out rather than on release, so a path is
            // always clean when handed over regardless of how its previous
            // holder let go of it.
            s_pool[i].destroy();
            return &s_pool[i];
        }
    }
    return 0;
}

void release(Path*& p) {
    if (!p) return;
    for (int i = 0; i < PATH_POOL_SIZE; ++i) {
        if (&s_pool[i] == p) {
            s_pool[i].destroy();
            s_poolUsed[i] = false;
            break;
        }
    }
    // Cleared even if the pointer was not ours. A path that did not come
    // from the pool is a bug elsewhere, and leaving the caller holding it
    // would turn that bug into a use-after-free later.
    p = 0;
}

int inUse() {
    int n = 0;
    for (int i = 0; i < PATH_POOL_SIZE; ++i) if (s_poolUsed[i]) ++n;
    return n;
}

}
