#include <Base/Types.h>

namespace Scripting
{
    struct Zenith;

    namespace Util::Zenith
    {
        ::Scripting::Zenith* GetGlobal();

        // No-op for ref == -1 (LUA_NOREF). zenith must be non-null.
        void Unref(::Scripting::Zenith* zenith, i32 ref);
    }
}