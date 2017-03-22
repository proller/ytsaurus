#pragma once

#include <util/generic/ptr.h>
#include <util/generic/stroka.h>
#include <util/system/compat.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class ILogger
    : public TThrRefBase
{
public:
    enum ELevel
    {
        FATAL,
        ERROR,
        INFO,
        DEBUG
    };

    virtual void Log(ELevel level, const char* file, int line, const char* format, va_list args) = 0;
};

using ILoggerPtr = TIntrusivePtr<ILogger>;

void SetLogger(ILoggerPtr logger);
ILoggerPtr GetLogger();

ILoggerPtr CreateStdErrLogger(ILogger::ELevel cutLevel);
ILoggerPtr CreateFileLogger(ILogger::ELevel cutLevel, const Stroka& path, bool append = false);

////////////////////////////////////////////////////////////////////////////////

inline void LogMessage(ILogger::ELevel level, const char* file, int line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    GetLogger()->Log(level, file, line, format, args);
    va_end(args);
}

#define LOG_DEBUG(...) { NYT::LogMessage(NYT::ILogger::DEBUG, __FILE__, __LINE__, __VA_ARGS__); }
#define LOG_INFO(...)  { NYT::LogMessage(NYT::ILogger::INFO, __FILE__, __LINE__, __VA_ARGS__); }
#define LOG_ERROR(...) { NYT::LogMessage(NYT::ILogger::ERROR, __FILE__, __LINE__, __VA_ARGS__); }
#define LOG_FATAL(...) { NYT::LogMessage(NYT::ILogger::FATAL, __FILE__, __LINE__, __VA_ARGS__); exit(1); }

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
