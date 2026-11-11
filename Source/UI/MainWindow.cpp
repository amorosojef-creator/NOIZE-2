#include "MainWindow.h"
#include "ArrangementView.h"
#include "SessionView.h"
#include "ChannelRackView.h"
#include "PianoRollView.h"
#include "MixerView.h"
#include "SynthView.h"
#include "VisualizerView.h"

namespace noize
{
static bool isAudioExt (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

//==============================================================================
// Sample browser: search, favorites, folders, preview with waveform, drag & drop
//==============================================================================
class SampleBrowser : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener, private juce::Timer
{
public:
    explicit SampleBrowser (AppContext& c)
        : ctx (c), thumbCache (8), thumb (512, formats, thumbCache), list ("samples", this)
    {
        formats.registerBasicFormats();
        search.setTextToShowWhenEmpty ("Search samples...", Theme::dim);
        search.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible (search);
        favOnly.setButtonText ("Fav"); favOnly.setClickingTogglesState (true); favOnly.onClick = [this] { applyFilter(); };
        addBtn.setButtonText ("+ Folder"); addBtn.onClick = [this] { addFolder(); };
        importBtn.setButtonText ("Import"); importBtn.onClick = [this] { importFiles(); };
        for (auto* b : { &favOnly, &addBtn, &importBtn }) addAndMakeVisible (b);
        list.setRowHeight (22); addAndMakeVisible (list);
        thumb.addChangeListener (this);
        player.setSource (&transport);
        ctx.engine.deviceManager().addAudioCallback (&player);
        rescan();
        startTimerHz (20);
    }
    ~SampleBrowser() override
    {
        stopTimer();
        ctx.engine.deviceManager().removeAudioCallback (&player);
        transport.setSource (nullptr); player.setSource (nullptr);
        thumb.removeChangeListener (this);
        alive = false;
    }

    void rescan()
    {
        auto dirs = ctx.settings.sampleDirs;
        auto token = ++scanToken;
        juce::Thread::launch ([this, dirs, token, safe = juce::Component::SafePointer<SampleBrowser> (this)]
        {
            juce::Array<juce::File> found;
            for (auto& d : dirs)
            {
                juce::File dir (d);
                if (! dir.isDirectory()) continue;
                for (auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3"))
                {
                    found.add (f);
                    if (found.size() >= 8000) break;
                }
            }
            juce::MessageManager::callAsync ([safe, found, token]
            {
                if (safe != nullptr && safe->scanToken == token) { safe->all = found; safe->applyFilter(); }
            });
        });
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (26); search.setBounds (top);
        r.removeFromTop (4);
        auto row = r.removeFromTop (26);
        favOnly.setBounds (row.removeFromLeft (46)); row.removeFromLeft (4);
        addBtn.setBounds (row.removeFromLeft (80)); row.removeFromLeft (4); importBtn.setBounds (row.removeFromLeft (70));
        r.removeFromTop (4);
        waveArea = r.removeFromBottom (92);
        r.removeFromBottom (4);
        list.setBounds (r);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::panel2); g.fillRoundedRectangle (waveArea.toFloat(), 4.f);
        g.setColour (Theme::edge); g.drawRoundedRectangle (waveArea.toFloat(), 4.f, 1.f);
        auto inner = waveArea.reduced (4);
        if (thumb.getTotalLength() > 0.0)
        {
            g.setColour (Theme::accent.withAlpha (0.85f));
            thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), 1.0f);
            if (transport.isPlaying())
            {
                const float x = inner.getX() + (float) (transport.getCurrentPosition() / juce::jmax (0.001, thumb.getTotalLength())) * inner.getWidth();
                g.setColour (Theme::text); g.drawVerticalLine ((int) x, (float) inner.getY(), (float) inner.getBottom());
            }
        }
        else
        {
            g.setColour (Theme::dim); g.setFont (Theme::font (12.f));
            g.drawText (previewName.isEmpty() ? "Click a sample to preview" : "Loading...", inner, juce::Justification::centred);
        }
        if (previewName.isNotEmpty()) { g.setColour (Theme::text); g.setFont (Theme::font (11.f)); g.drawText (previewName, inner.removeFromTop (14), juce::Justification::topLeft); }
    }

private:
    int getNumRows() override { return shown.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (row >= shown.size()) return;
        if (sel) { g.setColour (Theme::accent.withAlpha (0.18f)); g.fillRect (0, 0, w, h); }
        const auto& f = shown.getReference (row);
        const bool fav = ctx.settings.favorites.contains (f.getFullPathName());
        g.setColour (fav ? Theme::accent : Theme::dim); g.setFont (Theme::font (14.f, true));
        g.drawText (fav ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x86")), 0, 0, 20, h, juce::Justification::centred);
        g.setColour (Theme::text); g.setFont (Theme::font (12.f));
        g.drawText (f.getFileName(), 24, 0, w - 28, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= shown.size()) return;
        auto f = shown[row];
        if (e.x < 22)
        {
            auto p = f.getFullPathName();
            if (ctx.settings.favorites.contains (p)) ctx.settings.favorites.removeString (p); else ctx.settings.favorites.add (p);
            ctx.settings.save(); applyFilter(); return;
        }
        preview (f);
    }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override
    {
        if (rows.size() == 0) return {};
        const int r = rows[0];
        return r < shown.size() ? juce::var (shown[r].getFullPathName()) : juce::var();
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint (waveArea); }
    void timerCallback() override { if (transport.isPlaying()) repaint (waveArea); }

    void preview (const juce::File& f)
    {
        transport.stop(); transport.setSource (nullptr); readerSource.reset();
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        previewName = f.getFileName();
        if (r == nullptr) { previewName += "  (cannot decode)"; thumb.clear(); repaint(); return; }
        const double sr = r->sampleRate;
        readerSource = std::make_unique<juce::AudioFormatReaderSource> (r.release(), true);
        transport.setSource (readerSource.get(), 0, nullptr, sr);
        transport.setPosition (0.0); transport.start();
        thumb.setSource (new juce::FileInputSource (f));
        repaint();
    }
    void applyFilter()
    {
        shown.clear();
        const auto q = search.getText().trim();
        for (auto& f : all)
        {
            if (q.isNotEmpty() && ! f.getFileName().containsIgnoreCase (q) && ! f.getParentDirectory().getFileName().containsIgnoreCase (q)) continue;
            if (favOnly.getToggleState() && ! ctx.settings.favorites.contains (f.getFullPathName())) continue;
            shown.add (f);
        }
        list.updateContent(); list.repaint();
    }
    void addFolder()
    {
        chooser = std::make_shared<juce::FileChooser> ("Add sample folder", juce::File(), "");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            auto d = fc.getResult();
            if (d.isDirectory()) { ctx.settings.sampleDirs.addIfNotAlreadyThere (d.getFullPathName()); ctx.settings.save(); rescan(); }
        });
    }
    void importFiles()
    {
        chooser = std::make_shared<juce::FileChooser> ("Import samples into library", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems, [this] (const juce::FileChooser& fc)
        {
            auto lib = Engine::appDataDir().getChildFile ("Samples"); lib.createDirectory();
            for (auto& f : fc.getResults()) if (isAudioExt (f)) f.copyFileTo (lib.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension()));
            ctx.settings.sampleDirs.addIfNotAlreadyThere (lib.getFullPathName()); ctx.settings.save(); rescan();
        });
    }

    AppContext& ctx;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbCache; juce::AudioThumbnail thumb;
    juce::AudioSourcePlayer player; juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::TextEditor search; juce::TextButton favOnly, addBtn, importBtn;
    juce::ListBox list;
    juce::Array<juce::File> all, shown;
    juce::Rectangle<int> waveArea;
    juce::String previewName;
    std::shared_ptr<juce::FileChooser> chooser;
    std::atomic<int> scanToken { 0 };
    bool alive = true;
};

