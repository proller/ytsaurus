#pragma once

#include "private.h"

#include "cluster_tracker.h"
#include "storage_with_virtual_columns.h"
#include "table_schema.h"

#include "table_partition.h"

#include <Interpreters/Cluster.h>
#include <Interpreters/Context.h>

namespace NYT::NClickHouseServer {

////////////////////////////////////////////////////////////////////////////////

struct TTablePartAllocation
{
    TTablePart TablePart;
    IClusterNodePtr TargetClusterNode;

    TTablePartAllocation(
        TTablePart part,
        IClusterNodePtr node)
        : TablePart(std::move(part))
        , TargetClusterNode(std::move(node))
    {
    }
};

using TTableAllocation = std::vector<TTablePartAllocation>;

////////////////////////////////////////////////////////////////////////////////

// Abstract base class for distributed storages

class TStorageDistributed
    : public IStorageWithVirtualColumns
{
private:
    const IExecutionClusterPtr Cluster;
    TClickHouseTableSchema Schema;

public:
    TStorageDistributed(
        IExecutionClusterPtr cluster,
        TClickHouseTableSchema schema)
        : Cluster(std::move(cluster))
        , Schema(std::move(schema))
    { }

    virtual void startup() override
    {
        if (Schema.Columns.empty()) {
            THROW_ERROR_EXCEPTION("CHYT does not support tables without schema")
                << TErrorAttribute("path", getTableName());
        }
        setColumns(DB::ColumnsDescription(ListPhysicalColumns()));
    }

    // Database name
    std::string getName() const override
    {
        return "YTStaticTable";
    }

    bool isRemote() const override
    {
        return true;
    }

    virtual bool supportsIndexForIn() const override
    {
        return Schema.HasPrimaryKey();
    }

    virtual bool mayBenefitFromIndexForIn(const DB::ASTPtr & /* left_in_operand */, const DB::Context & /* query_context */) const override
    {
        return supportsIndexForIn();
    }

    DB::QueryProcessingStage::Enum getQueryProcessingStage(const DB::Context& context) const override;

    DB::BlockInputStreams read(
        const DB::Names& columnNames,
        const DB::SelectQueryInfo& queryInfo,
        const DB::Context& context,
        DB::QueryProcessingStage::Enum processedStage,
        size_t maxBlockSize,
        unsigned numStreams) override;

protected:
    virtual TTablePartList GetTableParts(
        const DB::ASTPtr& queryAst,
        const DB::Context& context,
        const DB::KeyCondition* keyCondition,
        const size_t maxParts) = 0;

    virtual DB::ASTPtr RewriteSelectQueryForTablePart(
        const DB::ASTPtr& queryAst,
        const std::string& jobSpec) = 0;

    const TClickHouseTableSchema& GetSchema() const
    {
        return Schema;
    }

private:
    const DB::NamesAndTypesList& ListPhysicalColumns() const override
    {
        return Schema.Columns;
    }

    TTableAllocation AllocateTablePartsToClusterNodes(
        const TClusterNodes& clusterNodes,
        const DB::SelectQueryInfo& queryInfo,
        const DB::Context& context);

    static DB::Settings PrepareLeafJobSettings(const DB::Settings& settings);

    static DB::ThrottlerPtr CreateNetThrottler(const DB::Settings& settings);

    static DB::BlockInputStreamPtr CreateLocalStream(
        const DB::ASTPtr& queryAst,
        const DB::Context& context,
        DB::QueryProcessingStage::Enum processedStage);

    static DB::BlockInputStreamPtr CreateRemoteStream(
        const IClusterNodePtr remoteNode,
        const DB::ASTPtr& queryAst,
        const DB::Context& context,
        const DB::ThrottlerPtr& throttler,
        const DB::Tables& externalTables,
        DB::QueryProcessingStage::Enum processedStage);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
