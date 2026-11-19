#include "FileCollector.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// "ABC" -> {'A','B','C'}
Bytes B(const std::string& s)
{
    return Bytes(s.begin(), s.end());
}

// Содержимое собранного файла строкой
std::string Text(const FileData& f)
{
    return std::string(f.Span().begin(), f.Span().end());
}

FileCollectorError::Code ErrorOf(const std::function<void()>& action)
{
    try
    {
        action();
    }
    catch (const FileCollectorError& e)
    {
        return e.code();
    }
    ADD_FAILURE() << "expected FileCollectorError";
    return FileCollectorError::Code::UnknownFile;
}

} 

TEST(FileCollector, ExampleFromTask)
{
    FileCollector fc;
    fc.CollectFile(1, 10);
    fc.OnNewChunk(1, 0, B("ABCD"));
    fc.OnNewChunk(1, 2, B("CDEF"));
    EXPECT_FALSE(fc.GetFile(1));
    fc.OnNewChunk(1, 6, B("GHIJ"));

    FileData f = fc.GetFile(1);
    ASSERT_TRUE(f);
    EXPECT_EQ(Text(f), "ABCDEFGHIJ");
}

TEST(FileCollector, ReverseOrder)
{
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 4, B("EF"));
    fc.OnNewChunk(1, 2, B("CD"));
    fc.OnNewChunk(1, 0, B("AB"));
    EXPECT_EQ(Text(fc.GetFile(1)), "ABCDEF");
}

TEST(FileCollector, DuplicatesAndNestedChunks)
{
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 0, B("ABCDEF"));
    fc.OnNewChunk(1, 0, B("ABCDEF"));
    fc.OnNewChunk(1, 2, B("CD"));
    EXPECT_EQ(Text(fc.GetFile(1)), "ABCDEF");
}

TEST(FileCollector, ChunkCoveringSeveralGaps)
{
    FileCollector fc;
    fc.CollectFile(1, 7);
    fc.OnNewChunk(1, 1, B("B"));
    fc.OnNewChunk(1, 4, B("E"));
    fc.OnNewChunk(1, 0, B("ABCDEFG"));
    EXPECT_EQ(Text(fc.GetFile(1)), "ABCDEFG");
}

TEST(FileCollector, EmptyFileIsCompleteRightAway)
{
    FileCollector fc;
    fc.CollectFile(1, 0);
    FileData f = fc.GetFile(1);
    ASSERT_TRUE(f);
    EXPECT_EQ(f.size, 0u);
}

TEST(FileCollector, EmptyChunkIsIgnored)
{
    FileCollector fc;
    fc.CollectFile(1, 3);
    fc.OnNewChunk(1, 1, Bytes{});
    EXPECT_FALSE(fc.GetFile(1));
}

TEST(FileCollector, UnknownFile)
{
    FileCollector fc;
    using C = FileCollectorError::Code;
    EXPECT_EQ(ErrorOf([&] { fc.OnNewChunk(7, 0, B("A")); }), C::UnknownFile);
    EXPECT_EQ(ErrorOf([&] { fc.GetFile(7); }), C::UnknownFile);
    EXPECT_EQ(ErrorOf([&] { fc.RemoveFile(7); }), C::UnknownFile);
}

TEST(FileCollector, ChunkOutOfBounds)
{
    FileCollector fc;
    fc.CollectFile(1, 4);
    using C = FileCollectorError::Code;
    EXPECT_EQ(ErrorOf([&] { fc.OnNewChunk(1, 3, B("XY")); }), C::OutOfRange);
    EXPECT_EQ(ErrorOf([&] { fc.OnNewChunk(1, 5, Bytes{}); }), C::OutOfRange);

    const std::size_t huge = std::numeric_limits<std::size_t>::max();
    EXPECT_EQ(ErrorOf([&] { fc.OnNewChunk(1, huge, B("X")); }), C::OutOfRange);
}

TEST(FileCollector, ChunkAfterCompleteIsIgnoredButStillChecked)
{
    FileCollector fc;
    fc.CollectFile(1, 2);
    fc.OnNewChunk(1, 0, B("AB"));
    fc.OnNewChunk(1, 0, B("AB"));
    EXPECT_EQ(ErrorOf([&] { fc.OnNewChunk(1, 1, B("BC")); }), FileCollectorError::Code::OutOfRange);
    EXPECT_EQ(Text(fc.GetFile(1)), "AB");
}

TEST(FileCollector, CollectFileTwice)
{
    FileCollector fc;
    fc.CollectFile(1, 4);
    fc.OnNewChunk(1, 0, B("AB"));
    fc.CollectFile(1, 4);
    EXPECT_EQ(ErrorOf([&] { fc.CollectFile(1, 5); }), FileCollectorError::Code::SizeMismatch);
    fc.OnNewChunk(1, 2, B("CD"));
    EXPECT_EQ(Text(fc.GetFile(1)), "ABCD");
}

TEST(FileCollector, ReadReturnsOnlyWhatArrived)
{
    FileCollector fc;
    fc.CollectFile(1, 8);
    fc.OnNewChunk(1, 0, B("ABC"));
    fc.OnNewChunk(1, 5, B("FGH"));

    Bytes out(8, '.');
    EXPECT_EQ(fc.Read(1, 0, out), 3u);
    EXPECT_EQ(std::string(out.begin(), out.begin() + 3), "ABC");
    EXPECT_EQ(fc.Read(1, 3, out), 0u);
    EXPECT_EQ(fc.Read(1, 6, out), 2u);
    EXPECT_EQ(fc.Read(1, 8, out), 0u);
    EXPECT_EQ(ErrorOf([&] { fc.Read(1, 9, out); }), FileCollectorError::Code::OutOfRange);
}

TEST(FileCollector, ReadStopsAtBufferSize)
{
    FileCollector fc;
    fc.CollectFile(1, 6);
    fc.OnNewChunk(1, 0, B("ABCDEF"));
    Bytes out(2);
    EXPECT_EQ(fc.Read(1, 1, out), 2u);
    EXPECT_EQ(std::string(out.begin(), out.end()), "BC");
}

TEST(FileCollector, RemovedFileKeepsDataForThoseWhoHoldIt)
{
    FileCollector fc;
    fc.CollectFile(1, 3);
    fc.OnNewChunk(1, 0, B("ABC"));
    FileData f = fc.GetFile(1);
    fc.RemoveFile(1);
    EXPECT_EQ(ErrorOf([&] { fc.GetFile(1); }), FileCollectorError::Code::UnknownFile);
    EXPECT_EQ(Text(f), "ABC");
}

TEST(FileCollector, TooFragmentedFileIsRejected)
{
    FileCollector fc(/*maxRangesPerFile=*/3);
    fc.CollectFile(1, 100);
    fc.OnNewChunk(1, 0, B("A"));
    fc.OnNewChunk(1, 2, B("C"));
    fc.OnNewChunk(1, 4, B("E"));
    EXPECT_EQ(ErrorOf([&] { fc.OnNewChunk(1, 6, B("G")); }), FileCollectorError::Code::TooFragmented);
    // Заполнить дыру можно и на пределе: участки склеятся, их станет меньше
    EXPECT_NO_THROW(fc.OnNewChunk(1, 1, B("B")));
    EXPECT_NO_THROW(fc.OnNewChunk(1, 5, B("F")));
    Bytes out(10);
    EXPECT_EQ(fc.Read(1, 0, out), 3u);
}