//==============================================================================
// VST3 plugin manager: scan, list, load, show GUI, remember crashing plugins
//==============================================================================
class PluginManagerComponent : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit PluginManagerComponent (AppContext& c) : ctx (c), list ("plugins", this)
    {
        fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
        knownFile = Engine::appDataDir().getChildFile ("plugins.xml");
        if (auto xml = juce::parseXML (knownFile)) known.recreateFromXml (*xml);
        for (auto* b : { &scanBtn, &openBtn, &closeAllBtn }) addAndMakeVisible (b);
        scanBtn.setButtonText ("Scan VST3"); openBtn.setButtonText ("Open Selected"); closeAllBtn.setButtonText ("Unload All");
        scanBtn.onClick = [this] { startScan(); };
        openBtn.onClick = [this] { openSelected(); };
        closeAllBtn.onClick = [this] { windows.clear(); status.setText ("All plugins unloaded", juce::dontSendNotification); };
        status.setColour (juce::Label::textColourId, Theme::dim); addAndMakeVisible (status);
        list.setRowHeight (24); addAndMakeVisible (list);
        refreshStatus();
    }
    ~PluginManagerComponent() override { windows.clear(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto row = r.removeFromTop (28);
        scanBtn.setBounds (row.removeFromLeft (110)); row.removeFromLeft (6);
        openBtn.setBounds (row.removeFromLeft (130)); row.removeFromLeft (6); closeAllBtn.setBounds (row.removeFromLeft (110));
        status.setBounds (r.removeFromBottom (24));
        r.removeFromTop (6); list.setBounds (r);
    }
    void paint (juce::Graphics& g) override { g.fillAll (Theme::panel); }

private:
    struct PluginWindow : public juce::DocumentWindow
    {
        PluginWindow (std::unique_ptr<juce::AudioPluginInstance> p, std::function<void (PluginWindow*)> onClose)
            : DocumentWindow (p->getName(), Theme::panel, DocumentWindow::closeButton), plugin (std::move (p)), closeCb (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (plugin->createEditorAndMakeActive(), true);
            if (getContentComponent() == nullptr) setContentOwned (new juce::GenericAudioProcessorEditor (*plugin), true);
            setResizable (false, false); centreWithSize (getWidth(), getHeight()); setVisible (true);
        }
        void closeButtonPressed() override { if (closeCb) closeCb (this); }
        std::unique_ptr<juce::AudioPluginInstance> plugin; std::function<void (PluginWindow*)> closeCb;
    };

    int getNumRows() override { return known.getNumTypes(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        auto types = known.getTypes();
        if (row >= types.size()) return;
        if (sel) { g.setColour (Theme::accent.withAlpha (0.18f)); g.fillRect (0, 0, w, h); }
        auto& d = types.getReference (row);
        g.setColour (Theme::text); g.setFont (Theme::font (13.f));
        g.drawText (d.name + "  -  " + d.manufacturerName + (d.isInstrument ? "  [instrument]" : "  [effect]"), 8, 0, w - 12, h, juce::Justification::centredLeft, true);
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { openSelected(); }

    void refreshStatus()
    {
        juce::String s = juce::String (known.getNumTypes()) + " plugin(s) known";
        if (! failed.isEmpty()) s += "  |  " + juce::String (failed.size()) + " incompatible/failed (skipped safely)";
        status.setText (s, juce::dontSendNotification);
        list.updateContent(); list.repaint();
    }
    void startScan()
    {
        if (scanning) return;
        scanning = true; scanBtn.setEnabled (false); failed.clear();
        juce::StringArray extra = ctx.settings.pluginDirs;
        auto dead = Engine::appDataDir().getChildFile ("scan-deadmans.txt");
        juce::Thread::launch ([this, extra, dead, safe = juce::Component::SafePointer<PluginManagerComponent> (this)]
        {
            juce::StringArray fails;
            for (auto* format : fm.getFormats())
            {
                juce::FileSearchPath path = format->getDefaultLocationsToSearch();
                for (auto& e : extra) path.addIfNotAlreadyThere (juce::File (e));
                juce::PluginDirectoryScanner scanner (known, *format, path, true, dead, true);
                juce::String name;
                while (scanner.scanNextFile (true, name))
                {
                    juce::MessageManager::callAsync ([safe, name] { if (safe != nullptr) safe->status.setText ("Scanning: " + name, juce::dontSendNotification); });
                }
                fails.addArray (scanner.getFailedFiles());
            }
            juce::MessageManager::callAsync ([safe, fails]
            {
                if (safe == nullptr) return;
                safe->failed = fails; safe->scanning = false; safe->scanBtn.setEnabled (true);
                if (auto xml = safe->known.createXml()) xml->writeTo (safe->knownFile);
                safe->refreshStatus();
            });
        });
    }
    void openSelected()
    {
        const int row = list.getSelectedRow();
        auto types = known.getTypes();
        if (row < 0 || row >= types.size()) { status.setText ("Select a plugin first", juce::dontSendNotification); return; }
        auto desc = types[row];
        auto& dm = ctx.engine.deviceManager();
        const double sr = dm.getCurrentAudioDevice() ? dm.getCurrentAudioDevice()->getCurrentSampleRate() : 44100.0;
        const int bs = dm.getCurrentAudioDevice() ? dm.getCurrentAudioDevice()->getCurrentBufferSizeSamples() : 512;
        fm.createPluginInstanceAsync (desc, sr, bs, [safe = juce::Component::SafePointer<PluginManagerComponent> (this)] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
        {
            if (safe == nullptr) return;
            if (inst == nullptr) { safe->status.setText ("Could not load plugin: " + err, juce::dontSendNotification); return; }
            safe->windows.add (new PluginWindow (std::move (inst), [safe] (PluginWindow* w) { if (safe != nullptr) safe->windows.removeObject (w); }));
            safe->status.setText ("Plugin loaded (GUI hosting only - not yet routed into the mixer)", juce::dontSendNotification);
        });
    }
    void timerCallback() override {}

    AppContext& ctx;
    juce::AudioPluginFormatManager fm; juce::KnownPluginList known; juce::File knownFile;
    juce::ListBox list; juce::TextButton scanBtn, openBtn, closeAllBtn; juce::Label status;
    juce::OwnedArray<PluginWindow> windows; juce::StringArray failed;
    std::atomic<bool> scanning { false };
};

//==============================================================================
// Settings
//==============================================================================
class DirListEditor : public juce::Component, private juce::ListBoxModel
{
public:
    DirListEditor (const juce::String& title, juce::StringArray& dirs, std::function<void()> changed)
        : arr (dirs), cb (std::move (changed)), list ("d", this)
    {
        lbl.setText (title, juce::dontSendNotification); addAndMakeVisible (lbl);
        add.setButtonText ("Add..."); rem.setButtonText ("Remove");
        add.onClick = [this]
        {
            chooser = std::make_shared<juce::FileChooser> ("Choose folder", juce::File(), "");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
            { if (fc.getResult().isDirectory()) { arr.addIfNotAlreadyThere (fc.getResult().getFullPathName()); list.updateContent(); cb(); } });
        };
        rem.onClick = [this] { const int r = list.getSelectedRow(); if (r >= 0 && r < arr.size()) { arr.remove (r); list.updateContent(); cb(); } };
        addAndMakeVisible (add); addAndMakeVisible (rem); list.setRowHeight (22); addAndMakeVisible (list);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (26); lbl.setBounds (top.removeFromLeft (r.getWidth() - 160));
        rem.setBounds (top.removeFromRight (74)); top.removeFromRight (4); add.setBounds (top.removeFromRight (74));
        list.setBounds (r);
    }
private:
    int getNumRows() override { return arr.size(); }
    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (sel) { g.setColour (Theme::accent.withAlpha (0.18f)); g.fillRect (0, 0, w, h); }
        g.setColour (Theme::text); g.setFont (Theme::font (12.f));
        if (row < arr.size()) g.drawText (arr[row], 6, 0, w - 8, h, juce::Justification::centredLeft, true);
    }
    juce::StringArray& arr; std::function<void()> cb; juce::Label lbl; juce::TextButton add, rem; juce::ListBox list;
    std::shared_ptr<juce::FileChooser> chooser;
};

