#ifndef EQUITIESSCREENER_ENGINE_H
#define EQUITIESSCREENER_ENGINE_H

#include "../data/Equity.h"
#include <vector>
#include <limits>
#include <iostream>
#include <algorithm>

namespace Engine {

    enum class SortOrder {
        Ascending,
        Descending
    };

    struct FilterRule {
        Data::Metric metric;
        float min_val{-std::numeric_limits<float>::infinity()};
        float max_val{std::numeric_limits<float>::infinity()};

        FilterRule(Data::Metric m,
                   float min_v = -std::numeric_limits<float>::infinity(),
                   float max_v = std::numeric_limits<float>::infinity())
            : metric(m), min_val(min_v), max_val(max_v) {}
    };

    class Engine {
    private:
        std::vector<Data::Equity> universe;
        std::vector<FilterRule> filters;

    public:
        Engine() = default;
        explicit Engine(std::vector<Data::Equity> initial_universe);

        // Universe Management
        void addEquity(const Data::Equity& equity);
        void addEquity(Data::Equity&& equity);
        void setUniverse(std::vector<Data::Equity> new_universe);
        void clearUniverse();
        const std::vector<Data::Equity>& getUniverse() const;
        size_t getUniverseSize() const;

        // Filter Management
        void addFilter(Data::Metric metric,
                       float min_val = -std::numeric_limits<float>::infinity(),
                       float max_val = std::numeric_limits<float>::infinity());
        void addFilter(const FilterRule& filter);
        void clearFilters();
        const std::vector<FilterRule>& getFilters() const;

        // Metric-specific convenience filter methods (support chaining)
        Engine& filterSpotPrice(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterPE(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterForwardPE(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterPB(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterPS(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterEVEBITDA(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterROA(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterROE(float min_val, float max_val = std::numeric_limits<float>::infinity());
        Engine& filterROIC(float min_val, float max_val = std::numeric_limits<float>::infinity());

        // Screening & Sorting
        bool matchesFilters(const Data::Equity& equity) const;
        bool matchesFilters(const Data::Equity& equity, const std::vector<FilterRule>& filter_list) const;
        std::vector<Data::Equity> runScreen() const;
        std::vector<Data::Equity> runScreen(const std::vector<FilterRule>& custom_filters) const;
        std::vector<Data::Equity> runScreenAndSort(Data::Metric sort_metric, SortOrder order = SortOrder::Descending) const;

        static void sortEquities(std::vector<Data::Equity>& equities, Data::Metric metric, SortOrder order = SortOrder::Descending);

        // Output helper
        void printResults(const std::vector<Data::Equity>& results, std::ostream& os = std::cout) const;

        // Same table for callers that no longer hold the Engine (the results
        // outlived it): universe_size is the "out of N total" header figure.
        static void printResults(const std::vector<Data::Equity>& results,
                                 std::size_t universe_size,
                                 std::ostream& os = std::cout);
    };

} // namespace Engine

#endif //EQUITIESSCREENER_ENGINE_H