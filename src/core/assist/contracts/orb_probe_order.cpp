#include "orb_probe_order.hpp"
#include <algorithm>

std::vector<int> EbOrbProbeOrder::window(int first, int last, int limit) {
    if (first < 0 || last < first || limit < 1 || first > limit)
        return {};
    int const low = std::max(1, first);
    int const high = std::min(limit, last);
    if (low > high)
        return {};
    int const middle = low + (high - low) / 2;
    std::vector<int> order{middle};
    for (int distance = 1; distance <= high - low; ++distance) {
        if (middle - distance >= low)
            order.push_back(middle - distance);
        if (middle + distance <= high)
            order.push_back(middle + distance);
    }
    return order;
}
