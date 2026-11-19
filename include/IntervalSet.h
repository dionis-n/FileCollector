#pragma once

#include <cstddef>
#include <map>
#include <vector>

struct Range
{
    size_t begin = 0;
    size_t end = 0;

    friend bool operator==(const Range& a, const Range& b) = default;
};

class IntervalSet
{
private:
    std::map<size_t, size_t> ranges_;
public:
    std::vector<Range> Gaps(size_t begin, size_t end) const; // пропуски
    void Add(size_t begin, size_t end); 
    size_t ContiguousEnd(size_t pos) const; // докуда можно читать файл с позиции pos
    bool Touches(size_t begin, size_t end) const;
    bool CoversAll(size_t size) const; // true - от 0 до size один непрерывный участок
    size_t Count() const;
};