class SettingsComponent : public juce::TabbedComponent
{
public:
    SettingsComponent (AppContext& c, std::function<void()> libsChanged, std::function<void()> applyUi)
        : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop), ctx (c), apply (std::move (applyUi))
    {
        setColour (juce::TabbedComponent::backgroundColourId, Theme::panel);
        audio = std::make_unique<juce::AudioDeviceSelectorComponent> (ctx.engine.deviceManager(), 0, 2, 2, 2, true, false, true, false);
        iface = std::make_unique<InterfaceTab> (ctx, apply);
        libs = std::make_unique<LibraryTab> (ctx, std::move (libsChanged));
        keys = std::make_unique<juce::TextEditor>();
        keys->setMultiLine (true); keys->setReadOnly (true);
        keys->setText ("Space  Play / stop\nCtrl+Z / Ctrl+Y  Undo / redo\nCtrl+N / O / S  New / open / save\nCtrl+Shift+S  Save as\n"
                       "Ctrl+E  Export WAV\nF1-F7  Arrangement, Session, Channel Rack, Piano Roll, Mixer, Synth, Visualizer\n"
                       "Ctrl+L  Toggle loop     Ctrl+M  Toggle metronome\nEsc  Leave fullscreen visualizer\n\n"
                       "Shortcuts are fixed in this version (editable bindings are not implemented).");
        addTab ("Audio / MIDI", Theme::panel, audio.get(), false);
        addTab ("Interface", Theme::panel, iface.get(), false);
        addTab ("Libraries", Theme::panel, libs.get(), false);
        addTab ("Shortcuts", Theme::panel, keys.get(), false);
        setSize (640, 520);
    }
