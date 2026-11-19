#include "IntervalSet.h"
#include <algorithm>
#include <iterator>

std::vector<Range> IntervalSet::Gaps(size_t begin, size_t end) const
{
    std::vector<Range> gaps;
    size_t cur = begin;

    auto it = ranges_.upper_bound(begin);
    if (it != ranges_.begin()) //
        --it;

    for (; it != ranges_.end() && it->first < end && cur < end; ++it)
    {
        if (it->second <= cur)
            continue; 

        if (it->first > cur)
            gaps.push_back({cur, it->first});


        cur = it->second;
    }

    if (cur < end)
        gaps.push_back({cur, end});

    return gaps;
}

void IntervalSet::Add(size_t begin, size_t end)
{
    if (begin >= end) return;

    auto first = ranges_.upper_bound(begin);
    if (first != ranges_.begin() && std::prev(first)->second >= begin)
        --first;

    auto last = first;
    size_t newEnd = end;
    while (last != ranges_.end() && last->first <= end)
    {
        newEnd = std::max(newEnd, last->second);
        ++last;
    }

    if (first != last && first->first <= begin) // расширяем существующий
    {
        first->second = newEnd;
        ranges_.erase(std::next(first), last);
    }
    else 
    {
        ranges_.emplace_hint(first, begin, newEnd);
        ranges_.erase(first, last);
    }
}

size_t IntervalSet::ContiguousEnd(size_t pos) const
{
    auto it = ranges_.upper_bound(pos);
    if (it == ranges_.begin())
        return pos;

    --it;
    return it->second > pos ? it->second : pos;
}

bool IntervalSet::Touches(size_t begin, size_t end) const
{
    auto it = ranges_.upper_bound(end);
    if (it == ranges_.begin()) 
        return false;
    
    --it;
    return it->second >= begin;
}

bool IntervalSet::CoversAll(size_t size) const
{
    if (size == 0) 
        return true;
    
    return ranges_.size() == 1 && ranges_.begin()->first == 0 && ranges_.begin()->second >= size;
    //Если файл собран -> все участки склеены -> size == 1
}

size_t IntervalSet::Count() const
{
    return ranges_.size();
}