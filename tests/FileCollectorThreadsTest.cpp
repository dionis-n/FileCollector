#include "FileCollector.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <latch>
#include <random>
#include <thread>
#include <vector>

namespace
{

using Bytes = std::vector<std::uint8_t>;

struct Chunk
{
    size_t pos;
    Bytes data;
};

// Содержимое файла, по которому легко проверить любой байт.
std::uint8_t ByteAt(std::uint32_t fileId, size_t i)
{
    return static_cast<std::uint8_t>(i * 7 + fileId * 13 + 3);
}

// нарезать файл на куски случайной длины, добавить случайные пересечения и дубликаты, перемешать
std::vector<Chunk> MakeChunks(std::uint32_t fileId, size_t size, std::mt19937& rng)
 {
    std::vector<Chunk> chunks;
    auto make = [&](size_t pos, size_t len)
    {
        Chunk c{pos, Bytes(len)};
        for (size_t i = 0; i < len; ++i) 
            c.data[i] = ByteAt(fileId, pos + i);
        
        chunks.push_back(std::move(c));
    };
    for (size_t pos = 0; pos < size;)
    {
        size_t len = std::min<size_t>(1 + rng() % 4096, size - pos);
        make(pos, len);
        pos += len;
    }
    for (size_t n = chunks.size() / 3; n > 0; --n)
    {  
        size_t pos = rng() % size;
        make(pos, std::min<size_t>(1 + rng() % 4096, size - pos));
    }
    std::shuffle(chunks.begin(), chunks.end(), rng);
    return chunks;
}

// Запустить threads потоков одновременно: latch держит всех, пока не стартует последний
template <class Work>
void RunTogether(unsigned threads, Work work)
{
    std::latch start(threads);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
    {
        pool.emplace_back([&, t] 
        {
            start.arrive_and_wait();
            work(t);
        });
    }
    for (auto& th : pool) 
        th.join();
    
}

void ExpectFileIsCorrect(const FileCollector& fc, std::uint32_t fileId, size_t size) {
    FileData f = fc.GetFile(fileId);
    ASSERT_TRUE(f) << "file " << fileId << " is not complete";
    ASSERT_EQ(f.size, size);
    for (size_t i = 0; i < size; ++i) 
        ASSERT_EQ(f.bytes[i], ByteAt(fileId, i)) << "file " << fileId << ", byte " << i;
    
}

}  // namespace




// Главный сценарий задачи: много потоков собирают 1 файл из пересекающихся кусков.
TEST(FileCollectorThreads, ManyThreadsOneFile)
{
    const unsigned threads = 8;
    const size_t size = 1 << 20;
    std::mt19937 rng(1);
    for (int round = 0; round < 3; ++round)
    {
        FileCollector fc;
        fc.CollectFile(1, size);
        auto chunks = MakeChunks(1, size, rng);
        RunTogether(threads, [&](unsigned t)
        {
            for (size_t i = t; i < chunks.size(); i += threads)
                fc.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data)); 
        });
        ExpectFileIsCorrect(fc, 1, size);
    }
}

TEST(FileCollectorThreads, ManyFilesInParallel)
{
    const unsigned threads = 8;
    const std::uint32_t files = 16;
    const size_t size = 64 << 10;
    std::mt19937 rng(2);
    FileCollector fc;
    std::vector<std::pair<std::uint32_t, Chunk>> all;
    for (std::uint32_t id = 0; id < files; ++id)
    {
        fc.CollectFile(id, size);
        for (auto& c : MakeChunks(id, size, rng)) 
            all.emplace_back(id, std::move(c));
    }
    std::shuffle(all.begin(), all.end(), rng);  // куски разных файлов вперемешку
    RunTogether(threads, [&](unsigned t)
    {
        for (size_t i = t; i < all.size(); i += threads)
        {
            fc.OnNewChunk(all[i].first, all[i].second.pos, std::move(all[i].second.data));
        }
    });

    for (std::uint32_t id = 0; id < files; ++id)
    {
        ExpectFileIsCorrect(fc, id, size);
    }
}

/*Читатель работает, пока писатели пишут: каждый прочитанный байт обязан быть верным,
 а GetFile не имеет права отдать файл раньше времени.*/
TEST(FileCollectorThreads, ReadWhileWriting)
{
    const unsigned writers = 4;
    const size_t size = 1 << 20;
    std::mt19937 rng(3);
    FileCollector fc;
    fc.CollectFile(1, size);
    auto chunks = MakeChunks(1, size, rng);
    std::atomic<int> badBytes{0};
    std::atomic<bool> partialFile{false};

    RunTogether(writers + 1, [&](unsigned t)
    {
        if (t < writers)
        {
            for (size_t i = t; i < chunks.size(); i += writers) 
                fc.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
            return;
        }
        std::mt19937 local(4);
        Bytes out(8192);
        while (true)
        {  // поток-читатель
            FileData f = fc.GetFile(1);
            if (f)
            {
                for (size_t i = 0; i < size; ++i)
                {
                    if (f.bytes[i] != ByteAt(1, i))
                    {
                        partialFile = true;
                        break;
                    }
                }
                return;
            }
            size_t pos = local() % size;
            size_t n = fc.Read(1, pos, out);
            for (size_t i = 0; i < n; ++i)
            {
                if (out[i] != ByteAt(1, pos + i))
                    ++badBytes;
            }
        }
    });
    EXPECT_EQ(badBytes.load(), 0);
    EXPECT_FALSE(partialFile.load());
}

TEST(FileCollectorThreads, ConcurrentRegistration)
{
    const unsigned threads = 8;
    {  // все регистрируют один файл одного размера -все успешно, файл один
        FileCollector fc;
        RunTogether(threads, [&](unsigned) { fc.CollectFile(1, 100); });
        fc.OnNewChunk(1, 0, Bytes(100, 1));
        EXPECT_TRUE(fc.GetFile(1));
    }
    {  // каждый со своим размером - выигрывает ровно один, остальные получают SizeMismatch.
        FileCollector fc;
        std::atomic<int> mismatches{0};
        RunTogether(threads, [&](unsigned t)
        {
            try
            {
                fc.CollectFile(1, 100 + t);
            }
            catch (const FileCollectorError& e)
            {
                if (e.code() == FileCollectorError::Code::SizeMismatch)
                    ++mismatches;
            }
        });
        EXPECT_EQ(mismatches.load(), static_cast<int>(threads) - 1);
    }
}

/* Файл удаляют, пока в него пишут: писатели либо успевают, либо получают UnknownFile,
 но никто не обращается к освобождённой памяти (это проверяет ThreadSanitizer).*/
TEST(FileCollectorThreads, RemoveWhileWriting)
{
    const unsigned writers = 4;
    const std::size_t size = 256 << 10;
    std::mt19937 rng(5);
    FileCollector fc;
    fc.CollectFile(1, size);
    auto chunks = MakeChunks(1, size, rng);
    RunTogether(writers + 1, [&](unsigned t)
    {
        if (t == writers)
        {
            fc.RemoveFile(1);
            return;
        }
        for (size_t i = t; i < chunks.size(); i += writers)
        {
            try
            {
                fc.OnNewChunk(1, chunks[i].pos, std::move(chunks[i].data));
            }
            catch (const FileCollectorError& e)
            {
                EXPECT_EQ(e.code(), FileCollectorError::Code::UnknownFile);
                return;
            }
        }
    });
}