#pragma once

#include "public.h"

#include <yt/yt/core/json/public.h>
#include <yt/yt/core/json/config.h>

#include <yt/yt/core/ytree/public.h>
#include <yt/yt/core/ytree/yson_serializable.h>

namespace NYT::NLogging {

////////////////////////////////////////////////////////////////////////////////

class TFileLogWriterConfig
    : public NYTree::TYsonSerializable
{
public:
    static constexpr const TStringBuf Type = "file";

    TString FileName;
    bool EnableCompression;
    ECompressionMethod CompressionMethod;
    int CompressionLevel;

    TFileLogWriterConfig();
};

DEFINE_REFCOUNTED_TYPE(TFileLogWriterConfig)

////////////////////////////////////////////////////////////////////////////////

class TStderrLogWriterConfig
    : public NYTree::TYsonSerializable
{
public:
    static constexpr TStringBuf Type = "stderr";
};

DEFINE_REFCOUNTED_TYPE(TStderrLogWriterConfig)

////////////////////////////////////////////////////////////////////////////////

class TLogWriterConfig
    : public NYTree::TYsonSerializable
{
public:
    TString Type;

    ELogFormat Format;

    std::optional<i64> RateLimit;

    //! Common formatter options.
    std::optional<bool> EnableSystemMessages;

    //! Plain text formatter options.
    bool EnableSourceLocation;

    //! Structured formatter options.
    THashMap<TString, NYTree::INodePtr> CommonFields;
    NJson::TJsonFormatConfigPtr JsonFormat;

    ELogFamily GetFamily() const;
    bool AreSystemMessagesEnabled() const;

    //! Constructs a full config by combining parameters from this one and #typedConfig.
    template <class TTypedConfigPtr>
    NYTree::IMapNodePtr BuildFullConfig(const TTypedConfigPtr& typedConfig);

    TLogWriterConfig();
};

DEFINE_REFCOUNTED_TYPE(TLogWriterConfig)

////////////////////////////////////////////////////////////////////////////////

class TRuleConfig
    : public NYTree::TYsonSerializable
{
public:
    std::optional<THashSet<TString>> IncludeCategories;
    THashSet<TString> ExcludeCategories;

    ELogLevel MinLevel;
    ELogLevel MaxLevel;

    ELogFamily Family;

    std::vector<TString> Writers;

    TRuleConfig();

    bool IsApplicable(TStringBuf category, ELogFamily family) const;
    bool IsApplicable(TStringBuf category, ELogLevel level, ELogFamily family) const;
};

DEFINE_REFCOUNTED_TYPE(TRuleConfig)

////////////////////////////////////////////////////////////////////////////////

class TLogManagerConfig
    : public NYTree::TYsonSerializable
{
public:
    std::optional<TDuration> FlushPeriod;
    std::optional<TDuration> WatchPeriod;
    std::optional<TDuration> CheckSpacePeriod;

    i64 MinDiskSpace;

    int HighBacklogWatermark;
    int LowBacklogWatermark;

    TDuration ShutdownGraceTimeout;

    std::vector<TRuleConfigPtr> Rules;
    THashMap<TString, NYTree::IMapNodePtr> Writers;
    std::vector<TString> SuppressedMessages;
    THashMap<TString, i64> CategoryRateLimits;

    TDuration RequestSuppressionTimeout;

    bool EnableAnchorProfiling;
    double MinLoggedMessageRateToProfile;

    bool AbortOnAlert;

    int CompressionThreadCount;

    TLogManagerConfig();

    TLogManagerConfigPtr ApplyDynamic(const TLogManagerDynamicConfigPtr& dynamicConfig) const;

    static TLogManagerConfigPtr CreateStderrLogger(ELogLevel logLevel);
    static TLogManagerConfigPtr CreateLogFile(const TString& path);
    static TLogManagerConfigPtr CreateDefault();
    static TLogManagerConfigPtr CreateQuiet();
    static TLogManagerConfigPtr CreateSilent();
    //! Create logging config a-la YT server config: #directory/#componentName{,.debug,.error}.log.
    //! Also allows adding structured logs. For example, pair ("RpcProxyStructuredMain", "main") would
    //! make structured messages with RpcProxyStructuredMain category go to #directory/#componentName.yson.main.log.
    static TLogManagerConfigPtr CreateYTServer(
        const TString& componentName,
        const TString& directory = ".",
        const THashMap<TString, TString>& structuredCategoryToWriterName = {});
    static TLogManagerConfigPtr CreateFromFile(const TString& file, const NYPath::TYPath& path = "");
    static TLogManagerConfigPtr CreateFromNode(NYTree::INodePtr node, const NYPath::TYPath& path = "");
    static TLogManagerConfigPtr TryCreateFromEnv();

    //! Applies #updater to each writer config in #Writers.
    //! If null is returned then the writer is removed, otherwise it is replaced.
    void UpdateWriters(const std::function<NYTree::IMapNodePtr(const NYTree::IMapNodePtr&)> updater);
};

DEFINE_REFCOUNTED_TYPE(TLogManagerConfig)

////////////////////////////////////////////////////////////////////////////////

class TLogManagerDynamicConfig
    : public NYTree::TYsonSerializable
{
public:
    std::optional<i64> MinDiskSpace;

    std::optional<int> HighBacklogWatermark;
    std::optional<int> LowBacklogWatermark;

    std::optional<std::vector<TRuleConfigPtr>> Rules;
    std::optional<std::vector<TString>> SuppressedMessages;
    std::optional<THashMap<TString, i64>> CategoryRateLimits;

    std::optional<TDuration> RequestSuppressionTimeout;

    std::optional<bool> EnableAnchorProfiling;
    std::optional<double> MinLoggedMessageRateToProfile;

    std::optional<bool> AbortOnAlert;

    std::optional<int> CompressionThreadCount;

    TLogManagerDynamicConfig();
};

DEFINE_REFCOUNTED_TYPE(TLogManagerDynamicConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLogging

#define CONFIG_INL_H_
#include "config-inl.h"
#undef CONFIG_INL_H_
