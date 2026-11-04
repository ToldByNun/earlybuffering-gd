#ifndef EARLYBUFFER_UI_CLOUDCONFIGLISTCELL_HPP
#define EARLYBUFFER_UI_CLOUDCONFIGLISTCELL_HPP

#include "../cloud/CloudConfigIndex.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

// GD-style alternating list row (comment/level-list look).
class CloudConfigListCell : public cocos2d::CCNode {
public:
    using SelectCallback = std::function<void(std::string const& id)>;

    static constexpr float kHeight = 42.f;

    struct StatsBadge {
        bool show = false;
        int downloads = 0;
        float ratingAvg = 0.f;
        int ratingCount = 0;
    };

    static CloudConfigListCell* create(
        CloudConfigIndex::PackEntry const& entry,
        float width,
        int index,
        bool selected,
        bool active,
        bool cached,
        bool updateAvailable,
        StatsBadge const& stats,
        SelectCallback onSelect
    );

    std::string const& packId() const { return m_id; }

protected:
    bool init(
        CloudConfigIndex::PackEntry const& entry,
        float width,
        int index,
        bool selected,
        bool active,
        bool cached,
        bool updateAvailable,
        StatsBadge const& stats,
        SelectCallback onSelect
    );

    void onClick(cocos2d::CCObject*);

    std::string m_id;
    SelectCallback m_onSelect;
};

#endif // EARLYBUFFER_UI_CLOUDCONFIGLISTCELL_HPP
