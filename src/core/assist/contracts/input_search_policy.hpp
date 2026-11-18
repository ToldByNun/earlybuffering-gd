#ifndef INPUT_SEARCH_POLICY_HPP
#define INPUT_SEARCH_POLICY_HPP
class EbInputSearchPolicy {
public:
    static bool usesInputWindow(int mode, bool down, bool dashing);
    static bool needsRescue(bool baselineDied, bool unknown, bool diedBeforeAction, int limit);

private:
};
#endif
