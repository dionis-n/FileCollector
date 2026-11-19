#include "IntervalSet.h"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using Ranges = std::vector<Range>;

TEST(IntervalSet, EmptySetHasOneGapEverywhere)
{
    IntervalSet s;
    EXPECT_EQ(s.Gaps(2, 8), (Ranges{{2, 8}}));
    EXPECT_EQ(s.Count(), 0u);
}

TEST(IntervalSet, EmptyRequestHasNoGaps)
{
    IntervalSet s;
    EXPECT_TRUE(s.Gaps(5, 5).empty());
}

TEST(IntervalSet, ExampleFromTask)
{
    IntervalSet s;
    s.Add(0, 4);
    EXPECT_EQ(s.Gaps(2, 6), (Ranges{{4, 6}}));  
    s.Add(2, 6);
    s.Add(6, 10);
    EXPECT_EQ(s.Count(), 1u); 
    EXPECT_TRUE(s.CoversAll(10));
}

TEST(IntervalSet, TouchingRangesMerge)
{
    IntervalSet s;
    s.Add(0, 4);
    s.Add(4, 8);
    EXPECT_EQ(s.Count(), 1u);
    EXPECT_EQ(s.ContiguousEnd(0), 8u);
}

TEST(IntervalSet, NestedRangeChangesNothing)
{
    IntervalSet s;
    s.Add(0, 10);
    s.Add(3, 5);
    EXPECT_EQ(s.Count(), 1u);
    EXPECT_TRUE(s.Gaps(3, 5).empty());
}

TEST(IntervalSet, RangeSwallowsSeveralRanges)
{
    IntervalSet s;
    s.Add(2, 3);
    s.Add(5, 6);
    s.Add(8, 9);
    EXPECT_EQ(s.Gaps(0, 10), (Ranges{{0, 2}, {3, 5}, {6, 8}, {9, 10}}));
    s.Add(1, 9);
    EXPECT_EQ(s.Count(), 1u);
    EXPECT_EQ(s.Gaps(0, 10), (Ranges{{0, 1}, {9, 10}}));
}

TEST(IntervalSet, NewRangeLeftOfAllRanges)
{
    IntervalSet s;
    s.Add(5, 8);
    s.Add(1, 6); 
    EXPECT_EQ(s.Count(), 1u);
    EXPECT_EQ(s.Gaps(0, 10), (Ranges{{0, 1}, {8, 10}}));
}

TEST(IntervalSet, ContiguousEndInGapAndInsideRange)
{
    IntervalSet s;
    s.Add(2, 5);
    EXPECT_EQ(s.ContiguousEnd(0), 0u);
    EXPECT_EQ(s.ContiguousEnd(3), 5u);
    EXPECT_EQ(s.ContiguousEnd(5), 5u);
}

TEST(IntervalSet, Touches)
{
    IntervalSet s;
    EXPECT_FALSE(s.Touches(0, 5));
    s.Add(4, 6);
    EXPECT_TRUE(s.Touches(5, 9));
    EXPECT_TRUE(s.Touches(6, 9));
    EXPECT_TRUE(s.Touches(1, 4));
    EXPECT_FALSE(s.Touches(7, 9));
    EXPECT_FALSE(s.Touches(0, 3));
}

TEST(IntervalSet, CoversAll)
{
    IntervalSet s;
    EXPECT_TRUE(s.CoversAll(0));
    EXPECT_FALSE(s.CoversAll(3));
    s.Add(0, 2);
    EXPECT_FALSE(s.CoversAll(3));
    s.Add(2, 3);
    EXPECT_TRUE(s.CoversAll(3));
}

// Тысячи случайных участков: сверяем с простым эталоном по флагу на байт
TEST(IntervalSet, RandomAgainstByteModel)
{
    std::mt19937 rng(12345);  // фиксированное зерно: падение всегда повторится
    const size_t size = 300;
    IntervalSet s;
    std::vector<bool> model(size, false);
    for (int step = 0; step < 3000; ++step)
    {
        size_t b = rng() % size;
        size_t e = b + rng() % 20;
        if (e > size) 
            e = size;
        
        // Gaps обязан вернуть ровно те байты, которых нет в эталоне
        Ranges expected;
        for (size_t i = b; i < e; ++i)
        {
            if (!model[i])
            {
                if (!expected.empty() && expected.back().end == i) 
                    expected.back().end = i + 1;
                else 
                    expected.push_back({i, i + 1});
                
            }
        }
        ASSERT_EQ(s.Gaps(b, e), expected) << "step " << step;
        s.Add(b, e);
        for (size_t i = b; i < e; ++i) 
            model[i] = true;
        
    }
}