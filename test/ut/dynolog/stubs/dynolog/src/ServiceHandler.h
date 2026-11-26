// TEST-ONLY minimal ServiceHandler stub. The real header comes from the
// upstream dynolog sources, which are not part of this repository. Only the
// type name is needed to compile SimpleJsonServerBase; the templated
// SimpleJsonServer wrapper is not used by these unit tests.
#pragma once

namespace dynolog
{

class ServiceHandler
{
};

}  // namespace dynolog
