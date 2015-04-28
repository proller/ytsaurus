#include "stdafx.h"

#include "helpers.h"

#include "schemaless_reader.h"
#include "schemaless_writer.h"
#include "schemaless_chunk_reader.h"

#include <ytlib/formats/parser.h>

#include <core/concurrency/scheduler.h>


namespace NYT {
namespace NVersionedTableClient {

using namespace NConcurrency;
using namespace NFormats;

//////////////////////////////////////////////////////////////////////////////////

TTableOutput::TTableOutput(const TFormat& format, NYson::IYsonConsumer* consumer)
    : Parser_(CreateParserForFormat(format, EDataType::Tabular, consumer))
{ }

TTableOutput::~TTableOutput() throw()
{ }

void TTableOutput::DoWrite(const void* buf, size_t len)
{
    YCHECK(IsParserValid_);
    try {
        Parser_->Read(TStringBuf(static_cast<const char*>(buf), len));
    } catch (const std::exception& ex) {
        IsParserValid_ = false;
        throw;
    }
}

void TTableOutput::DoFinish()
{
    if (IsParserValid_) {
        // Dump everything into consumer.
        Parser_->Finish();
    }
}

//////////////////////////////////////////////////////////////////////////////////

void PipeReaderToWriter(
    ISchemalessReaderPtr reader,
    ISchemalessWriterPtr writer,
    int bufferRowCount,
    bool validateValues)
{
    std::vector<TUnversionedRow> rows;
    rows.reserve(bufferRowCount);

    while (reader->Read(&rows)) {
        if (rows.empty()) {
            WaitFor(reader->GetReadyEvent())
                .ThrowOnError();
            continue;
        }

        if (validateValues) {
            for (const auto& row : rows) {
                for (int i = 0; i < row.GetCount(); ++i) {
                    ValidateDataValue(row[i]);
                }
            }
        }

        if (!writer->Write(rows)) {
            WaitFor(writer->GetReadyEvent())
                .ThrowOnError();
        }
    }

    WaitFor(writer->Close())
        .ThrowOnError();

    YCHECK(rows.empty());
}

void PipeReaderToWriter(
    ISchemalessMultiChunkReaderPtr reader,
    NFormats::ISchemalessFormatWriterPtr writer,
    int bufferRowCount,
    bool validateValues)
{
    std::vector<TUnversionedRow> rows;
    rows.reserve(bufferRowCount);

    while (reader->Read(&rows)) {
        if (rows.empty()) {
            WaitFor(reader->GetReadyEvent())
                .ThrowOnError();
            continue;
        }

        if (validateValues) {
            for (const auto& row : rows) {
                for (int i = 0; i < row.GetCount(); ++i) {
                    ValidateDataValue(row[i]);
                }
            }
        }

        writer->SetTableIndex(reader->GetTableIndex());

        if (!writer->Write(rows)) {
            WaitFor(writer->GetReadyEvent())
                .ThrowOnError();
        }
    }

    WaitFor(writer->Close())
        .ThrowOnError();

    YCHECK(rows.empty());
}

void PipeInputToOutput(
    TInputStream* input,
    TOutputStream* output,
    i64 bufferBlockSize)
{
    struct TWriteBufferTag { };
    TBlob buffer(TWriteBufferTag(), bufferBlockSize);

    while (true) {
        size_t length = input->Read(buffer.Begin(), buffer.Size());
        if (length == 0)
            break;

        output->Write(buffer.Begin(), length);
    }

    output->Finish();
}

//////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
} // namespace NVersionedTableClient
