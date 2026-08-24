#include "ChannelStripPanel.h"
#include "MidiLearnHooks.h"

static juce::String tunerAddressFor (const juce::String& midiParamId)
{
    return midiParamId.fromFirstOccurrenceOf (":", false, false);
}

//==============================================================================
ChannelStripPanel::ChannelStripPanel (ChannelStrip& s, const juce::String& id)
    : strip (s), stripId (id)
{
    refresh();
}

ChannelStripPanel::~ChannelStripPanel()
{
}

//==============================================================================
void ChannelStripPanel::rebuildSlots()
{
    numSlots = kMaxVisibleSlots;

    for (int i = 0; i < kMaxVisibleSlots; ++i)
    {
        slots[i].index = i;

        // Slots are fixed positions: emptiness is a property of the slot, not
        // of how many plugins the strip happens to hold.
        if (const auto* entry = strip.getPluginEntry (i))
        {
            auto* proc = entry->processor.get();
            slots[i].name = (proc != nullptr) ? proc->getName() : "(unknown)";
            slots[i].bypassed = entry->bypassed;
            slots[i].empty = false;

            // Per-slot appearance lives on the chain entry. Legacy projects
            // stored it keyed by plugin name - fall back to that map when
            // the entry carries nothing, so old projects still display.
            slots[i].tintColour = entry->tint;
            slots[i].nickname   = entry->nickname;

            if (proc != nullptr && entry->nickname.isEmpty() && entry->tint.getARGB() == 0)
            {
                auto it = pluginAppearance.find (proc->getName());
                if (it != pluginAppearance.end())
                {
                    slots[i].tintColour = it->second.tint;
                    slots[i].nickname = it->second.nickname;
                }
            }
        }
        else
        {
            slots[i].name = "";
            slots[i].nickname = "";
            slots[i].bypassed = false;
            slots[i].empty = true;
            slots[i].tintColour = juce::Colour (0x00000000);
        }
    }
}

void ChannelStripPanel::refresh()
{
    rebuildSlots();
    repaint();
}

int ChannelStripPanel::getPreferredHeight() const
{
    return kMaxVisibleSlots * (kSlotHeight + kSlotPadding) + kSlotPadding;
}

