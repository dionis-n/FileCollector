#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

class FileCollectorError : public std::runtime_error
{
public:
    enum class Code
    {
        UnknownFile, // id файла не зарегистрирован
        SizeMismatch, // дубль файла с другим размером
        OutOfRange, // чанк или чтение выходят за границы файла
        TooFragmented // защита от враждебного потока данных
    };

private:
    Code code_;

public:
    FileCollectorError(Code code, const char* what);
    Code code() const noexcept;
};



struct FileData
{
    std::shared_ptr<const std::uint8_t[]> bytes;
    size_t size = 0;

    explicit operator bool() const noexcept;
    std::span<const std::uint8_t> Span() const noexcept;
};







class FileCollector {
private:
    struct FileState;  // всё о файле

    std::shared_ptr<FileState> Find(std::uint32_t fileId) const;

    const size_t maxRanges_; // максимально разрешённое кол-во фрагментов
    mutable std::mutex filesMutex_; // мьютекс для хранилища
    std::unordered_map<std::uint32_t, std::shared_ptr<FileState>> files_; // хранилище всех файлов

public:
    explicit FileCollector(size_t maxRangesPerFile = size_t{1} << 20); // защита от враждебных данных(2^20) по умолчанию. Можно передать своё значение

    FileCollector(const FileCollector&) = delete;
    FileCollector& operator=(const FileCollector&) = delete;

    void CollectFile(std::uint32_t fileId, size_t fileSize); // регистрация файла
    void OnNewChunk(std::uint32_t fileId, size_t pos, std::vector<std::uint8_t>&& chunk); // добавить чанк
    FileData GetFile(std::uint32_t fileId) const; // целый файл
    size_t Read(std::uint32_t fileId, size_t pos, std::span<std::uint8_t> out) const; // Read(какой файл, с какой позиции, куда)
    void RemoveFile(std::uint32_t fileId);
};