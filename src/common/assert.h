#ifndef KYTY_COMMON_ASSERT_H_
#define KYTY_COMMON_ASSERT_H_

#include "common/common.h"
#include "common/logging/log.h"

#include <cstdlib>
#include <string_view>

namespace Common {

#ifdef __clang__
int DbgExitHandler(char const* file, int line, std::string_view text)
    __attribute__((analyzer_noreturn));
int DbgExitHandler(char const* file, int line, fmt::text_style style, std::string_view text)
    __attribute__((analyzer_noreturn));
int DbgExitIfHandler(char const* expr, char const* file, int line)
    __attribute__((analyzer_noreturn));
int DbgNotImplementedHandler(char const* expr, char const* file, int line)
    __attribute__((analyzer_noreturn));
#else
int DbgExitHandler(char const* file, int line, std::string_view text);
int DbgExitHandler(char const* file, int line, fmt::text_style style, std::string_view text);
int DbgExitIfHandler(char const* expr, char const* file, int line);
int DbgNotImplementedHandler(char const* expr, char const* file, int line);
#endif
[[noreturn]] void DbgExit(int status);

} // namespace Common

#define EXIT_HALT() (Common::DbgExit(321), 1)

// The handlers only report the failure. DbgExit never returns, so the compiler can see that a
// failing check leaves the function.
#ifndef KYTY_FINAL
#define EXIT_IF(x)                                                                                 \
	((x) ? (Common::DbgExitIfHandler(#x, __FILE__, __LINE__), Common::DbgExit(321)) : (void)0)
#else
// Still evaluate the condition: many call sites wrap work with side effects, such as
// EXIT_IF(!map.emplace(...).second), that must run in every build.
#define EXIT_IF(x) ((void)(x))
#endif

#define EXIT(...)                                                                                  \
	do {                                                                                           \
		Common::DbgExitHandler(__FILE__, __LINE__, ::fmt::sprintf(__VA_ARGS__));                   \
		Common::DbgExit(321);                                                                      \
	} while (0)

#define EXIT_COLOR(style, ...)                                                                     \
	do {                                                                                           \
		Common::DbgExitHandler(__FILE__, __LINE__, (style), ::fmt::sprintf(__VA_ARGS__));          \
		Common::DbgExit(321);                                                                      \
	} while (0)

#define EXIT_NOT_IMPLEMENTED(x)                                                                    \
	((x) ? (Common::DbgNotImplementedHandler(#x, __FILE__, __LINE__), Common::DbgExit(321))        \
	     : (void)0)
#define KYTY_NOT_IMPLEMENTED EXIT_NOT_IMPLEMENTED(true)

#endif /* KYTY_COMMON_ASSERT_H_ */