//==============================================================================
void ChannelStripPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Recessed panel background - warm dark
    juce::ColourGradient panelBg (
        juce::Colour (0xff1a1816), bounds.getCentreX(), bounds.getY(),
        juce::Colour (0xff1c1a18), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill (panelBg);
    g.fillRoundedRectangle (bounds, 3.0f);

    // Inset bevel: dark top-left, light bottom-right
    g.setColour (juce::Colour (0xff080706));
    g.drawLine (bounds.getX(), bounds.getY(), bounds.getRight(), bounds.getY(), 1.0f);
    g.drawLine (bounds.getX(), bounds.getY(), bounds.getX(), bounds.getBottom(), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.drawLine (bounds.getRight() - 1.0f, bounds.getY() + 1.0f, bounds.getRight() - 1.0f, bounds.getBottom(), 1.0f);
    g.drawLine (bounds.getX() + 1.0f, bounds.getBottom() - 1.0f, bounds.getRight(), bounds.getBottom() - 1.0f, 1.0f);

    // Draw slots
    for (int i = 0; i < kMaxVisibleSlots; ++i)
    {
        int sy = kSlotPadding + i * (kSlotHeight + kSlotPadding);
        auto slotRect = juce::Rectangle<int> (4, sy, getWidth() - 8, kSlotHeight);

        auto fr = slotRect.toFloat();
        if (slots[i].empty)
        {
            // Empty slot = a recessed well: dark floor, top/left inner shadow,
            // bottom/right catch-light so it looks cut into the panel.
            g.setColour (juce::Colour (0xff0e0d0c));
            g.fillRoundedRectangle (fr, 3.0f);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.drawLine (fr.getX() + 2.0f, fr.getY() + 0.7f, fr.getRight() - 2.0f, fr.getY() + 0.7f, 1.3f);
            g.drawLine (fr.getX() + 0.7f, fr.getY() + 2.0f, fr.getX() + 0.7f, fr.getBottom() - 2.0f, 1.3f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawLine (fr.getX() + 2.0f, fr.getBottom() - 0.6f, fr.getRight() - 2.0f, fr.getBottom() - 0.6f, 1.0f);
            g.drawLine (fr.getRight() - 0.6f, fr.getY() + 2.0f, fr.getRight() - 0.6f, fr.getBottom() - 2.0f, 1.0f);

            g.setColour (juce::Colour (0xff3a3a3a));
            g.setFont (juce::Font (juce::FontOptions().withHeight (9.0f)));
            g.drawText ("Slot " + juce::String (i + 1), slotRect, juce::Justification::centred);
        }
        else
        {
            // Filled slot = a label card seated IN the well: dark recess border,
            // then a raised card with a top-lit gradient and bevel.
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (fr, 3.0f);

            auto card = fr.reduced (1.0f);
            juce::Colour base = (slots[i].tintColour.getAlpha() > 0)
                ? (slots[i].bypassed ? slots[i].tintColour.withMultipliedBrightness (0.3f)
                                     : slots[i].tintColour.withMultipliedBrightness (0.5f))
                : juce::Colour (0xff2a2a2a);
            juce::ColourGradient cg (base.brighter (0.18f), card.getCentreX(), card.getY(),
                                     base.darker (0.22f),    card.getCentreX(), card.getBottom(), false);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (card, 2.5f);
            g.setColour (juce::Colours::white.withAlpha (0.10f));   // top bevel
            g.drawLine (card.getX() + 2.0f, card.getY() + 0.7f, card.getRight() - 2.0f, card.getY() + 0.7f, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));   // bottom shadow
            g.drawLine (card.getX() + 2.0f, card.getBottom() - 0.6f, card.getRight() - 2.0f, card.getBottom() - 0.6f, 1.0f);

            auto drawRect = card.toNearestInt();

            // Bypass dot
            auto dotArea = drawRect.removeFromLeft (14);
            auto dotCentre = dotArea.getCentre().toFloat();
            g.setColour (slots[i].bypassed ? juce::Colour (0xff555555) : juce::Colour (0xff27ae60));
            g.fillEllipse (dotCentre.x - 3.0f, dotCentre.y - 3.0f, 6.0f, 6.0f);

            // Plugin name (or nickname)
            juce::String displayName = slots[i].nickname.isNotEmpty() ? slots[i].nickname : slots[i].name;
            g.setColour (slots[i].bypassed ? juce::Colour (0xff666666) : juce::Colour (0xffcccccc));
            g.setFont (juce::Font (juce::FontOptions().withHeight (11.0f)));
            g.drawText (displayName, drawRect.reduced (3, 0), juce::Justification::centredLeft, true);
        }
    }

    // Draw drag indicator
    if (dragging && dragTargetSlot >= 0 && dragSourceSlot >= 0 && dragTargetSlot != dragSourceSlot)
    {
        int indicatorY = kSlotPadding + dragTargetSlot * (kSlotHeight + kSlotPadding);
        if (dragTargetSlot > dragSourceSlot)
            indicatorY += kSlotHeight;

        g.setColour (juce::Colour (0xff4a90e2));
        g.fillRect (6, indicatorY - 1, getWidth() - 12, 3);

        // Small triangles on the ends
        juce::Path arrow;
        arrow.addTriangle (4.0f, (float)indicatorY - 4.0f,
                           4.0f, (float)indicatorY + 4.0f,
                           10.0f, (float)indicatorY);
        g.fillPath (arrow);
    }

    // Dim the source slot while dragging
    if (dragging && dragSourceSlot >= 0)
    {
        int sy = kSlotPadding + dragSourceSlot * (kSlotHeight + kSlotPadding);
        auto sourceRect = juce::Rectangle<int> (4, sy, getWidth() - 8, kSlotHeight);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (sourceRect.toFloat(), 3.0f);
    }
}

//==============================================================================
void ChannelStripPanel::resized()
{
    repaint();
}

//==============================================================================
int ChannelStripPanel::getSlotAt (int y) const
{
    for (int i = 0; i < kMaxVisibleSlots; ++i)
    {
        int sy = kSlotPadding + i * (kSlotHeight + kSlotPadding);
        if (y >= sy && y < sy + kSlotHeight)
            return i;
    }
    return -1;
}

void ChannelStripPanel::mouseDown (const juce::MouseEvent& e)
{
    doubleClicked = false;

    int slotIndex = getSlotAt (e.y);
    if (slotIndex < 0) return;

    if (e.mods.isRightButtonDown())
    {
        showSlotContextMenu (slotIndex);
        return;
    }

    if (slots[slotIndex].empty)
    {
        // Clicking an empty slot fills that slot, not wherever the chain ends.
        if (onAddPluginClicked) onAddPluginClicked (slotIndex);
    }
    else
    {
        dragSourceSlot = slotIndex;
        dragging = false;
    }
}

void ChannelStripPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    int slotIndex = getSlotAt (e.y);
    if (slotIndex < 0 || slots[slotIndex].empty) return;

    doubleClicked = true;
    strip.openPluginEditor (slotIndex);
}

void ChannelStripPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragSourceSlot < 0 || slots[dragSourceSlot].empty) return;

    if (! dragging && e.getDistanceFromDragStart() > 5)
        dragging = true;

    if (dragging)
    {
        int target = getSlotAt (e.y);
        if (target != dragTargetSlot)
        {
            dragTargetSlot = target;
            repaint();
        }
    }
}

void ChannelStripPanel::mouseUp (const juce::MouseEvent& e)
{
    if (dragging && dragSourceSlot >= 0 && dragTargetSlot >= 0
        && dragSourceSlot != dragTargetSlot
        && ! strip.isSlotEmpty (dragSourceSlot))
    {
        // Swap the two slots. Dropping onto an empty slot moves there; dropping
        // onto an occupied one trades places. Nothing else in the rack moves.
        strip.swapSlots (dragSourceSlot, dragTargetSlot);
        refresh();
    }
    else if (! dragging && ! doubleClicked && dragSourceSlot >= 0 && ! slots[dragSourceSlot].empty)
    {
        bool bp = strip.isPluginBypassed (dragSourceSlot);
        strip.setPluginBypassed (dragSourceSlot, ! bp);
        refresh();
    }

    dragSourceSlot = -1;
    dragTargetSlot = -1;
    dragging = false;
    repaint();
}

