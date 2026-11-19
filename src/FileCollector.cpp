#include "FileCollector.h"

#include "IntervalSet.h"

#include <algorithm>
#include <atomic>

using Code = FileCollectorError::Code;

// FileCollectorError
FileCollectorError::FileCollectorError(Code code, const char* what)
    : std::runtime_error(what), code_(code) {}

FileCollectorError::Code FileCollectorError::code() const noexcept
{
    return code_;
}



//FileData
FileData::operator bool() const noexcept
{
    return bytes != nullptr;
}

std::span<const std::uint8_t> FileData::Span() const noexcept
{
    return {bytes.get(), size};
}



//FileCollector

// FileState
struct FileCollector::FileState
{
    explicit FileState(size_t fileSize)
        : size(fileSize),
          bytes(new std::uint8_t[fileSize]),
          complete(fileSize == 0) {}   // пустой файл собран сразу

    const size_t size;
    const std::shared_ptr<std::uint8_t[]> bytes;

    std::mutex mutex;        // охраняет done и busy (но не сами байты)
    IntervalSet done;         // эти байты записаны и больше никогда не меняются
    std::vector<Range> busy;   // эти байты сейчас копирует какой-то поток

    std::atomic<bool> complete;
};

namespace // анонимный namespace - функция видна только внутри этого файла
{

// Убрать из gaps всё, что пересекается с busy: эти байты уже копирует другой поток.
std::vector<Range> SubtractBusy(std::vector<Range> gaps, const std::vector<Range>& busy)
{
    for (const Range& b : busy)
    {
        std::vector<Range> left;
        for (const Range& g : gaps)
        {
            if (g.end <= b.begin || b.end <= g.begin)
            {
                left.push_back(g);  // не пересекаются
                continue;
            }
            if (g.begin < b.begin)
            {
                left.push_back({g.begin, b.begin});  // кусок слева от занятого
            }
            if (b.end < g.end)
            {
                left.push_back({b.end, g.end});  // кусок справа от занятого
            }
        }
        gaps = std::move(left);
    }
    return gaps;
}

}  




FileCollector::FileCollector(std::size_t maxRangesPerFile) : maxRanges_(maxRangesPerFile) {}

std::shared_ptr<FileCollector::FileState> FileCollector::Find(std::uint32_t fileId) const
{
    std::lock_guard lock(filesMutex_);
    auto it = files_.find(fileId);
    if (it == files_.end())
        throw FileCollectorError(Code::UnknownFile, "unknown fileId");

    return it->second; 
}

void FileCollector::CollectFile(std::uint32_t fileId, std::size_t fileSize)
{
    auto checkSize = [fileSize](const FileState& existing)
    {
        if (existing.size != fileSize)
        {
            throw FileCollectorError(Code::SizeMismatch, "fileId is already registered with another size");
        }
    };

    {  // замок: такой файл уже есть?
        std::lock_guard lock(filesMutex_);
        if (auto it = files_.find(fileId); it != files_.end())
        {
            checkSize(*it->second);
            return;
        }
    }

    auto state = std::make_shared<FileState>(fileSize);

    //замок: вставляем. Пока мы выделяли память, другой поток мог успеть первым.
    std::lock_guard lock(filesMutex_);
    auto [it, inserted] = files_.try_emplace(fileId, std::move(state));
    if (!inserted)
        checkSize(*it->second);
}

void FileCollector::OnNewChunk(std::uint32_t fileId, size_t pos, std::vector<std::uint8_t>&& chunk)
{
    auto file = Find(fileId);
    const size_t len = chunk.size();

    if (pos > file->size || len > file->size - pos) 
        throw FileCollectorError(Code::OutOfRange, "chunk is out of file bounds");
    
    if (len == 0 || file->complete.load(std::memory_order_acquire)) 
        return;  // пустой чанк или файл уже собран -это дубликат
    

    // Под замком: найти дыры и забронировать их за собой.
    std::vector<Range> mine;
    {
        std::lock_guard lock(file->mutex);
        mine = SubtractBusy(file->done.Gaps(pos, pos + len), file->busy);
        if (mine.empty())
            return;  // всё уже записано или прямо сейчас копируется другими

        /*На пределе пускаем только чанки, которые касаются полученного: они не плодят участки,
         а заполняют дыры. Иначе упёршийся в предел файл не собрался бы никогда.*/
        if (file->done.Count() >= maxRanges_ && !file->done.Touches(pos, pos + len)) 
            throw FileCollectorError(Code::TooFragmented, "too many separate ranges in one file");
        
        file->busy.insert(file->busy.end(), mine.begin(), mine.end()); //бронируем
    }

    //Без замка: копируем. Эти байты забронированы нами, никто другой их не пишет и не читает
    for (const Range& r : mine) 
        std::copy_n(chunk.data() + (r.begin - pos), r.end - r.begin, file->bytes.get() + r.begin);
    

    // под замком: снять бронь и отметить байты записанными
    std::lock_guard lock(file->mutex);
    for (const Range& r : mine) 
        file->busy.erase(std::find(file->busy.begin(), file->busy.end(), r));
    
    for (const Range& r : mine) 
        file->done.Add(r.begin, r.end);
    
    if (file->done.CoversAll(file->size)) 
        file->complete.store(true, std::memory_order_release);
    
}

FileData FileCollector::GetFile(std::uint32_t fileId) const
{
    auto file = Find(fileId);

    if (!file->complete.load(std::memory_order_acquire)) 
        return {};  // ещё не собран(operator bool вернет false)
    
    return {file->bytes, file->size};
}

size_t FileCollector::Read(std::uint32_t fileId, std::size_t pos, std::span<std::uint8_t> out) const
{
    auto file = Find(fileId);

    if (pos > file->size) 
        throw FileCollectorError(Code::OutOfRange, "read position is out of file bounds");
    

    size_t ready = 0;
    {  // под замком только узнаём, сколько байт подряд уже записано
        std::lock_guard lock(file->mutex);
        ready = file->done.ContiguousEnd(pos) - pos;
    }

    // копируем без замка: записанные байты больше никогда не меняются
    const size_t n = std::min(ready, out.size()); // защита от out of range
    std::copy_n(file->bytes.get() + pos, n, out.data());
    return n;
}

void FileCollector::RemoveFile(std::uint32_t fileId)
{
    std::shared_ptr<FileState> removed;  // объявлен до замка,  значит, память освободится после него
    std::lock_guard lock(filesMutex_);
    auto it = files_.find(fileId);
    if (it == files_.end()) 
        throw FileCollectorError(Code::UnknownFile, "unknown fileId");
    
    removed = std::move(it->second);
    files_.erase(it);
}

