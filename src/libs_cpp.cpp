#include "portable-file-dialogs.h"

// the functions in pfd are all inline,
// so make sure they are actually instantiated
void pfd_dummy() {
    (void) pfd::save_file("a", "b").result();
}