private:
    struct InterfaceTab : juce::Component
    {
        InterfaceTab (AppContext& c, std::function<void()> ap) : ctx (c), apply (std::move (ap))
        {
            auto addRow = [this] (const char* t, juce::Component& comp, juce::Label& l) { l.setText (t, juce::dontSendNotification); addAndMakeVisible (l); addAndMakeVisible (comp); };
            for (auto s : { "75%", "100%", "125%", "150%", "200%" }) scale.addItem (s, scale.getNumItems() + 1);
            for (auto s : { "2D (3D effects off)", "Low", "High" }) quality.addItem (s, quality.getNumItems() + 1);
            auto an = Settings::accentNames(); for (int i = 0; i < an.size(); ++i) accent.addItem (an[i], i + 1);
            for (int m : { 1, 2, 5, 10 }) autosave.addItem (juce::String (m) + " min", m);
            static const int sc[] = { 75, 100, 125, 150, 200 };
            for (int i = 0; i < 5; ++i) if (sc[i] == ctx.settings.scalePct) scale.setSelectedItemIndex (i, juce::dontSendNotification);
            quality.setSelectedId (ctx.settings.quality + 1, juce::dontSendNotification);
            accent.setSelectedId (ctx.settings.accentIndex + 1, juce::dontSendNotification);
            autosave.setSelectedId (ctx.settings.autosaveMinutes, juce::dontSendNotification);
            bpm.setRange (40, 240, 1); bpm.setValue (ctx.settings.defaultBpm, juce::dontSendNotification); bpm.setSliderStyle (juce::Slider::LinearBar);
            for (int n : { 3, 4, 5, 6, 7 }) ts.addItem (juce::String (n) + "/4", n);
            ts.setSelectedId (ctx.settings.defaultTimeSigNum, juce::dontSendNotification);
            addRow ("Interface scale", scale, l1); addRow ("3D / visual quality", quality, l2); addRow ("Accent theme", accent, l3);
            addRow ("Autosave interval", autosave, l4); addRow ("Default tempo (new projects)", bpm, l5); addRow ("Default time signature", ts, l6);
            auto onChange = [this]
            {
                static const int sc2[] = { 75, 100, 125, 150, 200 };
                ctx.settings.scalePct = sc2[juce::jlimit (0, 4, scale.getSelectedItemIndex())];
                ctx.settings.quality = quality.getSelectedId() - 1; ctx.settings.accentIndex = accent.getSelectedId() - 1;
                ctx.settings.autosaveMinutes = juce::jmax (1, autosave.getSelectedId());
                ctx.settings.defaultBpm = bpm.getValue(); ctx.settings.defaultTimeSigNum = ts.getSelectedId();
                ctx.settings.applyTheme(); ctx.settings.save(); apply();
            };
            for (auto* cb : { &scale, &quality, &accent, &autosave, &ts }) cb->onChange = onChange;
            bpm.onValueChange = onChange;
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (20);
            juce::Label* ls[] = { &l1, &l2, &l3, &l4, &l5, &l6 }; juce::Component* cs[] = { &scale, &quality, &accent, &autosave, &bpm, &ts };
            for (int i = 0; i < 6; ++i) { auto row = r.removeFromTop (30); ls[i]->setBounds (row.removeFromLeft (230)); cs[i]->setBounds (row.removeFromLeft (220)); r.removeFromTop (10); }
        }
        AppContext& ctx; std::function<void()> apply;
        juce::Label l1, l2, l3, l4, l5, l6; juce::ComboBox scale, quality, accent, autosave, ts; juce::Slider bpm;
    };
    struct LibraryTab : juce::Component
    {
        LibraryTab (AppContext& c, std::function<void()> ch)
            : sampleEd ("Sample library folders", c.settings.sampleDirs, [&c, ch] { c.settings.save(); ch(); }),
              pluginEd ("Extra VST3 plugin folders (default system folders are always scanned)", c.settings.pluginDirs, [&c] { c.settings.save(); })
        { addAndMakeVisible (sampleEd); addAndMakeVisible (pluginEd); }
        void resized() override { auto r = getLocalBounds().reduced (16); sampleEd.setBounds (r.removeFromTop (r.getHeight() / 2 - 8)); r.removeFromTop (16); pluginEd.setBounds (r); }
        DirListEditor sampleEd, pluginEd;
    };
    AppContext& ctx; std::function<void()> apply;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> audio; std::unique_ptr<InterfaceTab> iface; std::unique_ptr<LibraryTab> libs; std::unique_ptr<juce::TextEditor> keys;
};

