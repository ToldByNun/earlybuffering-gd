#ifndef MODULE_BINDINGS_HPP
#define MODULE_BINDINGS_HPP

class EbModuleBindings {
public:
    static void registerSettings();

private:
    static void onAnySetting();
    EbModuleBindings() = delete;
};

#endif
