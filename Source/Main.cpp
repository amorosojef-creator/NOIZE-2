#include "UI/MainWindow.h"
#include "Core/Project.h"

namespace noize
{
class NoizeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "NOIZE"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto cl = commandLine.unquoted().trim();
        if (cl.startsWith ("--make-demo"))
        {
            auto out = juce::File (cl.fromFirstOccurrenceOf ("--make-demo", false, false).unquoted().trim());
            out.getParentDirectory().createDirectory();
            const bool ok = makeDemoProject().saveToFile (out);
            std::printf (ok ? "demo written: %s\n" : "demo FAILED: %s\n", out.getFullPathName().toRawUTF8());
            setApplicationReturnValue (ok ? 0 : 1); quit(); return;
        }
        ctx = std::make_unique<AppContext>();
        ctx->settings.load();
        window = std::make_unique<MainWindow> (*ctx);
        auto path = commandLine.unquoted().trim();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path)) window->openFile (juce::File (path));
    }
    void shutdown() override { window.reset(); if (ctx) ctx->settings.save(); ctx.reset(); }
    void systemRequestedQuit() override { if (window) window->closeButtonPressed(); else quit(); }
private:
    std::unique_ptr<AppContext> ctx;
    std::unique_ptr<MainWindow> window;
};
}

START_JUCE_APPLICATION (noize::NoizeApp)