//==============================================================================
// Main component
//==============================================================================
struct MainWindow::Impl : public juce::Component, public juce::DragAndDropContainer, public juce::MenuBarModel,
                          private juce::ChangeListener, private juce::Timer, private juce::KeyListener
{
    enum Cmd { New = 1, Open, Save, SaveAs, ImportAudio, Export, Quit, Undo, Redo, AddMidi, AddAudio, DelTrack, ViewBase = 20, Fullscreen = 40,
               Settings = 50, PluginMgr, About, Loop = 60, Metro, Recover = 70 };

    Impl (AppContext& c, MainWindow& w) : ctx (c), owner (w), browser (c), tabs (juce::TabbedButtonBar::TabsAtTop)
    {
        arr = std::make_unique<ArrangementView> (ctx); session = std::make_unique<SessionView> (ctx); rack = std::make_unique<ChannelRackView> (ctx);
        roll = std::make_unique<PianoRollView> (ctx); mixer = std::make_unique<MixerView> (ctx); synth = std::make_unique<SynthView> (ctx);
        vis = std::make_unique<VisualizerView> (ctx);
        vis->onFullscreen = [this] { toggleFullscreen(); };
        const char* names[] = { "ARRANGEMENT", "SESSION", "CHANNEL RACK", "PIANO ROLL", "MIXER", "SYNTH", "VISUALIZER" };
        juce::Component* views[] = { arr.get(), session.get(), rack.get(), roll.get(), mixer.get(), synth.get(), vis.get() };
        for (int i = 0; i < 7; ++i) tabs.addTab (names[i], Theme::panel, views[i], false);
        tabs.setTabBarDepth (32); tabs.setOutline (0);
        tabs.setColour (juce::TabbedComponent::backgroundColourId, Theme::bg);
        tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::tabTextColourId, Theme::dim);
        tabs.getTabbedButtonBar().setColour (juce::TabbedButtonBar::frontTextColourId, Theme::accent);
        addAndMakeVisible (tabs);

        sideTabs.addTab ("SAMPLES", Theme::panel, &browser, false);
        plugins = std::make_unique<PluginManagerComponent> (ctx);
        sideTabs.addTab ("PLUGINS", Theme::panel, plugins.get(), false);
        sideTabs.setTabBarDepth (28); sideTabs.setOutline (0);
        addAndMakeVisible (sideTabs);

        // transport
        auto mk = [this] (juce::TextButton& b, const char* t, bool toggle = false) { b.setButtonText (t); b.setClickingTogglesState (toggle); addAndMakeVisible (b); };
        mk (playBtn, "PLAY"); mk (stopBtn, "STOP"); mk (recBtn, "REC"); mk (loopBtn, "LOOP", true); mk (metroBtn, "METRO", true);
        mk (undoBtn, "UNDO"); mk (redoBtn, "REDO"); mk (fsBtn, "3D FX");
        playBtn.onClick = [this] { ctx.engine.togglePlay(); };
        stopBtn.onClick = [this] { stopAll(); };
        recBtn.onClick = [this] { toggleRecord(); };
        loopBtn.onClick = [this] { ctx.edit ([&] { ctx.project().loopEnabled = loopBtn.getToggleState(); }); };
        metroBtn.onClick = [this] { ctx.edit ([&] { ctx.project().metronome = metroBtn.getToggleState(); }); };
        undoBtn.onClick = [this] { if (ctx.engine.undo()) ctx.sendChangeMessage(); };
        redoBtn.onClick = [this] { if (ctx.engine.redo()) ctx.sendChangeMessage(); };
        fsBtn.onClick = [this]
        {
            ctx.settings.quality = (ctx.settings.quality + 1) % 3; ctx.settings.applyTheme(); ctx.settings.save();
            repaintAll();
        };
        bpm.setSliderStyle (juce::Slider::LinearBar); bpm.setColour (juce::Slider::trackColourId, Theme::accent.withAlpha (0.18f)); bpm.setRange (20, 300, 0.1); bpm.setTextValueSuffix (" BPM");
        bpm.onDragStart = [this] { ctx.beginEdit(); };
        bpm.onValueChange = [this] { ctx.project().bpm = bpm.getValue(); ctx.engine.setTempo (bpm.getValue()); ctx.touch(); };
        addAndMakeVisible (bpm);
        for (int n : { 2, 3, 4, 5, 6, 7 }) ts.addItem (juce::String (n) + "/4", n);
        ts.onChange = [this] { ctx.edit ([&] { ctx.project().timeSigNum = ts.getSelectedId(); ctx.project().timeSigDen = 4; }); };
        addAndMakeVisible (ts);
        for (auto* l : { &posLabel, &cpuLabel, &nameLabel }) { l->setColour (juce::Label::textColourId, Theme::text); addAndMakeVisible (l); }
        posLabel.setFont (Theme::font (22.f, true)); posLabel.setJustificationType (juce::Justification::centred);
        cpuLabel.setFont (Theme::font (12.f)); nameLabel.setFont (Theme::font (13.f));
        cpuLabel.setColour (juce::Label::textColourId, Theme::dim); nameLabel.setColour (juce::Label::textColourId, Theme::dim);
        masterMeter = std::make_unique<MeterBar> ();
        addAndMakeVisible (*masterMeter);

        ctx.addChangeListener (this);
        owner.addKeyListener (this); addKeyListener (this);
        setWantsKeyboardFocus (true);
        syncFromProject();
        setSize (1360, 820);
        startTimerHz (20);
        autosaveTimerMs = 0;
    }
    ~Impl() override
    {
        owner.removeKeyListener (this);
        ctx.removeChangeListener (this);
        stopTimer();
    }

    //-------------------------------------------------------------------------- layout
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::bg);
        if (! fullscreenViz)
        {
            auto bar = getLocalBounds().removeFromTop (barH).toFloat();
            if (Theme::quality > 0) g.setGradientFill (juce::ColourGradient (Theme::metalHi.withAlpha (0.55f), 0, 0, Theme::metalLo, 0, bar.getHeight(), false));
            else g.setColour (Theme::panel2);
            g.fillRect (bar);
            g.setColour (Theme::accent); g.fillRect (bar.removeFromBottom (2.f));
            g.setFont (Theme::font (24.f, true)); g.setColour (Theme::accent);
            g.drawText ("NOIZE", 14, 0, 100, barH - 2, juce::Justification::centredLeft);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds();
        if (fullscreenViz) { tabs.setBounds (r); sideTabs.setVisible (false); hideTransport (true); return; }
        hideTransport (false); sideTabs.setVisible (true);
        auto bar = r.removeFromTop (barH).reduced (8, 12); bar.removeFromLeft (100);
        for (auto* b : { &playBtn, &stopBtn, &recBtn }) { b->setBounds (bar.removeFromLeft (64)); bar.removeFromLeft (4); }
        bar.removeFromLeft (8);
        for (auto* b : { &loopBtn, &metroBtn }) { b->setBounds (bar.removeFromLeft (64)); bar.removeFromLeft (4); }
        bar.removeFromLeft (8); bpm.setBounds (bar.removeFromLeft (120)); bar.removeFromLeft (4); ts.setBounds (bar.removeFromLeft (56)); bar.removeFromLeft (10);
        posLabel.setBounds (bar.removeFromLeft (130)); bar.removeFromLeft (10);
        for (auto* b : { &undoBtn, &redoBtn, &fsBtn }) { b->setBounds (bar.removeFromLeft (60)); bar.removeFromLeft (4); }
        masterMeter->setBounds (bar.removeFromRight (90).reduced (0, 4)); bar.removeFromRight (6);
        cpuLabel.setBounds (bar.removeFromRight (84)); nameLabel.setBounds (bar);
        sideTabs.setBounds (r.removeFromRight (juce::jmin (300, r.getWidth() / 4)));
        tabs.setBounds (r);
    }
    void hideTransport (bool h)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &playBtn, &stopBtn, &recBtn, &loopBtn, &metroBtn, &undoBtn, &redoBtn, &fsBtn, &bpm, &ts, &posLabel, &cpuLabel, &nameLabel, masterMeter.get() })
            c->setVisible (! h);
    }
    void toggleFullscreen()
    {
        fullscreenViz = ! fullscreenViz;
        tabs.setCurrentTabIndex (6);
        tabs.setTabBarDepth (fullscreenViz ? 0 : 32);
        juce::Desktop::getInstance().setKioskModeComponent (fullscreenViz ? owner.getTopLevelComponent() : nullptr, false);
        owner.setMenuBar (fullscreenViz ? nullptr : this);
        resized(); repaint();
    }
    void repaintAll() { owner.repaint(); repaint(); for (auto* v : std::initializer_list<juce::Component*> { arr.get(), session.get(), rack.get(), roll.get(), mixer.get(), synth.get(), vis.get() }) v->repaint(); }
    void applyUiSettings()
    {
        juce::Desktop::getInstance().setGlobalScaleFactor (ctx.settings.scalePct / 100.0f);
        owner.getLookAndFeel().setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        repaintAll();
    }

    //-------------------------------------------------------------------------- transport helpers
    void stopAll()
    {
        if (ctx.engine.isRecording()) { ctx.engine.stopRecording(); ctx.finishSlotRecord(); ctx.sendChangeMessage(); }
        ctx.engine.stop(); ctx.engine.setPosition (0.0);
    }
    void toggleRecord()
    {
        if (ctx.engine.isRecording()) { ctx.engine.stopRecording(); ctx.finishSlotRecord(); ctx.sendChangeMessage(); return; }
        juce::String err;
        if (ctx.selectedTrack < 0 && ! ctx.project().tracks.empty()) ctx.selectedTrack = ctx.project().tracks.front().id;
        if (! ctx.engine.startRecording (ctx.selectedTrack, &err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot record", err.isEmpty() ? "Select a track to record into." : err);
    }
    void syncFromProject()
    {
        auto& p = ctx.project();
        bpm.setValue (p.bpm, juce::dontSendNotification);
        ts.setSelectedId (p.timeSigNum, juce::dontSendNotification);
        loopBtn.setToggleState (p.loopEnabled, juce::dontSendNotification);
        metroBtn.setToggleState (p.metronome, juce::dontSendNotification);
        nameLabel.setText (p.name + (ctx.engine.isDirty() ? " *" : "") + (ctx.engine.getCurrentFile().existsAsFile() ? "  -  " + ctx.engine.getCurrentFile().getFileName() : ""), juce::dontSendNotification);
        owner.setName ("NOIZE - " + p.name);
    }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { syncFromProject(); }
    void timerCallback() override
    {
        auto& p = ctx.project();
        ctx.engine.pollMidiRecording();
        posLabel.setText (AppContext::beatsToBBT (ctx.engine.getPosition(), p.timeSigNum, p.timeSigDen), juce::dontSendNotification);
        cpuLabel.setText ("CPU " + juce::String ((int) (ctx.engine.getCpuLoad() * 100.0)) + "%", juce::dontSendNotification);
        playBtn.setToggleState (ctx.engine.isPlaying(), juce::dontSendNotification);
        recBtn.setToggleState (ctx.engine.isRecording(), juce::dontSendNotification);
        undoBtn.setEnabled (ctx.engine.canUndo()); redoBtn.setEnabled (ctx.engine.canRedo());
        masterMeter->setLevel (ctx.engine.getRenderer().takeMasterPeak (0), ctx.engine.getRenderer().takeMasterPeak (1));
        if (auto* tour = std::getenv ("NOIZE_TOUR_DIR"))   // verification harness: snapshot every workspace, then quit
        {
            ++tourTick;
            if (tourTick % 30 == 0)
            {
                const int step = tourTick / 30 - 1;
                if (step >= 0 && step < 8)
                {
                    juce::File dir (tour); dir.createDirectory();
                    if (step == 7) { sideTabs.setCurrentTabIndex (1); }
                    auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
                    juce::PNGImageFormat png; juce::FileOutputStream fo (dir.getChildFile ("view" + juce::String (step) + ".png"));
                    if (fo.openedOk()) { fo.setPosition (0); fo.truncate(); png.writeImageToStream (img, fo); }
                    if (step < 6) tabs.setCurrentTabIndex (step + 1);
                    if (step < 7 && step >= 0 && step < 6) if (auto* v = dynamic_cast<noize::ViewBase*> (tabs.getCurrentContentComponent())) v->activated();
                }
                else juce::JUCEApplication::getInstance()->quit();
            }
        }
        autosaveTimerMs += 50;
        if (autosaveTimerMs >= ctx.settings.autosaveMinutes * 60000) { autosaveTimerMs = 0; if (ctx.engine.isDirty()) ctx.engine.autosave(); }
    }

    //-------------------------------------------------------------------------- file ops
    void confirmDiscard (std::function<void()> proceed)
    {
        if (! ctx.engine.isDirty()) { proceed(); return; }
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Unsaved changes")
            .withMessage ("Save changes to \"" + ctx.project().name + "\" first?").withButton ("Save", 1).withButton ("Discard", 2).withButton ("Cancel", 0),
            [this, proceed] (int r)
            {
                if (r == 1) { saveProject (false, proceed); }
                else if (r == 2) proceed();
            });
    }
    void newProject()
    {
        confirmDiscard ([this]
        {
            ctx.engine.stop(); ctx.engine.newProject();
            auto& p = ctx.project(); p.bpm = ctx.settings.defaultBpm; p.timeSigNum = ctx.settings.defaultTimeSigNum;
            ctx.selectedTrack = p.tracks.empty() ? -1 : p.tracks.front().id; ctx.target = {};
            ctx.touch(); syncFromProject();
        });
    }
    void openDialog()
    {
        confirmDiscard ([this]
        {
            chooser = std::make_shared<juce::FileChooser> ("Open NOIZE project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.noize");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) openProject (fc.getResult()); });
        });
    }
    void openProject (const juce::File& f)
    {
        juce::String err; ctx.engine.stop();
        if (! ctx.engine.openProject (f, &err)) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot open project", err); return; }
        ctx.selectedTrack = ctx.project().tracks.empty() ? -1 : ctx.project().tracks.front().id; ctx.target = {};
        ctx.sendChangeMessage(); syncFromProject();
        auto missing = ctx.project().findMissingAssets();
        if (! missing.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Missing audio files",
                "These audio files could not be found:\n\n" + missing.joinIntoString ("\n") + "\n\nAffected clips will be silent until the files are restored.");
    }
    void saveProject (bool as, std::function<void()> then = nullptr)
    {
        auto f = ctx.engine.getCurrentFile();
        if (! as && f != juce::File() && f.hasFileExtension ("noize")) { if (ctx.engine.saveProject (f)) { syncFromProject(); if (then) then(); } else failSave(); return; }
        chooser = std::make_shared<juce::FileChooser> ("Save NOIZE project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (ctx.project().name + ".noize"), "*.noize");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, then] (const juce::FileChooser& fc)
            {
                auto r = fc.getResult(); if (r == juce::File()) return;
                r = r.withFileExtension ("noize");
                ctx.project().name = r.getFileNameWithoutExtension();
                if (ctx.engine.saveProject (r)) { syncFromProject(); if (then) then(); } else failSave();
            });
    }
    void failSave() { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", "The project could not be written. Check the folder permissions and free disk space."); }

    void importAudio()
    {
        chooser = std::make_shared<juce::FileChooser> ("Import audio", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult(); if (! f.existsAsFile()) return;
            auto data = ctx.engine.getAssets().get (f.getFullPathName());
            if (data == nullptr) { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", "Could not decode " + f.getFileName()); return; }
            ctx.edit ([&]
            {
                auto* t = ctx.selTrack();
                if (t == nullptr || ! t->audioTrack) { auto& nt = ctx.project().addTrack (f.getFileNameWithoutExtension(), true); ctx.selectedTrack = nt.id; t = &nt; }
                Clip c; c.name = f.getFileNameWithoutExtension(); c.audioFile = f.getFullPathName(); c.startBeat = 0;
                c.lengthBeats = juce::jmax (1.0, data->buffer.getNumSamples() / data->sampleRate * ctx.project().bpm / 60.0);
                t->clips.push_back (c);
            });
        });
    }

    //-------------------------------------------------------------------------- export
    void exportDialog()
    {
        auto* w = new juce::AlertWindow ("Export WAV", "Offline render of the whole project.", juce::MessageBoxIconType::NoIcon);
        w->addComboBox ("sr", { "44100 Hz", "48000 Hz", "88200 Hz", "96000 Hz" }, "Sample rate");
        w->addComboBox ("bd", { "16-bit", "24-bit", "32-bit float" }, "Bit depth");
        w->addComboBox ("ch", { "Stereo", "Mono" }, "Channels");
        w->addComboBox ("rng", { "Whole project", "Loop region" }, "Range");
        w->getComboBoxComponent ("sr")->setSelectedItemIndex (0); w->getComboBoxComponent ("bd")->setSelectedItemIndex (1);
        w->getComboBoxComponent ("ch")->setSelectedItemIndex (0); w->getComboBoxComponent ("rng")->setSelectedItemIndex (0);
        w->addButton ("Export...", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
        {
            if (r != 1) return;
            static const double srs[] = { 44100, 48000, 88200, 96000 }; static const int bds[] = { 16, 24, 32 };
            ExportOptions o;
            o.sampleRate = srs[w->getComboBoxComponent ("sr")->getSelectedItemIndex()]; o.bitDepth = bds[w->getComboBoxComponent ("bd")->getSelectedItemIndex()];
            o.mono = w->getComboBoxComponent ("ch")->getSelectedItemIndex() == 1;
            if (w->getComboBoxComponent ("rng")->getSelectedItemIndex() == 1) { o.startBeat = ctx.project().loopStartBeat; o.endBeat = ctx.project().loopEndBeat; }
            chooser = std::make_shared<juce::FileChooser> ("Export WAV", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (ctx.project().name + ".wav"), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                [this, o] (const juce::FileChooser& fc) mutable { if (fc.getResult() != juce::File()) { o.file = fc.getResult().withFileExtension ("wav"); runExport (o); } });
        }), true);
    }
    void runExport (ExportOptions o)
    {
        auto* box = new juce::AlertWindow ("Rendering...", "Rendering " + o.file.getFileName(), juce::MessageBoxIconType::NoIcon);
        auto progress = std::make_shared<double> (0.0);
        box->addProgressBarComponent (*progress);
        box->addButton ("Cancel", 0);
        auto cancel = std::make_shared<std::atomic<bool>> (false);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([cancel] (int) { cancel->store (true); }), false);
        Project copy = ctx.project();
        juce::Component::SafePointer<juce::AlertWindow> safeBox (box);
        juce::Thread::launch ([this, copy, o, progress, cancel, safeBox]
        {
            auto res = renderProjectToWav (copy, o, [progress, cancel] (float p) { *progress = p; return ! cancel->load(); });
            juce::MessageManager::callAsync ([this, res, safeBox, cancel]
            {
                if (safeBox != nullptr) { cancel->store (true); safeBox->exitModalState (0); }
                juce::AlertWindow::showMessageBoxAsync (res.ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                    res.ok ? "Export complete" : "Export failed", res.message);
            });
        });
    }

    //-------------------------------------------------------------------------- settings / about
    void showSettings()
    {
        auto* comp = new SettingsComponent (ctx, [this] { browser.rescan(); }, [this] { applyUiSettings(); });
        juce::DialogWindow::LaunchOptions o; o.content.setOwned (comp); o.dialogTitle = "NOIZE Settings";
        o.dialogBackgroundColour = Theme::panel; o.escapeKeyTriggersCloseButton = true; o.useNativeTitleBar = true; o.resizable = false;
        o.launchAsync();
    }
    void showAbout()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "NOIZE " + juce::String (JUCE_APPLICATION_VERSION_STRING),
            "Free, offline digital audio workstation.\nBuilt with JUCE (AGPLv3). See LICENSES.md.\n\nNo accounts, no subscriptions, no cloud.");
    }

    //-------------------------------------------------------------------------- menus & keys
    juce::StringArray getMenuBarNames() override { return { "File", "Edit", "Track", "View", "Tools" }; }
    juce::PopupMenu getMenuForIndex (int idx, const juce::String&) override
    {
        juce::PopupMenu m;
        if (idx == 0)
        {
            m.addItem (New, "New Project\tCtrl+N"); m.addItem (Open, "Open...\tCtrl+O"); m.addSeparator();
            m.addItem (Save, "Save\tCtrl+S"); m.addItem (SaveAs, "Save As...\tCtrl+Shift+S"); m.addSeparator();
            m.addItem (ImportAudio, "Import Audio..."); m.addItem (Export, "Export WAV...\tCtrl+E"); m.addSeparator();
            m.addItem (Quit, "Quit");
        }
        else if (idx == 1)
        {
            m.addItem (Undo, "Undo\tCtrl+Z", ctx.engine.canUndo()); m.addItem (Redo, "Redo\tCtrl+Y", ctx.engine.canRedo()); m.addSeparator();
            m.addItem (Loop, "Toggle Loop\tCtrl+L"); m.addItem (Metro, "Toggle Metronome\tCtrl+M");
        }
        else if (idx == 2) { m.addItem (AddMidi, "Add MIDI Track"); m.addItem (AddAudio, "Add Audio Track"); m.addItem (DelTrack, "Delete Selected Track"); }
        else if (idx == 3)
        {
            const char* n[] = { "Arrangement", "Session", "Channel Rack", "Piano Roll", "Mixer", "Synth", "Visualizer" };
            for (int i = 0; i < 7; ++i) m.addItem (ViewBase + i, juce::String (n[i]) + "\tF" + juce::String (i + 1));
            m.addSeparator(); m.addItem (Fullscreen, "Fullscreen Visualizer");
        }
        else { m.addItem (Settings, "Settings..."); m.addItem (PluginMgr, "Plugin Manager"); m.addSeparator(); m.addItem (About, "About NOIZE"); }
        return m;
    }
    void menuItemSelected (int id, int) override { perform (id); }
    void perform (int id)
    {
        if (id >= ViewBase && id < ViewBase + 7) { tabs.setCurrentTabIndex (id - ViewBase); if (auto* v = dynamic_cast<noize::ViewBase*> (tabs.getCurrentContentComponent())) v->activated(); return; }
        switch (id)
        {
            case New: newProject(); break;
            case Open: openDialog(); break;
            case Save: saveProject (false); break;
            case SaveAs: saveProject (true); break;
            case ImportAudio: importAudio(); break;
            case Export: exportDialog(); break;
            case Quit: owner.closeButtonPressed(); break;
            case Undo: if (ctx.engine.undo()) ctx.sendChangeMessage(); break;
            case Redo: if (ctx.engine.redo()) ctx.sendChangeMessage(); break;
            case Loop: loopBtn.setToggleState (! loopBtn.getToggleState(), juce::dontSendNotification); ctx.edit ([&] { ctx.project().loopEnabled = loopBtn.getToggleState(); }); break;
            case Metro: metroBtn.setToggleState (! metroBtn.getToggleState(), juce::dontSendNotification); ctx.edit ([&] { ctx.project().metronome = metroBtn.getToggleState(); }); break;
            case AddMidi: ctx.edit ([&] { auto& t = ctx.project().addTrack ("Polysynth", false, InstrumentType::PolySynth); ctx.selectedTrack = t.id; }); break;
            case AddAudio: ctx.edit ([&] { auto& t = ctx.project().addTrack ("Audio " + juce::String (ctx.project().tracks.size() + 1), true); ctx.selectedTrack = t.id; }); break;
            case DelTrack: if (ctx.selectedTrack >= 0) { const int id2 = ctx.selectedTrack; ctx.edit ([&] { ctx.project().removeTrack (id2); }); ctx.selectedTrack = ctx.project().tracks.empty() ? -1 : ctx.project().tracks.front().id; ctx.target = {}; ctx.sendChangeMessage(); } break;
            case Fullscreen: toggleFullscreen(); break;
            case Settings: showSettings(); break;
            case PluginMgr: sideTabs.setCurrentTabIndex (1); break;
            case About: showAbout(); break;
            default: break;
        }
    }
    bool keyPressed (const juce::KeyPress& k, juce::Component* origin) override
    {
        if (dynamic_cast<juce::TextEditor*> (origin) != nullptr) return false;
        const auto m = k.getModifiers();
        if (k == juce::KeyPress::escapeKey && fullscreenViz) { toggleFullscreen(); return true; }
        if (k == juce::KeyPress::spaceKey && ! m.isAnyModifierKeyDown()) { ctx.engine.togglePlay(); return true; }
        for (int i = 0; i < 7; ++i) if (k == juce::KeyPress (juce::KeyPress::F1Key + i)) { perform (ViewBase + i); return true; }
        if (m.isCommandDown())
        {
            const auto c = k.getTextCharacter();
            if (c == 'z' || c == 'Z') { perform (Undo); return true; }
            if (c == 'y' || c == 'Y') { perform (Redo); return true; }
            if (c == 'n' || c == 'N') { perform (New); return true; }
            if (c == 'o' || c == 'O') { perform (Open); return true; }
            if (c == 's' || c == 'S') { perform (m.isShiftDown() ? SaveAs : Save); return true; }
            if (c == 'e' || c == 'E') { perform (Export); return true; }
            if (c == 'l' || c == 'L') { perform (Loop); return true; }
            if (c == 'm' || c == 'M') { perform (Metro); return true; }
        }
        return false;
    }

    AppContext& ctx; MainWindow& owner;
    SampleBrowser browser; std::unique_ptr<PluginManagerComponent> plugins;
    juce::TabbedComponent tabs, sideTabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<ArrangementView> arr; std::unique_ptr<SessionView> session; std::unique_ptr<ChannelRackView> rack;
    std::unique_ptr<PianoRollView> roll; std::unique_ptr<MixerView> mixer; std::unique_ptr<SynthView> synth; std::unique_ptr<VisualizerView> vis;
    juce::TextButton playBtn, stopBtn, recBtn, loopBtn, metroBtn, undoBtn, redoBtn, fsBtn;
    juce::Slider bpm; juce::ComboBox ts; juce::Label posLabel, cpuLabel, nameLabel;
    std::unique_ptr<MeterBar> masterMeter;
    std::shared_ptr<juce::FileChooser> chooser;
    bool fullscreenViz = false; int autosaveTimerMs = 0, tourTick = 0; static constexpr int barH = 54;
};

