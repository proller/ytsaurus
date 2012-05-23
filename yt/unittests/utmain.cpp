#include "stdafx.h"

#include <ytlib/bus/nl_client.h>
#include <ytlib/logging/log_manager.h>
#include <ytlib/profiling/profiling_manager.h>
#include <ytlib/meta_state/async_change_log.h>
#include <ytlib/misc/delayed_invoker.h>

#include <util/datetime/base.h>
#include <util/random/random.h>
#include <util/string/printf.h>

#include <contrib/testing/framework.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

Stroka GenerateRandomFileName(const char* prefix)
{
    return Sprintf("%s-%016" PRIx64 "-%016" PRIx64,
        prefix,
        MicroSeconds(),
        RandomNumber<ui64>());
}

////////////////////////////////////////////////////////////////////////////////
 
} // namespace NYT

namespace testing {

////////////////////////////////////////////////////////////////////////////////

Matcher<const TStringBuf&>::Matcher(const Stroka& s)
{
    *this = Eq(TStringBuf(s));
}

Matcher<const TStringBuf&>::Matcher(const char* s)
{
    *this = Eq(TStringBuf(s));
}

Matcher<const Stroka&>::Matcher(const Stroka& s)
{
    *this = Eq(s);
}

Matcher<const Stroka&>::Matcher(const char* s)
{
    *this = Eq(Stroka(s));
}

Matcher<Stroka>::Matcher(const Stroka& s)
{
    *this = Eq(s);
}

Matcher<Stroka>::Matcher(const char* s)
{
    *this = Eq(Stroka(s));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace testing

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    int rv = RUN_ALL_TESTS();

    // XXX(sandello): Keep in sync with server/main.cpp, driver/main.cpp and utmain.cpp.
    NYT::NMetaState::TAsyncChangeLog::Shutdown();
    NYT::NLog::TLogManager::Get()->Shutdown();
    NYT::NProfiling::TProfilingManager::Get()->Shutdown();
    NYT::NBus::TNLClientManager::Get()->Shutdown();
    NYT::TDelayedInvoker::Shutdown();

    return rv;
}