void ChannelStripPanel::showSlotContextMenu (int slotIndex)
{
    juce::PopupMenu menu;
    bool hasPlugin = slotIndex >= 0 && ! strip.isSlotEmpty (slotIndex);
    auto& clipboard = getClipboard();
    bool clipboardHasData = clipboard.pluginIdentifier.isNotEmpty();

    if (hasPlugin)
    {
        menu.addItem (1, "Open Editor");
        menu.addItem (6, "Nickname...");
        menu.addSeparator();

        bool bypassed = strip.isPluginBypassed (slotIndex);
        menu.addItem (2, bypassed ? "Enable" : "Bypass");
        menu.addSeparator();

        menu.addItem (10, "Copy");
        if (clipboardHasData)
        {
            auto* proc = strip.getPlugin (slotIndex);
            bool samePlugin = proc != nullptr && clipboard.pluginIdentifier == slots[slotIndex].name;
            menu.addItem (11, samePlugin ? "Paste Settings" : "Paste (Replace)");
        }
        menu.addSeparator();

        juce::PopupMenu tintMenu;
        tintMenu.addItem (20, "None");
        tintMenu.addItem (21, "Red");
        tintMenu.addItem (22, "Green");
        tintMenu.addItem (23, "Blue");
        tintMenu.addItem (24, "Purple");
        tintMenu.addItem (25, "Orange");
        tintMenu.addItem (26, "Teal");
        tintMenu.addItem (27, "Yellow");
        menu.addSubMenu ("Tint Colour", tintMenu);

        menu.addSeparator();
        menu.addItem (3, "Move Up", slotIndex > 0);
        menu.addItem (4, "Move Down", slotIndex < strip.getNumSlots() - 1);
        menu.addSeparator();
        menu.addItem (5, "Remove");
        addSlotMidiItems (menu, slotIndex);
        addTunerItem (menu, slotIndex);
    }
    else
    {
        if (clipboardHasData)
            menu.addItem (11, "Paste");
        menu.addItem (12, "Add Plugin...");
        menu.addSeparator();
        menu.addItem (13, "Add NAM Amp");
        menu.addItem (14, "Add NAM Pedal");
        menu.addItem (15, "Add Cab IR");
        menu.addItem (16, "Add Space IR");
        addSlotMidiItems (menu, slotIndex);
    }

    menu.showMenuAsync ({}, [this, slotIndex, hasPlugin] (int result)
    {
        if (result <= 0) return;

        auto storeAppearance = [this, slotIndex] ()
        {
            if (strip.isSlotEmpty (slotIndex)) return;
            strip.setPluginAppearance (slotIndex, slots[slotIndex].tintColour,
                                       slots[slotIndex].nickname);
            // Retire any legacy name-keyed value so it can't shadow the
            // per-slot one on the next rebuild.
            if (auto* proc = strip.getPlugin (slotIndex))
                pluginAppearance.erase (proc->getName());
        };

        switch (result)
        {
            case 1: strip.openPluginEditor (slotIndex); break;
            case 2:
            {
                bool bp = strip.isPluginBypassed (slotIndex);
                strip.setPluginBypassed (slotIndex, ! bp);
                refresh();
                break;
            }
            case 3:
                strip.swapSlots (slotIndex, slotIndex - 1);
                refresh();
                break;
            case 4:
                strip.swapSlots (slotIndex, slotIndex + 1);
                refresh();
                break;
            case 5:
                strip.removePlugin (slotIndex);
                refresh();
                break;
            case 6:
                showNicknameDialog (slotIndex);
                break;

            case 10:
            {
                auto* proc = strip.getPlugin (slotIndex);
                if (proc != nullptr)
                {
                    auto& cb = getClipboard();
                    cb.pluginIdentifier = slots[slotIndex].name;
                    cb.pluginName = proc->getName();
                    cb.stateData.reset();
                    proc->getStateInformation (cb.stateData);
                    cb.bypassed = strip.isPluginBypassed (slotIndex);

                    if (const auto* entry = strip.getPluginEntry (slotIndex))
                        cb.pluginIdentifier = entry->identifier;
                }
                break;
            }

            case 11:
            {
                auto& cb = getClipboard();
                if (hasPlugin)
                {
                    const auto* entry = strip.getPluginEntry (slotIndex);
                    if (entry != nullptr && entry->identifier == cb.pluginIdentifier)
                    {
                        auto* proc = strip.getPlugin (slotIndex);
                        if (proc != nullptr && cb.stateData.getSize() > 0)
                            proc->setStateInformation (cb.stateData.getData(), (int) cb.stateData.getSize());
                    }
                    else
                    {
                        if (onPastePlugin)
                            onPastePlugin (cb.pluginIdentifier, cb.stateData, cb.bypassed, slotIndex);
                    }
                }
                else
                {
                    if (onPastePlugin)
                        onPastePlugin (cb.pluginIdentifier, cb.stateData, cb.bypassed, slotIndex);
                }
                refresh();
                break;
            }

            case 12:
                if (onAddPluginClicked) onAddPluginClicked (slotIndex);
                break;

            case 30:
                if (MidiLearnHooks::beginLearn)
                    MidiLearnHooks::beginLearn (slotBypassParamId (stripId, slotIndex), 0.0f, 1.0f);
                break;

            case 31:
                if (MidiLearnHooks::clearBinding)
                    MidiLearnHooks::clearBinding (slotBypassParamId (stripId, slotIndex));
                break;

            case 32: case 33: case 34:
                if (MidiLearnHooks::setSwitchType)
                    MidiLearnHooks::setSwitchType (slotBypassParamId (stripId, slotIndex),
                                                   result - 32);
                break;

            case 40:
                if (MidiLearnHooks::setTunerSlot)
                {
                    // Ticked already means "un-mark"; otherwise mark this slot.
                    const auto addr = tunerAddressFor (slotBypassParamId (stripId, slotIndex));
                    const bool marked = MidiLearnHooks::getTunerSlot
                                     && MidiLearnHooks::getTunerSlot() == addr;
                    MidiLearnHooks::setTunerSlot (marked ? juce::String() : addr);
                }
                break;

            case 13: case 14: case 15: case 16:
                if (onAddInternalRow) onAddInternalRow (result - 13, slotIndex);
                break;

            case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27:
            {
                juce::Colour tint;
                switch (result)
                {
                    case 20: tint = juce::Colour (0x00000000); break;
                    case 21: tint = juce::Colour (0xffcc3333); break;
                    case 22: tint = juce::Colour (0xff33cc33); break;
                    case 23: tint = juce::Colour (0xff3366cc); break;
                    case 24: tint = juce::Colour (0xff9933cc); break;
                    case 25: tint = juce::Colour (0xffcc8833); break;
                    case 26: tint = juce::Colour (0xff33ccaa); break;
                    case 27: tint = juce::Colour (0xffcccc33); break;
                }

                slots[slotIndex].tintColour = tint;
                storeAppearance();
                repaint();
                break;
            }
            default: break;
        }
    });
}

