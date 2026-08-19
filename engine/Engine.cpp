#include "Engine.h"
#include <iomanip>
#include <utility>

namespace Engine {

    Engine::Engine(std::vector<Data::Equity> initial_universe)
        : universe(std::move(initial_universe)) {}

    // --- Universe Management ---

    void Engine::addEquity(const Data::Equity& equity) {
        universe.push_back(equity);
    }

    void Engine::addEquity(Data::Equity&& equity) {
        universe.push_back(std::move(equity));
    }

    void Engine::setUniverse(std::vector<Data::Equity> new_universe) {
        universe = std::move(new_universe);
    }

    void Engine::clearUniverse() {
        universe.clear();
    }

    const std::vector<Data::Equity>& Engine::getUniverse() const {
        return universe;
    }

    size_t Engine::getUniverseSize() const {
        return universe.size();
    }

    // --- Filter Management ---

    void Engine::addFilter(Data::Metric metric, float min_val, float max_val) {
        filters.emplace_back(metric, min_val, max_val);
    }

    void Engine::addFilter(const FilterRule& filter) {
        filters.push_back(filter);
    }

    void Engine::clearFilters() {
        filters.clear();
    }

    const std::vector<FilterRule>& Engine::getFilters() const {
        return filters;
    }

    // --- Metric-Specific Convenience Methods ---

    Engine& Engine::filterSpotPrice(float min_val, float max_val) {
        addFilter(Data::Metric::SpotPrice, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterPE(float min_val, float max_val) {
        addFilter(Data::Metric::PE, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterForwardPE(float min_val, float max_val) {
        addFilter(Data::Metric::ForwardPE, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterPB(float min_val, float max_val) {
        addFilter(Data::Metric::PB, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterPS(float min_val, float max_val) {
        addFilter(Data::Metric::PS, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterEVEBITDA(float min_val, float max_val) {
        addFilter(Data::Metric::EVEBITDA, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterROA(float min_val, float max_val) {
        addFilter(Data::Metric::ROA, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterROE(float min_val, float max_val) {
        addFilter(Data::Metric::ROE, min_val, max_val);
        return *this;
    }

    Engine& Engine::filterROIC(float min_val, float max_val) {
        addFilter(Data::Metric::ROIC, min_val, max_val);
        return *this;
    }

    // --- Screening Execution ---

    bool Engine::matchesFilters(const Data::Equity& equity) const {
        return matchesFilters(equity, filters);
    }

    bool Engine::matchesFilters(const Data::Equity& equity, const std::vector<FilterRule>& filter_list) const {
        for (const auto& rule : filter_list) {
            if (!equity.passesFilter(rule.metric, rule.min_val, rule.max_val)) {
                return false;
            }
        }
        return true;
    }

    std::vector<Data::Equity> Engine::runScreen() const {
        return runScreen(filters);
    }

    std::vector<Data::Equity> Engine::runScreen(const std::vector<FilterRule>& custom_filters) const {
        std::vector<Data::Equity> matched;
        for (const auto& equity : universe) {
            if (matchesFilters(equity, custom_filters)) {
                matched.push_back(equity);
            }
        }
        return matched;
    }

    std::vector<Data::Equity> Engine::runScreenAndSort(Data::Metric sort_metric, SortOrder order) const {
        auto results = runScreen();
        sortEquities(results, sort_metric, order);
        return results;
    }

    void Engine::sortEquities(std::vector<Data::Equity>& equities, Data::Metric metric, SortOrder order) {
        std::sort(equities.begin(), equities.end(), [metric, order](const Data::Equity& a, const Data::Equity& b) {
            float valA = a.getMetricValue(metric);
            float valB = b.getMetricValue(metric);
            if (order == SortOrder::Ascending) {
                return valA < valB;
            } else {
                return valA > valB;
            }
        });
    }

    void Engine::printResults(const std::vector<Data::Equity>& results, std::ostream& os) const {
        printResults(results, universe.size(), os);
    }

    void Engine::printResults(const std::vector<Data::Equity>& results,
                              std::size_t universe_size,
                              std::ostream& os) {
        os << "==========================================================================================\n";
        os << " Screener Results (" << results.size() << " matched out of " << universe_size << " total equities)\n";
        os << "==========================================================================================\n";
        if (results.empty()) {
            os << " No equities matched the screening criteria.\n";
        } else {
            for (const auto& equity : results) {
                os << " " << equity << "\n";
            }
        }
        os << "==========================================================================================\n";
    }

} // namespace Engine