// Synthetic CI fixture, independently written; contains no game code.
#include "psprecomp/runtime.hpp"
#include "psprecomp/common.hpp"
namespace psprecomp {void register_generated_functions(Runtime &) {throw Error("Host-test build has no game corpus; rebuild from your own ISO");}}