void ChannelStripPanel::addSlotMidiItems (juce::PopupMenu& menu, int slotIndex) const
{
    if (! MidiLearnHooks::beginLearn || stripId.isEmpty()) return;

    const auto pid = slotBypassParamId (stripId, slotIndex);
    const int  cc  = MidiLearnHooks::getCc ? MidiLearnHooks::getCc (pid) : -1;

    menu.addSeparator();
    menu.addItem (30, cc >= 0 ? "Learn MIDI (re-learn, now CC " + juce::String (cc) + ")"
                              : "Learn MIDI");
    menu.addItem (31, "Clear MIDI binding", cc >= 0);

    if (cc >= 0)
    {
        // Latching and Momentary send identical CC streams, so the right one
        // can't be detected - it has to be picked. Latching is the default.
        const int type = MidiLearnHooks::getSwitchType
                       ? MidiLearnHooks::getSwitchType (pid) : 0;
        juce::PopupMenu modeMenu;
        modeMenu.addItem (32, "Latching (alternates 0 / 127)",        true, type == 0);
        modeMenu.addItem (33, "Momentary (127 held, 0 released)",     true, type == 1);
        modeMenu.addItem (34, "Single value (same value each press)", true, type == 2);
        menu.addSubMenu ("MIDI Switch Type", modeMenu);
    }
}

juce::Array<PluginAppearanceState> ChannelStripPanel::getAppearances() const
{
    juce::Array<PluginAppearanceState> result;
    for (const auto& [name, app] : pluginAppearance)
    {
        PluginAppearanceState pas;
        pas.pluginName = name;
        pas.tint       = app.tint;
        pas.nickname   = app.nickname;
        result.add (pas);
    }
    return result;
}

void ChannelStripPanel::setAppearances (const juce::Array<PluginAppearanceState>& appearances)
{
    pluginAppearance.clear();
    for (const auto& pas : appearances)
    {
        PluginAppearance a;
        a.tint     = pas.tint;
        a.nickname = pas.nickname;
        pluginAppearance[pas.pluginName] = a;
    }
    refresh();
}

void ChannelStripPanel::showNicknameDialog (int slotIndex)
{
    if (slotIndex < 0 || strip.isSlotEmpty (slotIndex)) return;

    auto* aw = new juce::AlertWindow ("Plugin Nickname",
                                       "Enter a display name for this plugin.\nLeave blank to use the original name.",
                                       juce::AlertWindow::NoIcon);

    aw->addTextEditor ("nickname", slots[slotIndex].nickname, "Nickname:");
    aw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, slotIndex, aw] (int result)
        {
            if (result == 1 && ! strip.isSlotEmpty (slotIndex))
            {
                auto nick = aw->getTextEditorContents ("nickname").trim();
                slots[slotIndex].nickname = nick;
                strip.setPluginAppearance (slotIndex, slots[slotIndex].tintColour, nick);
                if (auto* proc = strip.getPlugin (slotIndex))
                    pluginAppearance.erase (proc->getName());
                repaint();
            }
            delete aw;
        }), true);
}

void ChannelStripPanel::addTunerItem (juce::PopupMenu& menu, int slotIndex) const
{
    if (! MidiLearnHooks::setTunerSlot) return;

    const auto addr = tunerAddressFor (slotBypassParamId (stripId, slotIndex));
    const bool marked = MidiLearnHooks::getTunerSlot
                     && MidiLearnHooks::getTunerSlot() == addr;

    menu.addSeparator();
    menu.addItem (40, "Use as Tuner", true, marked);
}
