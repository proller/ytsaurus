#include "stdafx.h"

#include <ytlib/meta_state/change_log.h>

#include <util/random/random.h>
#include <util/system/tempfile.h>

#include <contrib/testing/framework.h>

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

class TChangeLogTest
    : public ::testing::Test
{
protected:
    THolder<TTempFile> TemporaryFile;
    THolder<TTempFile> TemporaryIndexFile;

    virtual void SetUp()
    {
        TemporaryFile.Reset(new TTempFile(GenerateRandomFileName("ChangeLog")));
        TemporaryIndexFile.Reset(new TTempFile(TemporaryFile->Name() + ".index"));
    }

    virtual void TearDown()
    {
        TemporaryFile.Reset(0);
        TemporaryIndexFile.Reset(0);
    }

    template<class T>
    static void CheckRecord(T data, TRef record)
    {
        EXPECT_EQ(record.Size(), sizeof(data));
        EXPECT_EQ(*(reinterpret_cast<T*>(record.Begin())), data);
    }

    template<class T>
    static void CheckRead(
        TChangeLog::TPtr changeLog,
        i32 firstRecordId,
        i32 recordCount,
        i32 logRecordCount)
    {
        yvector<TSharedRef> records;
        changeLog->Read(firstRecordId, recordCount, &records);

        i32 expectedRecordCount =
            firstRecordId >= logRecordCount ?
            0 : Min(recordCount, logRecordCount - firstRecordId);

        EXPECT_EQ(records.ysize(), expectedRecordCount);
        for (i32 i = 0; i < records.ysize(); ++i) {
            CheckRecord<T>(static_cast<T>(firstRecordId + i), records[i]);
        }
    }

    template<class T>
    static void CheckReads(TChangeLog::TPtr changeLog, i32 logRecordCount)
    {
        for (i32 start = 0; start <= logRecordCount; ++start) {
            for (i32 end = start; end <= 2 * logRecordCount + 1; ++end) {
                CheckRead<T>(changeLog, start, end - start, logRecordCount);
            }
        }
    }
};

TEST_F(TChangeLogTest, EmptyChangeLog)
{
    ASSERT_NO_THROW({
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);
    });

    ASSERT_NO_THROW({
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();
    });
}


TEST_F(TChangeLogTest, Finalized)
{
    const int logRecordCount = 256;

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);

        yvector<TSharedRef> records(logRecordCount);
        for (i32 recordId = 0; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui32));
            *reinterpret_cast<ui32*>(blob.begin()) = static_cast<ui32>(recordId);
            records[recordId] = MoveRV(blob);
        }
        changeLog->Append(0, records);

        changeLog->Flush();
        EXPECT_EQ(changeLog->IsFinalized(), false);

        changeLog->Finalize();
        EXPECT_EQ(changeLog->IsFinalized(), true);
    }

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        EXPECT_EQ(changeLog->IsFinalized(), true);
    }
}


TEST_F(TChangeLogTest, ReadWrite)
{
    const int logRecordCount = 16;

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);

        yvector<TSharedRef> records(logRecordCount);
        for (i32 recordId = 0; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui32));
            *reinterpret_cast<ui32*>(blob.begin()) = static_cast<ui32>(recordId);
            records[recordId] = MoveRV(blob);
        }
        changeLog->Append(0, records);

        changeLog->Flush();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckReads<ui32>(changeLog, logRecordCount);
    }

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckReads<ui32>(changeLog, logRecordCount);
    }
}

TEST_F(TChangeLogTest, TestCorrupted)
{
    const int logRecordCount = 1024;

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);

        yvector<TSharedRef> records(logRecordCount);
        for (i32 recordId = 0; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui32));
            *reinterpret_cast<ui32*>(blob.begin()) = static_cast<ui32>(recordId);
            records[recordId] = MoveRV(blob);
        }
        changeLog->Append(0, records);

        changeLog->Flush();
    }

    {
        // Truncate file.
        TFile changeLogFile(TemporaryFile->Name(), RdWr);
        changeLogFile.Resize(changeLogFile.GetLength() - 1);
    }

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount - 1);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount - 1);

        TBlob blob(sizeof(i32));
        *reinterpret_cast<i32*>(blob.begin()) = static_cast<i32>(logRecordCount - 1);
        yvector<TSharedRef> records;
        records.push_back(MoveRV(blob));
        changeLog->Append(logRecordCount - 1, records);
        changeLog->Flush();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }
}

TEST_F(TChangeLogTest, Truncate)
{
    const int logRecordCount = 256;

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);

        yvector<TSharedRef> records(logRecordCount);
        for (i32 recordId = 0; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui32));
            *reinterpret_cast<ui32*>(blob.begin()) = static_cast<ui32>(recordId);
            records[recordId] = MoveRV(blob);
        }
        changeLog->Append(0, records);

        changeLog->Flush();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }

    for (int recordId = logRecordCount; recordId >= 0; --recordId) {
        {
            TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
            changeLog->Open();
            changeLog->Truncate(recordId);
        }
        {
            TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
            changeLog->Open();

            EXPECT_EQ(changeLog->GetRecordCount(), recordId);
            CheckRead<ui32>(changeLog, 0, recordId, recordId);
        }
    }
}

TEST_F(TChangeLogTest, TruncateAppend)
{
    const int logRecordCount = 256;

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);

        yvector<TSharedRef> records(logRecordCount);
        for (i32 recordId = 0; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui32));
            *reinterpret_cast<ui32*>(blob.begin()) = static_cast<ui32>(recordId);
            records[recordId] = MoveRV(blob);
        }
        changeLog->Append(0, records);

        changeLog->Flush();

        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }

    int truncatedRecordId = logRecordCount / 2;
    {
        // Truncate
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();
        changeLog->Truncate(truncatedRecordId);

        CheckRead<ui32>(changeLog, 0, truncatedRecordId, truncatedRecordId);
    }
    {
        // Append
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        yvector<TSharedRef> records;
        for (i32 recordId = truncatedRecordId; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui32));
            *reinterpret_cast<ui32*>(blob.begin()) = static_cast<ui32>(recordId);
            records.push_back(MoveRV(blob));
        }
        changeLog->Append(truncatedRecordId, records);
    }
    {
        // Check
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }
}

TEST_F(TChangeLogTest, UnalighnedChecksum)
{
    const int logRecordCount = 256;

    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);

        yvector<TSharedRef> records(logRecordCount);
        for (i32 recordId = 0; recordId < logRecordCount; ++recordId) {
            TBlob blob(sizeof(ui8));
            *reinterpret_cast<ui8*>(blob.begin()) = static_cast<ui8>(recordId);
            records[recordId] = MoveRV(blob);
        }
        changeLog->Append(0, records);
    }
    {
        TChangeLog::TPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();

        CheckRead<ui8>(changeLog, 0, logRecordCount, logRecordCount);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
