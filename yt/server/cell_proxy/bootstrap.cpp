#include "bootstrap.h"
#include "config.h"

#include <yt/server/admin_server/admin_service.h>

#include <yt/server/blackbox/default_blackbox_service.h>
#include <yt/server/blackbox/token_authenticator.h>
#include <yt/server/blackbox/cookie_authenticator.h>

#include <yt/server/misc/address_helpers.h>
#include <yt/server/misc/build_attributes.h>

#include <yt/server/rpc_proxy/api_service.h>
#include <yt/server/rpc_proxy/discovery_service.h>
#include <yt/server/rpc_proxy/proxy_coordinator.h>

#include <yt/ytlib/api/native_client.h>
#include <yt/ytlib/api/native_connection.h>

#include <yt/ytlib/monitoring/http_integration.h>
#include <yt/ytlib/monitoring/http_server.h>
#include <yt/ytlib/monitoring/monitoring_manager.h>

#include <yt/ytlib/orchid/orchid_service.h>

#include <yt/core/bus/config.h>
#include <yt/core/bus/server.h>
#include <yt/core/bus/tcp_server.h>

#include <yt/core/concurrency/thread_pool.h>

#include <yt/core/net/address.h>

#include <yt/core/misc/core_dumper.h>
#include <yt/core/misc/ref_counted_tracker.h>
#include <yt/core/misc/lfalloc_helpers.h>

#include <yt/core/profiling/profile_manager.h>

#include <yt/core/rpc/bus_channel.h>
#include <yt/core/rpc/bus_server.h>
#include <yt/core/rpc/response_keeper.h>
#include <yt/core/rpc/retrying_channel.h>
#include <yt/core/rpc/server.h>

#include <yt/core/http/server.h>

#include <yt/core/ytree/virtual.h>
#include <yt/core/ytree/ypath_client.h>

