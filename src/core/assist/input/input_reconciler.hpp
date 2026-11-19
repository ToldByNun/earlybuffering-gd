#ifndef INPUT_RECONCILER_HPP
#define INPUT_RECONCILER_HPP
#include <array>
#include <cstdint>
class EbInputReconciler {
public:
    enum class Device { Unknown, Keyboard, Mouse, Touch };
    enum class Disposition { Requested, AggregatedNoEdge, Repeat, Dispatched, Overflow };
    class Source {
    public:
        Device device = Device::Unknown;
        int id = 0;

    private:
    };
    class Event {
    public:
        std::uint64_t sequence = 0;
        Source source;
        int player = 0;
        bool down = false;
        long long timeUs = 0;
        Disposition disposition = Disposition::Requested;

    private:
    };
    class State {
    public:
        bool physical = false;
        bool requested = false;
        bool dispatched = false;

    private:
    };
    bool request(Source source, int player, bool down, long long timeUs);
    void acknowledge(int player, bool down, long long timeUs);
    State state(int player) const;
    void reset();
    Event const& latest() const;

private:
    class Owner {
    public:
        Source source;
        int player = 0;
        bool used = false;
        bool held = false;

    private:
    };
    void record(Source source, int player, bool down, long long timeUs, Disposition disposition);
    std::array<Owner, 32> m_owners{};
    std::array<State, 2> m_states{};
    std::array<Event, 128> m_events{};
    std::uint64_t m_sequence = 0;
};
#endif
