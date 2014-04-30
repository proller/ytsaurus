﻿#pragma once

#include "public.h"

#include <core/misc/enum.h>
#include <core/misc/ref.h>
#include <core/misc/small_vector.h>

#include <ytlib/new_table_client/public.h>

#include <ytlib/api/public.h>

namespace NYT {
namespace NTabletClient {

///////////////////////////////////////////////////////////////////////////////

DECLARE_ENUM(EWireProtocolCommand,
    // Sentinels:

    ((End)(0))


    // Read commands:
    
    ((LookupRows)(100))
    // Finds rows with given keys and fetches their components.
    //
    // Input:
    //   * Column filter
    //   * Unversioned rowset containing keys
    //
    // Output:
    //   * Unversioned rowset containing rows (whose size matches the number of requested keys)


    // Write commands:

    ((WriteRow)(200))
    // Inserts a new row or completely replaces an existing one with matching key.
    //
    // Input:
    //   * Unversioned row
    // Output:
    //   None

    ((DeleteRow)(201))
    // Deletes a row with a given key, if it exists.
    //
    // Input:
    //   * Key
    // Output:
    //   None


    // Rowset commands:
    ((RowsetChunk)(300))
    ((EndOfRowset)(301))

);

////////////////////////////////////////////////////////////////////////////////

class TWireProtocolWriter
{
public:
    TWireProtocolWriter();
    ~TWireProtocolWriter();

    void WriteCommand(EWireProtocolCommand command);

    void WriteColumnFilter(const NVersionedTableClient::TColumnFilter& filter);

    void WriteTableSchema(const NVersionedTableClient::TTableSchema& schema);

    void WriteMessage(const ::google::protobuf::MessageLite& message);

    typedef SmallVector<int, NVersionedTableClient::TypicalColumnCount> TColumnIdMapping;

    void WriteUnversionedRow(
        NVersionedTableClient::TUnversionedRow row,
        const TColumnIdMapping* idMapping = nullptr);
    void WriteUnversionedRow(
        const std::vector<NVersionedTableClient::TUnversionedValue>& row,
        const TColumnIdMapping* idMapping = nullptr);
    void WriteUnversionedRowset(
        const std::vector<NVersionedTableClient::TUnversionedRow>& rowset,
        const TColumnIdMapping* idMapping = nullptr);
    NVersionedTableClient::ISchemafulWriterPtr CreateSchemafulRowsetWriter();

    Stroka GetData();

private:
    class TImpl;
    class TSchemafulRowsetWriter;

    std::unique_ptr<TImpl> Impl_;

};

///////////////////////////////////////////////////////////////////////////////

class TWireProtocolReader
{
public:
    explicit TWireProtocolReader(const Stroka& data); 
    ~TWireProtocolReader();

    EWireProtocolCommand ReadCommand();
    
    NVersionedTableClient::TColumnFilter ReadColumnFilter();

    NVersionedTableClient::TTableSchema ReadTableSchema();

    void ReadMessage(::google::protobuf::MessageLite* message);

    NVersionedTableClient::TUnversionedRow ReadUnversionedRow();
    void ReadUnversionedRowset(std::vector<NVersionedTableClient::TUnversionedRow>* rowset);
    NVersionedTableClient::ISchemafulReaderPtr CreateSchemafulRowsetReader();

private:
    class TImpl;
    class TSchemafulRowsetReader;

    std::unique_ptr<TImpl> Impl_;

};

///////////////////////////////////////////////////////////////////////////////

} // namespace NTabletClient
} // namespace NYT

