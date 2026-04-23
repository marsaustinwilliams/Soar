#ifndef SnapshotTestCategory_hpp
#define SnapshotTestCategory_hpp

#include "SoarHelper.hpp"
#include "TestCategory.hpp"
#include "TestHelpers.hpp"

#include <typeinfo>
#include <cstdlib>
#include <algorithm>

inline uint64_t snapshot_timeout_budget_ms()
{
    const uint64_t kDefaultBudgetMs = 300000;
    const char* env = std::getenv("SOAR_SNAPSHOT_TIMEOUT_MS");
    if (!env || !*env)
    {
        return kDefaultBudgetMs;
    }

    char* end_ptr = nullptr;
    const unsigned long parsed = std::strtoul(env, &end_ptr, 10);
    if ((end_ptr == env) || (*end_ptr != '\0'))
    {
        return kDefaultBudgetMs;
    }

    return static_cast<uint64_t>(parsed);
}

template <typename T>
class SnapshotTestCategory : public T
{
public:
    SnapshotTestCategory() : T()
    {
        this->m_categoryName = TestHelpers::demangle(typeid(T).name());

        for (auto& test : this->m_TestCategory_tests)
        {
            std::get<2>(test) = "[snapshot] " + std::get<2>(test);
            std::get<1>(test) = std::max(std::get<1>(test), snapshot_timeout_budget_ms());
        }
    }

    void before() override
    {
        previousSnapshotMode = SoarHelper::snapshot_every_step;
        SoarHelper::snapshot_every_step = true;

        try
        {
            T::before();
        }
        catch (...)
        {
            SoarHelper::snapshot_every_step = previousSnapshotMode;
            throw;
        }
    }

    void after(bool caught) override
    {
        try
        {
            T::after(caught);
        }
        catch (...)
        {
            SoarHelper::snapshot_every_step = previousSnapshotMode;
            throw;
        }

        SoarHelper::snapshot_every_step = previousSnapshotMode;
    }

private:
    bool previousSnapshotMode = false;
};

#endif