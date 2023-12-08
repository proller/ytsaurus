# Generated by devtools/yamaker.

LIBRARY()

LICENSE(
    Apache-2.0 AND
    MIT
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/clickhouse/base/base
    contrib/clickhouse/base/widechar_width
    contrib/libs/aws-sdk-cpp/aws-cpp-sdk-core
    contrib/libs/aws-sdk-cpp/aws-cpp-sdk-s3
    contrib/libs/c-ares
    contrib/libs/cctz
    contrib/libs/cctz/tzdata
    contrib/libs/double-conversion
    contrib/libs/fmt
    contrib/libs/libunwind
    contrib/libs/lz4
    contrib/libs/lzma
    contrib/libs/miniselect
    contrib/libs/pdqsort
    contrib/libs/poco/Crypto
    contrib/libs/poco/Foundation
    contrib/libs/poco/JSON
    contrib/libs/poco/Net
    contrib/libs/poco/NetSSL_OpenSSL
    contrib/libs/poco/Util
    contrib/libs/poco/XML
    contrib/libs/re2
    contrib/libs/snappy
    contrib/libs/sparsehash
    contrib/libs/zstd
    contrib/restricted/aws/aws-c-auth
    contrib/restricted/aws/aws-c-common
    contrib/restricted/aws/aws-c-io
    contrib/restricted/aws/aws-c-mqtt
    contrib/restricted/aws/aws-c-sdkutils
    contrib/restricted/aws/aws-crt-cpp
    contrib/restricted/boost/circular_buffer
    contrib/restricted/boost/container_hash
    contrib/restricted/boost/context
    contrib/restricted/boost/convert
    contrib/restricted/boost/dynamic_bitset
    contrib/restricted/boost/filesystem
    contrib/restricted/boost/geometry
    contrib/restricted/boost/heap
    contrib/restricted/boost/multi_index
    contrib/restricted/boost/program_options
    contrib/restricted/boost/system
    contrib/restricted/boost/tti
    contrib/restricted/cityhash-1.0.2
    contrib/restricted/dragonbox
    contrib/restricted/fast_float
    contrib/restricted/magic_enum
    library/cpp/clickhouse_deps/re2_st_stub
    library/cpp/sanitizer/include
)

ADDINCL(
    GLOBAL contrib/clickhouse/base/pcg-random
    GLOBAL contrib/clickhouse/includes/configs
    GLOBAL contrib/clickhouse/src
    contrib/clickhouse/base
    contrib/clickhouse/base/widechar_width
    contrib/libs/double-conversion
    contrib/libs/libunwind/include
    contrib/libs/lz4
    contrib/libs/miniselect/include
    contrib/libs/pdqsort
    contrib/libs/sparsehash/src
    contrib/libs/zstd/include
    contrib/restricted/cityhash-1.0.2
    contrib/restricted/fast_float/include
)

NO_COMPILER_WARNINGS()

NO_UTIL()

IF (OS_DARWIN)
    CFLAGS(
        GLOBAL -DOS_DARWIN
    )
ELSEIF (OS_LINUX)
    CFLAGS(
        GLOBAL -DOS_LINUX
    )
ENDIF()

CFLAGS(
    -DAWS_SDK_VERSION_MAJOR=1
    -DAWS_SDK_VERSION_MINOR=10
    -DAWS_SDK_VERSION_PATCH=36
    -DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1
    -DBOOST_ASIO_STANDALONE=1
    -DBOOST_TIMER_ENABLE_DEPRECATED=1
    -DCARES_STATICLIB
    -DENABLE_MULTITARGET_CODE=1
    -DINCBIN_SILENCE_BITCODE_WARNING
    -DLZ4_DISABLE_DEPRECATE_WARNINGS=1
    -DLZ4_FAST_DEC_LOOP=1
    -DPOCO_ENABLE_CPP11
    -DPOCO_HAVE_FD_EPOLL
    -DPOCO_OS_FAMILY_UNIX
    -DUNALIGNED_OK
    -DWITH_COVERAGE=0
    -DWITH_GZFILEOP
    -DX86_64
    -DZLIB_COMPAT
    -DZOOKEEPER_LOG
    -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS
    -D__GCC_HAVE_DWARF2_CFI_ASM=1
)

SRCDIR(contrib/clickhouse)

