#include "stdafx.h"

#include <ytlib/meta_state/change_log.h>
#include <ytlib/profiling/single_timer.h>

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

    template<class RecordType=ui32>
    TChangeLogPtr CreateChangeLog(size_t recordsCount) const
    {
        TChangeLogPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);
        yvector<TSharedRef> records = MakeRecords<RecordType>(0, recordsCount);
        changeLog->Append(0, records);
        changeLog->Flush();
        return changeLog;
    }

    template<class RecordType=ui32>
    yvector<TSharedRef> MakeRecords(i32 from, i32 to) const
    {
        yvector<TSharedRef> records(to - from);
        for (i32 recordId = from; recordId < to; ++recordId) {
            TBlob blob(sizeof(RecordType));
            *reinterpret_cast<RecordType*>(blob.begin()) = static_cast<RecordType>(recordId);
            records[recordId - from] = MoveRV(blob);
        }
        return records;
    }

    TChangeLogPtr OpenChangeLog() const
    {
        TChangeLogPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();
        return changeLog;
    }

    template <class T>
    static void CheckRecord(const T& data, const TRef& record)
    {
        EXPECT_EQ(record.Size(), sizeof(data));
        EXPECT_EQ(*(reinterpret_cast<const T*>(record.Begin())), data);
    }

    template <class T>
    static void CheckRead(
        TChangeLogPtr changeLog,
        i32 firstRecordId,
        i32 recordCount,
        i32 logRecordCount)
    {
        yvector<TSharedRef> records;
        changeLog->Read(firstRecordId, recordCount, &records);

        i32 expectedRecordCount =
            firstRecordId >= logRecordCount ?
            0 : Min(recordCount, logRecordCount - firstRecordId);

        EXPECT_EQ(records.size(), expectedRecordCount);
        for (i32 i = 0; i < records.size(); ++i) {
            CheckRecord<T>(static_cast<T>(firstRecordId + i), records[i]);
        }
    }

    template <class T>
    static void CheckReads(TChangeLogPtr changeLog, i32 logRecordCount)
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
        TChangeLogPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Create(0);
    });

    ASSERT_NO_THROW({
        TChangeLogPtr changeLog = New<TChangeLog>(TemporaryFile->Name(), 0, 64);
        changeLog->Open();
    });
}


TEST_F(TChangeLogTest, Finalized)
{
    const int logRecordCount = 256;
    {
        TChangeLogPtr changeLog = CreateChangeLog(logRecordCount);
        EXPECT_EQ(changeLog->IsFinalized(), false);
        changeLog->Finalize();
        EXPECT_EQ(changeLog->IsFinalized(), true);
    }
    {
        TChangeLogPtr changeLog = OpenChangeLog();
        EXPECT_EQ(changeLog->IsFinalized(), true);
    }
}


TEST_F(TChangeLogTest, ReadWrite)
{
    const int logRecordCount = 16;
    {
        TChangeLogPtr changeLog = CreateChangeLog(logRecordCount);
        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckReads<ui32>(changeLog, logRecordCount);
    }
    {
        TChangeLogPtr changeLog = OpenChangeLog();
        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckReads<ui32>(changeLog, logRecordCount);
    }
}

TEST_F(TChangeLogTest, TestCorrupted)
{
    const int logRecordCount = 1024;
    {
        TChangeLogPtr changeLog = CreateChangeLog(logRecordCount);
    }
    {
        // Truncate file.
        TFile changeLogFile(TemporaryFile->Name(), RdWr);
        changeLogFile.Resize(changeLogFile.GetLength() - 1);
    }

    {
        TChangeLogPtr changeLog = OpenChangeLog();

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
        TChangeLogPtr changeLog = OpenChangeLog();
        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }
}

TEST_F(TChangeLogTest, Truncate)
{
    const int logRecordCount = 256;

    {
        TChangeLogPtr changeLog = CreateChangeLog(logRecordCount);
        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }

    for (int recordId = logRecordCount; recordId >= 0; --recordId) {
        {
            TChangeLogPtr changeLog = OpenChangeLog();
            changeLog->Truncate(recordId);
        }
        {
            TChangeLogPtr changeLog = OpenChangeLog();
            EXPECT_EQ(changeLog->GetRecordCount(), recordId);
            CheckRead<ui32>(changeLog, 0, recordId, recordId);
        }
    }
}

TEST_F(TChangeLogTest, TruncateAppend)
{
    const int logRecordCount = 256;

    {
        TChangeLogPtr changeLog = CreateChangeLog(logRecordCount);
        EXPECT_EQ(changeLog->GetRecordCount(), logRecordCount);
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }

    int truncatedRecordId = logRecordCount / 2;
    {
        // Truncate
        TChangeLogPtr changeLog = OpenChangeLog();
        changeLog->Truncate(truncatedRecordId);
        CheckRead<ui32>(changeLog, 0, truncatedRecordId, truncatedRecordId);
    }
    {
        // Append
        TChangeLogPtr changeLog = OpenChangeLog();
        changeLog->Append(truncatedRecordId, MakeRecords(truncatedRecordId, logRecordCount));
    }
    {
        // Check
        TChangeLogPtr changeLog = OpenChangeLog();
        CheckRead<ui32>(changeLog, 0, logRecordCount, logRecordCount);
    }
}

TEST_F(TChangeLogTest, UnalighnedChecksum)
{
    const int logRecordCount = 256;

    {
        TChangeLogPtr changeLog = CreateChangeLog<ui8>(logRecordCount);
    }
    {
        TChangeLogPtr changeLog = OpenChangeLog();
        CheckRead<ui8>(changeLog, 0, logRecordCount, logRecordCount);
    }
}

TEST_F(TChangeLogTest, Profiling)
{
    int recordsCount = 10000000;
    {
        NProfiling::TSingleTimer timer;
        TChangeLogPtr changeLog = CreateChangeLog<ui32>(recordsCount);
        std::cerr << "Make changelog of size " << recordsCount <<
            ", time " << timer.ElapsedTimeAsString() << std::endl;
    }

    {
        NProfiling::TSingleTimer timer;
        TChangeLogPtr changeLog = OpenChangeLog();
        std::cerr << "Open changelog of size " << recordsCount <<
            ", time " << timer.ElapsedTimeAsString() << std::endl;
    }
    {
        TChangeLogPtr changeLog = OpenChangeLog();
        NProfiling::TSingleTimer timer;
        yvector<TSharedRef> records;
        changeLog->Read(0, recordsCount, &records);
        std::cerr << "Read full changelog of size " << recordsCount <<
            ", time " << timer.ElapsedTimeAsString() << std::endl;

        timer.Restart();
        changeLog->Truncate(recordsCount / 2);
        std::cerr << "Truncating changelog of size " << recordsCount <<
            ", time " << timer.ElapsedTimeAsString() << std::endl;

        timer.Restart();
        changeLog->Finalize();
        std::cerr << "Finalizing changelog of size " << recordsCount / 2 <<
            ", time " << timer.ElapsedTimeAsString() << std::endl;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
