#ifndef SPIDER_CONTINUATION_HPP
#define SPIDER_CONTINUATION_HPP
class EbSpiderContinuation {
public:
    static int probe(int limit, int index);
    static bool allowed(int baselineDeath, int delay, int delayedDeath, int nextPress);
    static bool verified(bool died, bool unknown, int ticksRan, int horizon, bool followupJumped,
                         int nextPress);

private:
};
#endif