//==============================================================================
MainWindow::MainWindow (AppContext& ctx)
    : juce::DocumentWindow ("NOIZE", Theme::bg, juce::DocumentWindow::allButtons)
{
    static NoizeLookAndFeel laf;
    juce::LookAndFeel::setDefaultLookAndFeel (&laf);
    setUsingNativeTitleBar (true);
    const auto err = ctx.engine.initialiseAudio();
    if (ctx.project().tracks.empty()) ctx.engine.newProject();
    impl = std::make_unique<Impl> (ctx, *this);
    setContentNonOwned (impl.get(), true);
    setMenuBar (impl.get());
    setResizable (true, true); setResizeLimits (1000, 640, 4000, 3000);
    centreWithSize (1360, 820);
    setVisible (true);
    impl->applyUiSettings();
    impl->grabKeyboardFocus();
    if (err.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "No audio device",
            "NOIZE could not open an audio device:\n" + err + "\n\nYou can still edit projects and export. Pick a device in Tools > Settings.");
    if (Engine::hasRecovery())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Recover project?")
            .withMessage ("NOIZE did not close cleanly last time. Restore the autosaved project?").withButton ("Restore", 1).withButton ("Discard", 0),
            [this, &ctx] (int r)
            {
                if (r == 1)
                {
                    juce::String e;
                    if (ctx.engine.openProject (Engine::recoveryFile(), &e)) { ctx.engine.markDirty(); ctx.selectedTrack = ctx.project().tracks.empty() ? -1 : ctx.project().tracks.front().id; ctx.sendChangeMessage(); }
                }
                else ctx.engine.clearRecovery();
            });
    }
}

MainWindow::~MainWindow()
{
    setMenuBar (nullptr);
    clearContentComponent();
    impl.reset();
}

void MainWindow::openFile (const juce::File& f) { if (impl && f.existsAsFile()) impl->openProject (f); }

void MainWindow::closeButtonPressed()
{
    auto& ctx = impl->ctx;
    auto quit = [this, &ctx]
    {
        ctx.engine.stop(); ctx.engine.shutdownAudio(); ctx.engine.clearRecovery();
        juce::JUCEApplication::getInstance()->quit();
    };
    impl->confirmDiscard (quit);
}
}
