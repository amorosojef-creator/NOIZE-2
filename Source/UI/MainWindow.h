#pragma once
#include "AppContext.h"

namespace noize
{
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (AppContext& ctx);
    ~MainWindow() override;
    void closeButtonPressed() override;
    /** Handles a command line / double-clicked project path. */
    void openFile (const juce::File&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