SRCS(
    includes/configs/config_version.cpp
    src/Common/ActionLock.cpp
    src/Common/AlignedBuffer.cpp
    src/Common/Allocator.cpp
    src/Common/AsyncLoader.cpp
    src/Common/AsyncTaskExecutor.cpp
    src/Common/AsynchronousMetrics.cpp
    src/Common/Base58.cpp
    src/Common/Base64.cpp
    src/Common/CancelToken.cpp
    src/Common/CancelableSharedMutex.cpp
    src/Common/CaresPTRResolver.cpp
    src/Common/ClickHouseRevision.cpp
    src/Common/ConcurrencyControl.cpp
    src/Common/Config/AbstractConfigurationComparison.cpp
    src/Common/Config/ConfigHelper.cpp
    src/Common/Config/ConfigProcessor.cpp
    src/Common/Config/ConfigReloader.cpp
    src/Common/Config/YAMLParser.cpp
    src/Common/Config/configReadClient.cpp
    src/Common/CurrentMemoryTracker.cpp
    src/Common/CurrentMetrics.cpp
    src/Common/CurrentThread.cpp
    src/Common/DNSPTRResolverProvider.cpp
    src/Common/DNSResolver.cpp
    src/Common/DateLUT.cpp
    src/Common/DateLUTImpl.cpp
    src/Common/Dwarf.cpp
    src/Common/Elf.cpp
    src/Common/EnvironmentProxyConfigurationResolver.cpp
    src/Common/Epoll.cpp
    src/Common/ErrorCodes.cpp
    src/Common/EventFD.cpp
    src/Common/EventNotifier.cpp
    src/Common/Exception.cpp
    src/Common/FST.cpp
    src/Common/FailPoint.cpp
    src/Common/FieldVisitorDump.cpp
    src/Common/FieldVisitorHash.cpp
    src/Common/FieldVisitorSum.cpp
    src/Common/FieldVisitorToString.cpp
    src/Common/FieldVisitorWriteBinary.cpp
    src/Common/FileChecker.cpp
    src/Common/FileRenamer.cpp
    src/Common/FrequencyHolder.cpp
    src/Common/FunctionDocumentation.cpp
    src/Common/GetPriorityForLoadBalancing.cpp
    src/Common/HTTPHeaderFilter.cpp
    src/Common/IO.cpp
    src/Common/IPv6ToBinary.cpp
    src/Common/IntervalKind.cpp
    src/Common/JSONBuilder.cpp
    src/Common/KnownObjectNames.cpp
    src/Common/LockMemoryExceptionInThread.cpp
    src/Common/LoggingFormatStringHelpers.cpp
    src/Common/Macros.cpp
    src/Common/MemoryStatisticsOS.cpp
    src/Common/MemoryTracker.cpp
    src/Common/MemoryTrackerBlockerInThread.cpp
    src/Common/NamePrompter.cpp
    src/Common/NetlinkMetricsProvider.cpp
    src/Common/OpenSSLHelpers.cpp
    src/Common/OpenTelemetryTraceContext.cpp
    src/Common/OptimizedRegularExpression.cpp
    src/Common/OvercommitTracker.cpp
    src/Common/PODArray.cpp
    src/Common/PipeFDs.cpp
    src/Common/ProcfsMetricsProvider.cpp
    src/Common/ProfileEvents.cpp
    src/Common/ProfileEventsScope.cpp
    src/Common/ProgressIndication.cpp
    src/Common/ProxyConfigurationResolverProvider.cpp
    src/Common/ProxyListConfigurationResolver.cpp
    src/Common/QueryProfiler.cpp
    src/Common/RWLock.cpp
    src/Common/RemoteHostFilter.cpp
    src/Common/RemoteProxyConfigurationResolver.cpp
    src/Common/SensitiveDataMasker.cpp
    src/Common/SettingsChanges.cpp
    src/Common/SharedMutex.cpp
    src/Common/ShellCommand.cpp
    src/Common/ShellCommandSettings.cpp
    src/Common/StackTrace.cpp
    src/Common/StatusFile.cpp
    src/Common/StatusInfo.cpp
    src/Common/StringUtils/StringUtils.cpp
    src/Common/StudentTTest.cpp
    src/Common/SymbolIndex.cpp
    src/Common/SystemLogBase.cpp
    src/Common/TLDListsHolder.cpp
    src/Common/TargetSpecific.cpp
    src/Common/TerminalSize.cpp
    src/Common/ThreadFuzzer.cpp
    src/Common/ThreadPool.cpp
    src/Common/ThreadProfileEvents.cpp
    src/Common/ThreadStatus.cpp
    src/Common/Throttler.cpp
    src/Common/TimerDescriptor.cpp
    src/Common/TraceSender.cpp
    src/Common/TransactionID.cpp
    src/Common/UTF8Helpers.cpp
    src/Common/UnicodeBar.cpp
    src/Common/VersionNumber.cpp
    src/Common/WeakHash.cpp
    src/Common/XMLUtils.cpp
    src/Common/ZooKeeper/IKeeper.cpp
    src/Common/ZooKeeper/TestKeeper.cpp
    src/Common/ZooKeeper/ZooKeeper.cpp
    src/Common/ZooKeeper/ZooKeeperArgs.cpp
    src/Common/ZooKeeper/ZooKeeperCachingGetter.cpp
    src/Common/ZooKeeper/ZooKeeperCommon.cpp
    src/Common/ZooKeeper/ZooKeeperConstants.cpp
    src/Common/ZooKeeper/ZooKeeperIO.cpp
    src/Common/ZooKeeper/ZooKeeperImpl.cpp
    src/Common/ZooKeeper/ZooKeeperLock.cpp
    src/Common/ZooKeeper/ZooKeeperNodeCache.cpp
    src/Common/assertProcessUserMatchesDataOwner.cpp
    src/Common/atomicRename.cpp
    src/Common/checkSSLReturnCode.cpp
    src/Common/checkStackSize.cpp
    src/Common/clearPasswordFromCommandLine.cpp
    src/Common/clickhouse_malloc.cpp
    src/Common/createHardLink.cpp
    src/Common/escapeForFileName.cpp
    src/Common/filesystemHelpers.cpp
    src/Common/formatIPv6.cpp
    src/Common/formatReadable.cpp
    src/Common/getCurrentProcessFDCount.cpp
    src/Common/getExecutablePath.cpp
    src/Common/getHashOfLoadedBinary.cpp
    src/Common/getMappedArea.cpp
    src/Common/getMaxFileDescriptorCount.cpp
    src/Common/getMultipleKeysFromConfig.cpp
    src/Common/getNumberOfPhysicalCPUCores.cpp
    src/Common/getRandomASCIIString.cpp
    src/Common/hasLinuxCapability.cpp
    src/Common/isLocalAddress.cpp
    src/Common/isValidUTF8.cpp
    src/Common/likePatternToRegexp.cpp
    src/Common/makeSocketAddress.cpp
    src/Common/new_delete.cpp
    src/Common/parseAddress.cpp
    src/Common/parseGlobs.cpp
    src/Common/parseRemoteDescription.cpp
    src/Common/quoteString.cpp
    src/Common/randomSeed.cpp
    src/Common/register_objects.cpp
    src/Common/remapExecutable.cpp
    src/Common/setThreadName.cpp
    src/Common/thread_local_rng.cpp
    src/Common/waitForPid.cpp
    src/Coordination/KeeperFeatureFlags.cpp
    src/IO/AIO.cpp
    src/IO/Archives/LibArchiveReader.cpp
    src/IO/Archives/ZipArchiveReader.cpp
    src/IO/Archives/ZipArchiveWriter.cpp
    src/IO/Archives/createArchiveReader.cpp
    src/IO/Archives/createArchiveWriter.cpp
    src/IO/Archives/hasRegisteredArchiveFileExtension.cpp
    src/IO/AsyncReadCounters.cpp
    src/IO/AsynchronousReadBufferFromFile.cpp
    src/IO/AsynchronousReadBufferFromFileDescriptor.cpp
    src/IO/BoundedReadBuffer.cpp
    src/IO/BrotliReadBuffer.cpp
    src/IO/BrotliWriteBuffer.cpp
    src/IO/Bzip2ReadBuffer.cpp
    src/IO/Bzip2WriteBuffer.cpp
    src/IO/CascadeWriteBuffer.cpp
    src/IO/CompressionMethod.cpp
    src/IO/ConcatSeekableReadBuffer.cpp
    src/IO/ConnectionTimeouts.cpp
    src/IO/DoubleConverter.cpp
    src/IO/FileEncryptionCommon.cpp
    src/IO/ForkWriteBuffer.cpp
    src/IO/HTTPChunkedReadBuffer.cpp
    src/IO/HTTPCommon.cpp
    src/IO/HadoopSnappyReadBuffer.cpp
    src/IO/HashingWriteBuffer.cpp
    src/IO/LZMADeflatingWriteBuffer.cpp
    src/IO/LZMAInflatingReadBuffer.cpp
    src/IO/LimitReadBuffer.cpp
    src/IO/LimitSeekableReadBuffer.cpp
    src/IO/Lz4DeflatingWriteBuffer.cpp
    src/IO/Lz4InflatingReadBuffer.cpp
    src/IO/MMapReadBufferFromFile.cpp
    src/IO/MMapReadBufferFromFileDescriptor.cpp
    src/IO/MMapReadBufferFromFileWithCache.cpp
    src/IO/MMappedFile.cpp
    src/IO/MMappedFileDescriptor.cpp
    src/IO/MemoryReadWriteBuffer.cpp
    src/IO/MySQLBinlogEventReadBuffer.cpp
    src/IO/MySQLPacketPayloadReadBuffer.cpp
    src/IO/MySQLPacketPayloadWriteBuffer.cpp
    src/IO/NullWriteBuffer.cpp
    src/IO/OpenedFile.cpp
    src/IO/ParallelReadBuffer.cpp
    src/IO/PeekableReadBuffer.cpp
    src/IO/Progress.cpp
    src/IO/ReadBuffer.cpp
    src/IO/ReadBufferFromEncryptedFile.cpp
    src/IO/ReadBufferFromFile.cpp
    src/IO/ReadBufferFromFileBase.cpp
    src/IO/ReadBufferFromFileDecorator.cpp
    src/IO/ReadBufferFromFileDescriptor.cpp
    src/IO/ReadBufferFromIStream.cpp
    src/IO/ReadBufferFromMemory.cpp
    src/IO/ReadBufferFromPocoSocket.cpp
    src/IO/ReadBufferFromS3.cpp
    src/IO/ReadHelpers.cpp
    src/IO/ReadWriteBufferFromHTTP.cpp
    src/IO/Resource/ClassifiersConfig.cpp
    src/IO/Resource/DynamicResourceManager.cpp
    src/IO/Resource/FairPolicy.cpp
    src/IO/Resource/FifoQueue.cpp
    src/IO/Resource/PriorityPolicy.cpp
    src/IO/Resource/SemaphoreConstraint.cpp
    src/IO/Resource/StaticResourceManager.cpp
    src/IO/Resource/registerResourceManagers.cpp
    src/IO/Resource/registerSchedulerNodes.cpp
    src/IO/S3/AWSLogger.cpp
    src/IO/S3/Client.cpp
    src/IO/S3/Credentials.cpp
    src/IO/S3/PocoHTTPClient.cpp
    src/IO/S3/PocoHTTPClientFactory.cpp
    src/IO/S3/ProviderType.cpp
    src/IO/S3/Requests.cpp
    src/IO/S3/URI.cpp
    src/IO/S3/copyS3File.cpp
    src/IO/S3/getObjectInfo.cpp
    src/IO/S3Common.cpp
    src/IO/SeekableReadBuffer.cpp
    src/IO/SharedThreadPools.cpp
    src/IO/SnappyReadBuffer.cpp
    src/IO/SnappyWriteBuffer.cpp
    src/IO/StdIStreamFromMemory.cpp
    src/IO/StdStreamBufFromReadBuffer.cpp
    src/IO/SwapHelper.cpp
    src/IO/SynchronousReader.cpp
    src/IO/TimeoutSetter.cpp
    src/IO/UseSSL.cpp
    src/IO/VarInt.cpp
    src/IO/WithFileName.cpp
    src/IO/WithFileSize.cpp
    src/IO/WriteBuffer.cpp
    src/IO/WriteBufferFromEncryptedFile.cpp
    src/IO/WriteBufferFromFile.cpp
    src/IO/WriteBufferFromFileBase.cpp
    src/IO/WriteBufferFromFileDecorator.cpp
    src/IO/WriteBufferFromFileDescriptor.cpp
    src/IO/WriteBufferFromFileDescriptorDiscardOnFailure.cpp
    src/IO/WriteBufferFromHTTP.cpp
    src/IO/WriteBufferFromOStream.cpp
    src/IO/WriteBufferFromPocoSocket.cpp
    src/IO/WriteBufferFromS3.cpp
    src/IO/WriteBufferFromS3BufferAllocationPolicy.cpp
    src/IO/WriteBufferFromS3TaskTracker.cpp
    src/IO/WriteBufferValidUTF8.cpp
    src/IO/WriteHelpers.cpp
    src/IO/ZlibDeflatingWriteBuffer.cpp
    src/IO/ZlibInflatingReadBuffer.cpp
    src/IO/ZstdDeflatingAppendableWriteBuffer.cpp
    src/IO/ZstdDeflatingWriteBuffer.cpp
    src/IO/ZstdInflatingReadBuffer.cpp
    src/IO/copyData.cpp
    src/IO/parseDateTimeBestEffort.cpp
    src/IO/readFloatText.cpp
)

END()