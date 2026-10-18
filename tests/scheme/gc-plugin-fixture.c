/* A disposable native plugin with writable, collector-visible static roots. */
static void *roots[4096];

void store_root (void *value) { roots[4095] = value; }
void *read_root (void) { return roots[4095]; }
