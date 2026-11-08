#ifndef CLOUD_CONFIG_LIST_CELL_HPP
#define CLOUD_CONFIG_LIST_CELL_HPP

#include "../cloud/cloud_config_index.hpp"
#include <Geode/Geode.hpp>
#include <functional>
#include <string>

// GD-style alternating list row (comment/level-list look).
class CloudConfigListCell : public cocos2d::CCNode {
public:
    using SelectCallback = std::function<void(std::string const& id)>;

    static constexpr float kHeight = 42.f;

    class StatsBadge {
    public:
        bool show = false;
        int downloads = 0;
        float ratingAvg = 0.f;
        int ratingCount = 0;

    private:
    };

    static CloudConfigListCell* create(CloudConfigIndex::PackEntry const& entry, float width,
                                       int index, bool selected, bool active, bool cached,
                                       bool updateAvailable, StatsBadge const& stats,
                                       SelectCallback onSelect);

    std::string const& packId() const {
        return m_id;
    }

protected:
    bool init(CloudConfigIndex::PackEntry const& entry, float width, int index, bool selected,
              bool active, bool cached, bool updateAvailable, StatsBadge const& stats,
              SelectCallback onSelect);

    void onClick(cocos2d::CCObject*);

    std::string m_id;
    SelectCallback m_onSelect;

private:
};

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>
#include <cocos-ext.h>
#include <fmt/format.h>

class CloudConfigListCellImplementation {
public:
    static std::string formatDlBadge(int count);

private:
    CloudConfigListCellImplementation() = delete;
};

#endif