namespace NYT {
namespace NCellProxy {

using namespace NAdmin;
using namespace NBus;
using namespace NMonitoring;
using namespace NOrchid;
using namespace NProfiling;
using namespace NRpc;
using namespace NYTree;
using namespace NConcurrency;
using namespace NApi;
using namespace NRpcProxy;
using namespace NBlackbox;

////////////////////////////////////////////////////////////////////////////////

static const NLogging::TLogger Logger("Bootstrap");

////////////////////////////////////////////////////////////////////////////////

TBootstrap::TBootstrap(TCellProxyConfigPtr config, INodePtr configNode)
    : Config_(std::move(config))
    , ConfigNode_(std::move(configNode))
{ }

TBootstrap::~TBootstrap() = default;

void TBootstrap::Run()
{
    ControlQueue_ = New<TActionQueue>("RpcProxy");

    BIND(&TBootstrap::DoRun, this)
        .AsyncVia(ControlQueue_->GetInvoker())
        .Run()
        .Get()
        .ThrowOnError();

    Sleep(TDuration::Max());
}

void TBootstrap::DoRun()
{
    LocalAddresses_ = NYT::GetLocalAddresses(Config_->Addresses, Config_->RpcPort);

    LOG_INFO("Starting proxy (LocalAddresses: %v, PrimaryMasterAddresses: %v)",
        GetValues(LocalAddresses_),
        Config_->ClusterConnection->PrimaryMaster->Addresses);

    TNativeConnectionOptions connectionOptions;
    connectionOptions.RetryRequestQueueSizeLimitExceeded = true;
    NativeConnection_ = CreateNativeConnection(Config_->ClusterConnection, connectionOptions);

    TClientOptions clientOptions;
    clientOptions.User = NSecurityClient::RootUserName;
    NativeClient_ = NativeConnection_->CreateNativeClient(clientOptions);

    auto blackbox = CreateDefaultBlackboxService(Config_->Blackbox, GetControlInvoker());
    CookieAuthenticator_ = CreateCookieAuthenticator(Config_->CookieAuthenticator, blackbox);
    TokenAuthenticator_ = CreateBlackboxTokenAuthenticator(Config_->TokenAuthenticator, blackbox);
    TokenAuthenticator_ = CreateCachingTokenAuthenticator(Config_->TokenAuthenticator, TokenAuthenticator_);
    ProxyCoordinator_ = CreateProxyCoordinator();

    BusServer_ = CreateTcpBusServer(Config_->BusServer);

    RpcServer_ = CreateBusServer(BusServer_);

    if (!Config_->UseNewHttpServer) {
        HttpServer_.reset(new NXHttp::TServer(
            Config_->MonitoringPort,
            Config_->BusServer->BindRetryCount,
            Config_->BusServer->BindRetryBackoff));
    } else {
        Config_->MonitoringServer->Port = Config_->MonitoringPort;
        Config_->MonitoringServer->BindRetryCount = Config_->BusServer->BindRetryCount;
        Config_->MonitoringServer->BindRetryBackoff = Config_->BusServer->BindRetryBackoff;
        NewHttpServer_ = NHttp::CreateServer(
            Config_->MonitoringServer);
    }

    if (Config_->CoreDumper) {
        CoreDumper_ = New<TCoreDumper>(Config_->CoreDumper);
    }

    MonitoringManager_ = New<TMonitoringManager>();
    MonitoringManager_->Register(
        "/ref_counted",
        TRefCountedTracker::Get()->GetMonitoringProducer());
    MonitoringManager_->Start();

    LFAllocProfiler_ = std::make_unique<NLFAlloc::TLFAllocProfiler>();

    auto orchidRoot = NYTree::GetEphemeralNodeFactory(true)->CreateMap();
    SetNodeByYPath(
        orchidRoot,
        "/monitoring",
        CreateVirtualNode(MonitoringManager_->GetService()));
    SetNodeByYPath(
        orchidRoot,
        "/profiling",
        CreateVirtualNode(TProfileManager::Get()->GetService()));
    SetNodeByYPath(
        orchidRoot,
        "/config",
        ConfigNode_);

    SetBuildAttributes(orchidRoot, "proxy");

    RpcServer_->RegisterService(CreateOrchidService(
        orchidRoot,
        GetControlInvoker()));
    RpcServer_->RegisterService(CreateApiService(this));
    RpcServer_->RegisterService(CreateDiscoveryService(this));

    if (HttpServer_) {
        HttpServer_->Register(
            "/orchid",
            NMonitoring::GetYPathHttpHandler(orchidRoot->Via(GetControlInvoker())));
    } else {
        NewHttpServer_->AddHandler(
            "/orchid/",
            NMonitoring::GetOrchidYPathHttpHandler(orchidRoot->Via(GetControlInvoker())));
    }

    LOG_INFO("Listening for HTTP requests on port %v", Config_->MonitoringPort);
    if (HttpServer_) {
        HttpServer_->Start();
    } else {
        NewHttpServer_->Start();
    }

    LOG_INFO("Listening for RPC requests on port %v", Config_->RpcPort);
    RpcServer_->Configure(Config_->RpcServer);
    RpcServer_->Start();
}

const TCellProxyConfigPtr& TBootstrap::GetConfig() const
{
    return Config_;
}

const IInvokerPtr& TBootstrap::GetControlInvoker() const
{
    return ControlQueue_->GetInvoker();
}

const INativeConnectionPtr& TBootstrap::GetNativeConnection() const
{
    return NativeConnection_;
}

const INativeClientPtr& TBootstrap::GetNativeClient() const
{
    return NativeClient_;
}

const ITokenAuthenticatorPtr& TBootstrap::GetTokenAuthenticator() const
{
    return TokenAuthenticator_;
}

const ICookieAuthenticatorPtr& TBootstrap::GetCookieAuthenticator() const
{
    return CookieAuthenticator_;
}

const IProxyCoordinatorPtr& TBootstrap::GetProxyCoordinator() const
{
    return ProxyCoordinator_;
}

const NNodeTrackerClient::TAddressMap& TBootstrap::GetLocalAddresses() const
{
    return LocalAddresses_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCellProxy
} // namespace NYT
