/*
  * Copyright (C) 2011 Cameron White
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "systemrenderer.h"

#include <QFontMetricsF>

#include <boost/foreach.hpp>

#include <powertabdocument/rect.h>
#include <powertabdocument/system.h>
#include <powertabdocument/staff.h>
#include <powertabdocument/barline.h>
#include <powertabdocument/position.h>
#include <powertabdocument/score.h>
#include <powertabdocument/alternateending.h>
#include <powertabdocument/tempomarker.h>
#include <powertabdocument/chordtext.h>
#include <powertabdocument/direction.h>
#include <powertabdocument/rhythmslash.h>
#include <powertabdocument/layout.h>
#include <powertabdocument/dynamic.h>
#include <powertabdocument/guitar.h>

#include <app/common.h>

#include "barlinepainter.h"
#include "systempainter.h"
#include "staffdata.h"
#include "staffpainter.h"
#include "clefpainter.h"
#include "keysignaturepainter.h"
#include "timesignaturepainter.h"
#include "tabnotepainter.h"
#include "tempomarkerpainter.h"
#include "chordtextpainter.h"
#include "directionpainter.h"
#include "rhythmslashpainter.h"
#include "tremolobarpainter.h"
#include "stdnotationpainter.h"
#include "notestem.h"
#include "restpainter.h"
#include "beamgroup.h"
#include "irregularnotegroup.h"
#include "caret.h"

#include <app/scorearea.h>

using boost::shared_ptr;

SystemRenderer::SystemRenderer(const Score* score, const int lineSpacing) :
    score(score), lineSpacing(lineSpacing),
    parentSystem(NULL)
{
}

QGraphicsItem* SystemRenderer::operator()(boost::shared_ptr<const System> system)
{
    this->system = system;

    const Rect& systemRectangle = system->GetRect();

    // save the top-left position of the system
    const int leftEdge = systemRectangle.GetLeft();
    const int topEdge = systemRectangle.GetTop();
    const int width = systemRectangle.GetWidth();

    // draw system rectangle
    SystemPainter* sysPainter = new SystemPainter(system);
    sysPainter->setPos(leftEdge, topEdge);
    parentSystem = sysPainter;

    // Draw each staff
    for (uint32_t i = 0; i < system->GetStaffCount(); i++)
    {
        StaffData currentStaffInfo;
        currentStaffInfo.positionWidth = system->GetPositionSpacing();

        staff = system->GetStaff(i);

        // Populate the staff info structure with information from the given staff
        currentStaffInfo.leftEdge = leftEdge;
        currentStaffInfo.numOfStrings = staff->GetTablatureStaffType();
        currentStaffInfo.stdNotationStaffAboveSpacing = staff->GetStandardNotationStaffAboveSpacing();
        currentStaffInfo.stdNotationStaffBelowSpacing = staff->GetStandardNotationStaffBelowSpacing();
        currentStaffInfo.symbolSpacing = staff->GetSymbolSpacing();
        currentStaffInfo.tabLineSpacing = lineSpacing;
        currentStaffInfo.tabStaffBelowSpacing = staff->GetTablatureStaffBelowSpacing();
        currentStaffInfo.topEdge = system->GetStaffHeightOffset(i, true);
        currentStaffInfo.width = width;
        currentStaffInfo.calculateHeight();

        // Draw the staff lines
        StaffPainter* staffPainter = new StaffPainter(system, staff, currentStaffInfo);
        staffPainters.push_back(staffPainter);

        staffPainter->setPos(0, system->GetStaffHeightOffset(i));
        staffPainter->setParentItem(parentSystem);
        parentStaff = staffPainter;

        // Draw the clefs
        ClefPainter* clefPainter = new ClefPainter(currentStaffInfo, staff);
        clefPainter->setPos(System::CLEF_PADDING, currentStaffInfo.getTopStdNotationLine());
        clefPainter->setParentItem(parentStaff);

        drawTabClef(System::CLEF_PADDING, currentStaffInfo);

        renderBars(currentStaffInfo);

        drawTabNotes(currentStaffInfo);
        drawStdNotation(currentStaffInfo);

        if (i == 0)
        {
            drawSystemSymbols(currentStaffInfo);
            drawRhythmSlashes();
        }

        drawLegato(currentStaffInfo);
        drawSlides(currentStaffInfo);
        drawSymbols(currentStaffInfo);
        drawSymbolsBelowTabStaff(currentStaffInfo);
    }

    parentSystem = NULL;
    parentStaff = NULL;
    system.reset();
    staff.reset();

    return sysPainter;
}

/// Draws the tab clef
void SystemRenderer::drawTabClef(int x, const StaffData& staffInfo)
{
    QGraphicsSimpleTextItem* tabClef = new QGraphicsSimpleTextItem;
    tabClef->setPos(x, staffInfo.getTopTabLine() - 12);
    musicFont.setSymbol(tabClef, MusicFont::TabClef, staffInfo.numOfStrings * 4.25);
    tabClef->setParentItem(parentStaff);
}

/// Draw all of the barlines for the staff.
void SystemRenderer::renderBars(const StaffData& currentStaffInfo) const
{
    std::vector<System::BarlineConstPtr> barlines;
    system->GetBarlines(barlines);

    for (size_t i = 0; i < barlines.size(); i++)
    {
        const System::BarlineConstPtr& currentBarline = barlines[i];
        const KeySignature& keySig = currentBarline->GetKeySignature();
        const TimeSignature& timeSig = currentBarline->GetTimeSignature();

        BarlinePainter* barlinePainter = new BarlinePainter(currentStaffInfo, currentBarline);
        barlinePainters.push_back(barlinePainter);

        double x = system->GetPositionX(currentBarline->GetPosition());
        double keySigX = x + barlinePainter->boundingRect().width() - 1;
        double timeSigX = x + barlinePainter->boundingRect().width() + keySig.GetWidth();
        double rehearsalSignX = x;

        if (i == 0) // start bar of system
        {
            if (currentBarline->IsBar()) // for normal bars, display a line at the far left edge
            {
                x = 0 - barlinePainter->boundingRect().width() / 2 - 0.5;
            }
            else // otherwise, display the bar after the clef, etc, and to the left of the first note
            {
                x = system->GetFirstPositionX() - currentStaffInfo.positionWidth;
            }

            keySigX = System::CLEF_WIDTH;
            timeSigX = System::CLEF_WIDTH + keySig.GetWidth();
        }
        else if (i == barlines.size() - 1) // last barline of system
        {
            x = system->GetRect().GetWidth() - barlinePainter->boundingRect().width() / 2 - 1;

            if (currentBarline->IsRepeatEnd())
            {
                x -= 6;     // bit of a positioning hack
            }
            else if (currentBarline->IsBar())
            {
                x += 0.5;
            }
        }

        barlinePainter->setPos(x, 0);
        barlinePainter->setParentItem(parentStaff);

        if (keySig.IsShown())
        {
            KeySignaturePainter* keySigPainter = new KeySignaturePainter(currentStaffInfo, keySig,
                                                                         currentBarline->GetPosition());
            keySignaturePainters.push_back(keySigPainter);

            keySigPainter->setPos(keySigX, currentStaffInfo.getTopStdNotationLine());
            keySigPainter->setParentItem(parentStaff);
        }

        if (timeSig.IsShown())
        {
            TimeSignaturePainter* timeSigPainter = new TimeSignaturePainter(currentStaffInfo, timeSig);
            timeSigPainter->setPos(timeSigX, currentStaffInfo.getTopStdNotationLine());
            timeSigPainter->setParentItem(parentStaff);
        }

        const RehearsalSign& rehearsalSign = currentBarline->GetRehearsalSign();
        if (rehearsalSign.IsSet())
        {
            const int y = 1;
            QFont displayFont("Helvetica");
            displayFont.setPixelSize(12);

            QGraphicsSimpleTextItem* signLetter = new QGraphicsSimpleTextItem;
            signLetter->setText(QString(QChar(rehearsalSign.GetLetter())));
            signLetter->setPos(rehearsalSignX, y);
            signLetter->setFont(displayFont);

            QGraphicsSimpleTextItem* signText = new QGraphicsSimpleTextItem;
            signText->setText(QString::fromStdString(rehearsalSign.GetDescription()));
            signText->setFont(displayFont);
            signText->setPos(rehearsalSignX + signLetter->boundingRect().width() + 7, y);

            // draw rectangle around rehearsal sign letter
            QRectF boundingRect = signLetter->boundingRect();
            boundingRect.setWidth(boundingRect.width() + 7);
            boundingRect.translate(-4, 0);
            QGraphicsRectItem* rect = new QGraphicsRectItem(boundingRect);
            rect->setPos(rehearsalSignX, y);

            rect->setParentItem(parentSystem);
            signText->setParentItem(parentSystem);
            signLetter->setParentItem(parentSystem);
        }
    }
}

/// Draws the tab notes for all notes (and voices) in the staff
void SystemRenderer::drawTabNotes(const StaffData& currentStaffInfo) const
{
    for (quint32 voice = 0; voice < Staff::NUM_STAFF_VOICES; voice++)
    {
        for (quint32 i=0; i < staff->GetPositionCount(voice); i++)
        {
            const Position* currentPosition = staff->GetPosition(voice, i);
            const quint32 location = system->GetPositionX(currentPosition->GetPosition());

            for (quint32 j=0; j < currentPosition->GetNoteCount(); j++)
            {
                Note* note = currentPosition->GetNote(j);

                TabNotePainter* tabNote = new TabNotePainter(note);
                centerItem(tabNote, location, location + currentStaffInfo.positionWidth,
                           currentStaffInfo.getTabLineHeight(note->GetString()) + currentStaffInfo.tabLineSpacing / 2 - 1);
                tabNote->setParentItem(parentStaff);
            }

            // draw arpeggios if necessary
            if (currentPosition->HasArpeggioDown() || currentPosition->HasArpeggioUp())
            {
                drawArpeggio(currentPosition, location, currentStaffInfo);
            }
        }
    }
}

void SystemRenderer::drawArpeggio(const Position* position, quint32 x,
                                  const StaffData& currentStaffInfo) const
{
    // get the highest and lowest strings used at this position, and
    // convert the string indices to positions on the staff
    std::pair<quint8, quint8> bounds = position->GetStringBounds();
    bounds.first = currentStaffInfo.getTabLineHeight(bounds.first + 1);
    bounds.second = currentStaffInfo.getTabLineHeight(bounds.second + 1);

    const uint8_t height = bounds.second - bounds.first;

    // take a vibrato segment, spanning the distance from top to bottom note, and then rotate it by 90 degrees
    const QFont& font = musicFont.getFont();
    const QChar arpeggioSymbol = musicFont.getSymbol(MusicFont::Vibrato);
    const double symbolWidth = QFontMetricsF(font).width(arpeggioSymbol);
    const int numSymbols = height / symbolWidth;

    QGraphicsSimpleTextItem* arpeggio = new QGraphicsSimpleTextItem(QString(numSymbols, arpeggioSymbol));
    arpeggio->setFont(font);
    arpeggio->setPos(x + arpeggio->boundingRect().height() / 2.0 - 3.0, bounds.first);
    arpeggio->setRotation(90);
    arpeggio->setParentItem(parentStaff);

    // draw the end of the arpeggio
    const QChar arpeggioEnd = position->HasArpeggioUp() ? musicFont.getSymbol(MusicFont::ArpeggioUp) :
                                                          musicFont.getSymbol(MusicFont::ArpeggioDown);

    QGraphicsSimpleTextItem* endPoint = new QGraphicsSimpleTextItem(arpeggioEnd);
    const double y = position->HasArpeggioUp() ? bounds.first : bounds.second;
    endPoint->setFont(font);
    endPoint->setPos(x, y - 1.45 * font.pixelSize());
    endPoint->setParentItem(parentStaff);
}

void SystemRenderer::centerItem(QGraphicsItem *item, float xmin, float xmax, float y) const
{
    float itemWidth = item->boundingRect().width();
    float centredX = xmin + ((xmax - (xmin + itemWidth)) / 2);
    item->setPos(centredX, y);
}

void SystemRenderer::drawSystemSymbols(const StaffData& currentStaffInfo) const
{
    quint32 height = 0;

    if (system->HasRehearsalSign()) // rehearsal signs are drawn at the same time as barlines
    {
        height += System::SYSTEM_SYMBOL_SPACING;
        drawDividerLine(currentStaffInfo, height);
    }

    std::vector<Score::AlternateEndingPtr> altEndings;
    score->GetAlternateEndingsInSystem(altEndings, system);

    if (altEndings.size() > 0)
    {
        drawAltEndings(altEndings, height);
        height += System::SYSTEM_SYMBOL_SPACING;
        drawDividerLine(currentStaffInfo, height);
    }

    std::vector<Score::TempoMarkerPtr> tempoMarkers;
    score->GetTempoMarkersInSystem(tempoMarkers, system);

    if (tempoMarkers.size() > 0)
    {
        drawTempoMarkers(tempoMarkers, height);
        height += System::SYSTEM_SYMBOL_SPACING;
        drawDividerLine(currentStaffInfo, height);
    }

    if (system->GetDirectionCount() > 0)
    {
        drawDirections(height, currentStaffInfo);
        height += System::SYSTEM_SYMBOL_SPACING * system->MaxDirectionSymbolCount();
        drawDividerLine(currentStaffInfo, height);
    }

    if (system->GetChordTextCount() > 0)
    {
        drawChordText(height, currentStaffInfo);
        height += System::SYSTEM_SYMBOL_SPACING;
        drawDividerLine(currentStaffInfo, height);
    }
}

void SystemRenderer::drawDividerLine(const StaffData& currentStaffInfo, quint32 y) const
{
    QGraphicsLineItem* line = new QGraphicsLineItem;
    line->setLine(0, y, currentStaffInfo.width, y);
    line->setOpacity(0.5);
    line->setPen(QPen(Qt::black, 0.5, Qt::DashLine));

    line->setParentItem(parentSystem);
}


void SystemRenderer::drawAltEndings(const std::vector<Score::AlternateEndingPtr>& altEndings,
                                    uint32_t height) const
{
    QFont displayFont("Liberation Sans");
    displayFont.setPixelSize(10);

    const double TOP_LINE_OFFSET = 2;
    const double TEXT_PADDING = 5;

    BOOST_FOREACH(Score::AlternateEndingConstPtr altEnding, altEndings)
    {
        const uint32_t location = system->GetPositionX(altEnding->GetPosition());

        // draw the vertical line
        QGraphicsLineItem* vertLine = new QGraphicsLineItem;
        vertLine->setLine(0, TOP_LINE_OFFSET, 0, System::SYSTEM_SYMBOL_SPACING - TOP_LINE_OFFSET);
        vertLine->setPos(location, height);
        vertLine->setParentItem(parentSystem);

        // draw the text indicating the repeat numbers
        const std::string repeatText = altEnding->GetText();

        QGraphicsSimpleTextItem* text = new QGraphicsSimpleTextItem(QString::fromStdString(repeatText));
        text->setFont(displayFont);
        text->setPos(location + TEXT_PADDING, height + TEXT_PADDING / 2.0);
        text->setParentItem(parentSystem);

        // the horizontal line either stretches to the next repeat end bar in the system,
        // or just to the next bar
        std::vector<System::BarlineConstPtr> barlines;
        system->GetBarlines(barlines);

        double endX = 0;
        BOOST_FOREACH(System::BarlineConstPtr barline, barlines)
        {
            // look for the next repeat end bar
            if (barline->GetPosition() > altEnding->GetPosition() && barline->IsRepeatEnd())
            {
                endX = system->GetPositionX(barline->GetPosition());
            }
        }
        // otherwise, if there is no next repeat bar, just go to the next barline
        if (endX == 0)
        {
            endX = system->GetPositionX(system->GetNextBarline(altEnding->GetPosition())->GetPosition());
        }

        // ensure that the line doesn't extend past the edge of the system
        endX = clamp(endX, 0.0, static_cast<double>(system->GetRect().GetWidth()));

        QGraphicsLineItem* horizLine = new QGraphicsLineItem;
        horizLine->setLine(0, TOP_LINE_OFFSET, endX - location, TOP_LINE_OFFSET);
        horizLine->setPos(location, height);
        horizLine->setParentItem(parentSystem);
    }
}

void SystemRenderer::drawTempoMarkers(const std::vector<Score::TempoMarkerPtr>& tempoMarkers,
                                      quint32 height) const
{
    BOOST_FOREACH(Score::TempoMarkerPtr tempoMarker, tempoMarkers)
    {
        const quint32 location = system->GetPositionX(tempoMarker->GetPosition());

        TempoMarkerPainter* tempoMarkerPainter = new TempoMarkerPainter(tempoMarker);
        tempoMarkerPainter->setPos(location, height + 4);
        tempoMarkerPainter->setParentItem(parentSystem);
    }
}

void SystemRenderer::drawChordText(uint32_t height, const StaffData& currentStaffInfo) const
{
    for (uint32_t i = 0; i < system->GetChordTextCount(); i++)
    {
        shared_ptr<const ChordText> chordText = system->GetChordText(i);

        const quint32 location = system->GetPositionX(chordText->GetPosition());

        ChordTextPainter* chordTextPainter = new ChordTextPainter(chordText);
        centerItem(chordTextPainter, location, location + currentStaffInfo.positionWidth, height + 4);
        chordTextPainter->setParentItem(parentSystem);
    }
}

void SystemRenderer::drawDirections(uint32_t height, const StaffData& currentStaffInfo) const
{
    for (uint32_t i = 0; i < system->GetDirectionCount(); i++)
    {
        shared_ptr<const Direction> direction = system->GetDirection(i);

        quint32 directionHeight = height; // keeps track of the height for this Direction only

        const quint32 location = system->GetPositionX(direction->GetPosition());

        // draw each symbol for the Direction
        for (size_t symbol = 0; symbol < direction->GetSymbolCount(); symbol++)
        {
            DirectionPainter* directionPainter = new DirectionPainter(direction, symbol);
            centerItem(directionPainter, location, location + currentStaffInfo.positionWidth, directionHeight + 4);
            directionPainter->setParentItem(parentSystem);

            directionHeight += System::SYSTEM_SYMBOL_SPACING;
        }
    }
}

void SystemRenderer::drawRhythmSlashes() const
{
    const uint32_t y = system->GetExtraSpacing();

    std::vector<RhythmSlashPainter*> slashPainters;

    // generate the rhythm slashes
    for (size_t i = 0; i < system->GetRhythmSlashCount(); i++)
    {
        shared_ptr<const RhythmSlash> rhythmSlash = system->GetRhythmSlash(i);
        const uint32_t x = system->GetPositionX(rhythmSlash->GetPosition());

        RhythmSlashPainter* painter = new RhythmSlashPainter(rhythmSlash);
        painter->setPos(x, y);
        slashPainters.push_back(painter);

        // draw ties
        if (rhythmSlash->IsTied() && i != 0)
        {
            const RhythmSlashPainter* prevSlash = slashPainters.at(i - 1);

            const double xDiff = x - prevSlash->x();

            QPainterPath path;
            path.moveTo(xDiff, System::RHYTHM_SLASH_SPACING / 4.0);
            path.arcTo(0, 0, xDiff, System::RHYTHM_SLASH_SPACING / 2.0, 0, 180);

            QGraphicsPathItem* arc = new QGraphicsPathItem(path);
            arc->setPos(prevSlash->x() + RhythmSlashPainter::STEM_OFFSET,
                        y + RhythmSlashPainter::NOTE_HEAD_OFFSET - arc->boundingRect().height());
            arc->setParentItem(parentSystem);
        }
    }

    // add each rhythm slash to the scene
    BOOST_FOREACH(RhythmSlashPainter* painter, slashPainters)
    {
        painter->setParentItem(parentSystem);
    }
}

void SystemRenderer::drawLegato(const StaffData& currentStaffInfo) const
{
    for (quint32 voice = 0; voice < Staff::NUM_STAFF_VOICES; voice++)
    {
        for (int string = 0; string < staff->GetTablatureStaffType(); string++)
        {
            int startPos = -1;
            const size_t positionCount = staff->GetPositionCount(voice);
            for (size_t i = 0; i < positionCount; i++)
            {
                const Position* position = staff->GetPosition(voice, i);
                const Note* note = position->GetNoteByString(string);

                const int currentPosition = position->GetPosition();

                if (note == NULL)
                {
                    startPos = -1;
                    continue;
                }
                if (note->HasHammerOn() || note->HasPullOff() || note->HasLegatoSlide())
                {
                    if (startPos == -1) // set the start position of an arc
                    {
                        startPos = currentPosition;
                    }
                }
                // if an arc has already been started, and the current note is
                // not a hammer-on/pull-off, end the arc
                else if (startPos != -1)
                {
                    const double leftPos = system->GetPositionX(startPos);
                    const double width = system->GetPositionX(currentPosition) - leftPos;
                    double height = 7.5;
                    double y = currentStaffInfo.getTabLineHeight(string) - 2;

                    if (string >= currentStaffInfo.numOfStrings / 2)
                    {
                        // for notes on the bottom half of the staff, flip the arc and place it below the notes
                        y += 2 * currentStaffInfo.tabLineSpacing + height / 2.5;
                        height = -height;
                    }

                    QPainterPath path;
                    path.moveTo(width, height / 2);
                    path.arcTo(0, 0, width, height, 0, 180);

                    QGraphicsPathItem *arc = new QGraphicsPathItem(path);
                    arc->setPos(leftPos + system->GetPositionSpacing() / 2, y);
                    arc->setParentItem(parentStaff);

                    startPos = -1;
                }

                if (note->HasHammerOnFromNowhere() || note->HasPullOffToNowhere())
                {
                    const double height = 10;
                    const double width = 6;
                    QPainterPath path;
                    path.moveTo(width, height / 2);
                    path.arcTo(0, 0, width, height, 0, 180);
                    QGraphicsPathItem *arc = new QGraphicsPathItem(path);
                    arc->setPos(system->GetPositionX(currentPosition) + 2,
                                currentStaffInfo.getTabLineHeight(string) - 2);
                    arc->setParentItem(parentStaff);

                    startPos = -1;
                }
            }
        }
    }
}

/// Draws all of the slides for a staff
void SystemRenderer::drawSlides(const StaffData& currentStaffInfo) const
{
    for (quint32 voice = 0; voice < Staff::NUM_STAFF_VOICES; voice++)
    {
        // iterate across the staff for each string
        for (quint8 string = 0; string < staff->GetTablatureStaffType(); string++)
        {
            const size_t positionCount = staff->GetPositionCount(voice);
            for (size_t i = 0; i < positionCount; i++)
            {
                const Position* currentPosition = staff->GetPosition(voice, i);
                const Note* note = currentPosition->GetNoteByString(string);

                if (note == NULL)
                {
                    continue;
                }

                quint8 type = 0;
                qint8 steps = 0;

                // draw any slides out of the note
                note->GetSlideOutOf(type, steps);

                if (type != Note::slideOutOfNone)
                {
                    // figure out if we're sliding up or down
                    const bool slideUp = (type == Note::slideOutOfUpwards) || (steps > 0);
                    const size_t nextPosIndex = staff->GetIndexOfNextPosition(voice, system, currentPosition);

                    drawSlidesHelper(currentStaffInfo, string, slideUp,
                                     currentPosition->GetPosition(), nextPosIndex);
                }

                // draw any slides into the note
                note->GetSlideInto(type);
                if (type != Note::slideIntoNone)
                {
                    const quint32 currentPosIndex = currentPosition->GetPosition();
                    const bool slideUp = (type == Note::slideIntoFromBelow);
                    const quint32 prevPosIndex = (currentPosIndex == 0) ? 0 : currentPosIndex - 1;

                    drawSlidesHelper(currentStaffInfo, string, slideUp,
                                     prevPosIndex, currentPosIndex);
                }
            }
        }
    }
}

/// Helper function to perform the actual rendering of a slide
/// Draws a slide on the given string, between the two position indexes
void SystemRenderer::drawSlidesHelper(const StaffData& currentStaffInfo, quint8 string,
                                      bool slideUp, quint32 posIndex1,
                                      quint32 posIndex2) const
{
    Q_ASSERT(posIndex1 <= posIndex2);

    const double leftPos = system->GetPositionX(posIndex1);
    const double width = system->GetPositionX(posIndex2) - leftPos;

    double height = 5;
    double y = (currentStaffInfo.getTabLineHeight(string + 1) + currentStaffInfo.getTabLineHeight(string)) / 2;

    if (slideUp) // if we're sliding up, flip the diagonal line
    {
        height = -height;
        y += currentStaffInfo.tabLineSpacing + 1;
    }

    QPainterPath path;
    path.lineTo(width - currentStaffInfo.positionWidth / 2, height);

    QGraphicsPathItem* line = new QGraphicsPathItem(path);
    line->setPos(leftPos + currentStaffInfo.positionWidth / 1.5 + 1, y + height / 2);
    line->setParentItem(parentStaff);
}

/// Draws the text symbols that appear below the tab staff (hammerons, slides, etc)
void SystemRenderer::drawSymbolsBelowTabStaff(const StaffData& staffInfo) const
{
    std::vector<Layout::SymbolGroup> symbolGroups;
    Layout::CalculateTabStaffBelowLayout(symbolGroups, system, staff);

    BOOST_FOREACH(const Layout::SymbolGroup& symbolGroup, symbolGroups)
    {
        QGraphicsItem* renderedSymbol = NULL;

        switch(symbolGroup.symbolType)
        {
        case Layout::SymbolPickStrokeDown:
            renderedSymbol = createPickStroke(musicFont.getSymbol(MusicFont::PickStrokeDown));
            break;

        case Layout::SymbolPickStrokeUp:
            renderedSymbol = createPickStroke(musicFont.getSymbol(MusicFont::PickStrokeUp));
            break;

        case Layout::SymbolTap:
            renderedSymbol = createPlainText("T", QFont::StyleNormal);
            break;

        case Layout::SymbolHammerOnPullOff:
        {
            const Position* pos = staff->GetPositionByPosition(0, symbolGroup.leftPosIndex);
            if (pos->HasNoteWithHammeron() || pos->HasNoteWithHammeronFromNowhere())
            {
                renderedSymbol = createPlainText("H", QFont::StyleNormal);
            }
            else if (pos->HasNoteWithPulloff() || pos->HasNoteWithPulloffToNowhere())
            {
                renderedSymbol = createPlainText("P", QFont::StyleNormal);
            }
            break;
        }

        case Layout::SymbolSlide:
            renderedSymbol = createPlainText("sl.", QFont::StyleItalic);
            break;

        case Layout::SymbolTappedHarmonic:
            renderedSymbol = createPlainText("T", QFont::StyleNormal);
            break;

        case Layout::SymbolArtificialHarmonic:
            renderedSymbol = createArtificialHarmonicText(staff->GetPositionByPosition(0, symbolGroup.leftPosIndex));
            break;

        default:
            Q_ASSERT(false); // shouldn't get any other symbol types!!!
            break;
        }

        centerItem(renderedSymbol, symbolGroup.leftX, symbolGroup.leftX + symbolGroup.width,
                   staffInfo.getBottomTabLine() + (symbolGroup.height + 1) * Staff::TAB_SYMBOL_HEIGHT);

        renderedSymbol->setParentItem(parentStaff);
    }
}

QGraphicsItem* SystemRenderer::createPickStroke(const QString& text) const
{
    QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(text);
    textItem->setFont(musicFont.getFont());
    textItem->setPos(2, -28);

    // Sticking the text in a QGraphicsItemGroup allows us to offset the position of the text from its default location
    QGraphicsItemGroup* group = new QGraphicsItemGroup;
    group->addToGroup(textItem);

    return group;
}

/// Creates a plain text item; useful for symbols that don't use the music font (hammerons, slides, etc)
QGraphicsItem* SystemRenderer::createPlainText(const QString& text, QFont::Style style) const
{
    QFont displayFont("Liberation Sans");
    displayFont.setPixelSize(10);
    displayFont.setStyle(style);

    QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(text);
    textItem->setFont(displayFont);
    textItem->setPos(0, -8);

    QGraphicsItemGroup* group = new QGraphicsItemGroup;
    group->addToGroup(textItem);

    return group;
}

/// Creates the text portion of an artificial harmonic - displaying the note value
QGraphicsItem* SystemRenderer::createArtificialHarmonicText(const Position* position) const
{
    std::vector<const Note*> notes;
    position->GetNotes(notes);

    // find the note with an artificial harmonic
    std::vector<const Note*>::const_iterator location = std::find_if(notes.begin(), notes.end(),
                                                               std::mem_fun(&Note::HasArtificialHarmonic));

     // this function should not even be executed if there is no note with an artificial harmonic
    Q_ASSERT(location != notes.end());

    const Note* note = *location;

    quint8 key = 0, variation = 0, octave = 0;
    note->GetArtificialHarmonic(key, variation, octave);

    // use the ChordName class to convert the key/variation data to a text representation
    ChordName name(key, variation, ChordName::DEFAULT_FORMULA, ChordName::DEFAULT_FORMULA_MODIFICATIONS);

    const QString text = QString().fromStdString(name.GetKeyText(false));

    return createPlainText(text, QFont::StyleNormal);
}

/// Draws the symbols that appear between the tab and standard notation staves
void SystemRenderer::drawSymbols(const StaffData& staffInfo) const
{
    std::vector<Layout::SymbolGroup> symbolGroups;
    Layout::CalculateSymbolLayout(symbolGroups, score, system, staff);

    BOOST_FOREACH(const Layout::SymbolGroup& symbolGroup, symbolGroups)
    {
        QGraphicsItem* renderedSymbol = NULL;

        const int width = clamp(symbolGroup.width, 0, system->GetRect().GetWidth() - symbolGroup.leftX);

        switch(symbolGroup.symbolType)
        {
        case Layout::SymbolLetRing:
            renderedSymbol = createConnectedSymbolGroup("let ring", QFont::StyleItalic, width, staffInfo);
            break;

        case Layout::SymbolVolumeSwell:
        {
            // figure out the direction of the volume swell
            uint8_t startVolume = 0, endVolume = 0, duration = 0;
            staff->GetPosition(0, symbolGroup.leftPosIndex)->GetVolumeSwell(startVolume, endVolume, duration);

            renderedSymbol = createVolumeSwell(width, staffInfo,
                                               (startVolume <= endVolume) ? VolumeIncreasing : VolumeDecreasing);
            break;
        }

        case Layout::SymbolVibrato:
            renderedSymbol = drawContinuousFontSymbols(MusicFont::Vibrato, width);
            break;

        case Layout::SymbolWideVibrato:
            renderedSymbol = drawContinuousFontSymbols(MusicFont::WideVibrato, width);
            break;

        case Layout::SymbolPalmMuting:
            renderedSymbol = createConnectedSymbolGroup("P.M.", QFont::StyleNormal, width, staffInfo);
            break;

        case Layout::SymbolTremoloPicking:
            renderedSymbol = createTremoloPicking(staffInfo);
            break;

        case Layout::SymbolTremoloBar:
            renderedSymbol = new TremoloBarPainter(staff->GetPosition(0, symbolGroup.leftPosIndex),
                                                   width);
            break;

        case Layout::SymbolTrill:
            renderedSymbol = createTrill(staffInfo);
            break;

        case Layout::SymbolNaturalHarmonic:
            renderedSymbol = createConnectedSymbolGroup("N.H.", QFont::StyleNormal, width, staffInfo);
            break;

        case Layout::SymbolArtificialHarmonic:
            renderedSymbol = createConnectedSymbolGroup("A.H.", QFont::StyleNormal, width, staffInfo);
            break;

        case Layout::SymbolDynamic:
        {
            const Position* pos = staff->GetPosition(0, symbolGroup.leftPosIndex);
            renderedSymbol = createDynamic(score->FindDynamic(score->FindSystemIndex(system),
                                                              system->FindStaffIndex(staff),
                                                              pos->GetPosition()));
            break;
        }

        default:
            Q_ASSERT(false); // shouldn't have any other symbol types!!!
            break;
        }

        renderedSymbol->setPos(symbolGroup.leftX,
                               staffInfo.getTopTabLine() - (symbolGroup.height + 1) * Staff::TAB_SYMBOL_HEIGHT);
        renderedSymbol->setParentItem(parentStaff);
    }
}

/// Draws symbols that are grouped across multiple positions (i.e. consecutive "let ring" symbols)
QGraphicsItem* SystemRenderer::createConnectedSymbolGroup(const QString& text,
                                                          QFont::Style style, int width,
                                                          const StaffData& currentStaffInfo) const
{
    static QFont font("Liberation Sans");
    font.setPixelSize(9);
    font.setStyle(style);

    // Render the description (i.e. "let ring")
    QGraphicsSimpleTextItem* description = new QGraphicsSimpleTextItem;
    description->setText(text);
    description->setFont(font);

    QGraphicsItemGroup* group = new QGraphicsItemGroup;
    group->addToGroup(description);

    // Draw dashed line across the remaining positions in the group
    if (width > currentStaffInfo.positionWidth)
    {
        const double rightEdge = width - 0.5 * currentStaffInfo.positionWidth;
        const double leftEdge = description->boundingRect().right();
        const double middleHeight = Staff::TAB_SYMBOL_HEIGHT / 2.0;
        QGraphicsLineItem* line = new QGraphicsLineItem(leftEdge, middleHeight, rightEdge, middleHeight);
        line->setPen(QPen(Qt::black, 1, Qt::DashLine));

        group->addToGroup(line);

        // Draw a vertical line at the end of the dotted lines
        QGraphicsLineItem* lineEnd = new QGraphicsLineItem(line->boundingRect().right(), 1,
                                                           line->boundingRect().right(), Staff::TAB_SYMBOL_HEIGHT - 1);
        group->addToGroup(lineEnd);
    }

    return group;
}

/// Creates a volume swell QGraphicsItem of the specified type
QGraphicsItem* SystemRenderer::createVolumeSwell(uint8_t width, const StaffData& currentStaffInfo,
                                                 VolumeSwellType type) const
{
    double leftX = currentStaffInfo.positionWidth / 2.0;
    double rightX = width;

    if (type == VolumeDecreasing) // switch directions for decreasing volume swells
    {
        std::swap(leftX, rightX);
    }

    // in this case, the width of the symbol rectangle is the # of positions that the volume swell spans
    QPainterPath path;
    path.moveTo(rightX, 0);
    path.lineTo(leftX, Staff::TAB_SYMBOL_HEIGHT / 2.0);
    path.lineTo(rightX, Staff::TAB_SYMBOL_HEIGHT);

    QGraphicsPathItem* swell = new QGraphicsPathItem(path);
    return swell;
}

QGraphicsItem* SystemRenderer::drawContinuousFontSymbols(QChar symbol, int width) const
{
    QFont font = musicFont.getFont();
    font.setPixelSize(25);

    const double symbolWidth = QFontMetricsF(font).width(symbol);
    const int numSymbols = width / symbolWidth;
    QGraphicsSimpleTextItem* text = new QGraphicsSimpleTextItem(QString(numSymbols, symbol));
    text->setFont(font);
    text->setPos(0, -25);

    // a bit of a hack for getting around the height offset caused by the music font
    QGraphicsItemGroup* group = new QGraphicsItemGroup;
    group->addToGroup(text);

    return group;
}

QGraphicsItem* SystemRenderer::createTremoloPicking(const StaffData& currentStaffInfo) const
{
    const double offset = Staff::TAB_SYMBOL_HEIGHT / 3;

    QGraphicsItemGroup* group = new QGraphicsItemGroup;

    for (int i = 0; i < 3; i++)
    {
        QGraphicsSimpleTextItem* line = new QGraphicsSimpleTextItem(MusicFont::getSymbol(MusicFont::TremoloPicking));
        line->setFont(musicFont.getFont());
        centerItem(line, 0, currentStaffInfo.positionWidth * 1.25, -37 + i * offset);
        group->addToGroup(line);
    }

    return group;
}

QGraphicsItem* SystemRenderer::createTrill(const StaffData& currentStaffInfo) const
{
    QFont font(musicFont.getFont());
    font.setPixelSize(21);

    QGraphicsSimpleTextItem* text = new QGraphicsSimpleTextItem(MusicFont::getSymbol(MusicFont::Trill));
    text->setFont(font);
    centerItem(text, 0, currentStaffInfo.positionWidth, -18);

    QGraphicsItemGroup* group = new QGraphicsItemGroup;
    group->addToGroup(text);

    return group;
}

QGraphicsItem* SystemRenderer::createDynamic(boost::shared_ptr<const Dynamic> dynamic) const
{
    QGraphicsSimpleTextItem* textItem = new QGraphicsSimpleTextItem(QString::fromStdString(dynamic->GetText(false)));
    textItem->setFont(musicFont.getFont());
    textItem->setPos(0, -20);

    // Sticking the text in a QGraphicsItemGroup allows us to offset the position of the text from its default location
    QGraphicsItemGroup* group = new QGraphicsItemGroup;
    group->addToGroup(textItem);

    return group;
}

void SystemRenderer::drawStdNotation(const StaffData& currentStaffInfo) const
{
    System::BarlineConstPtr currentBarline;
    System::BarlineConstPtr prevBarline = system->GetStartBar();

    QList<StdNotationPainter*> notePainters;
    QMultiMap<int, StdNotationPainter*> accidentalsMap;
    std::list<NoteStem> stems;

    for (quint32 voice = 0; voice < Staff::NUM_STAFF_VOICES; voice++)
    {
        for (quint32 i=0; i < staff->GetPositionCount(voice); i++)
        {
            const Position* currentPosition = staff->GetPosition(voice, i);
            const quint32 location = system->GetPositionX(currentPosition->GetPosition());
            currentBarline = system->GetPrecedingBarline(currentPosition->GetPosition());
            const KeySignature& currentKeySig = currentBarline->GetKeySignature();

            // Find the guitar corresponding to the current staff
            shared_ptr<Guitar> currentGuitar = score->GetGuitar(system->FindStaffIndex(staff));
            Q_ASSERT(currentGuitar);

            std::vector<double> noteLocations;

            // if we reach a new bar, we can adjust all of the accidentals for the previous bar
            if (currentBarline != prevBarline)
            {
                adjustAccidentals(accidentalsMap);
                accidentalsMap.clear();
                prevBarline = currentBarline;
            }

            if (currentPosition->HasMultibarRest())
            {
                uint8_t measureCount = 0;
                currentPosition->GetMultibarRest(measureCount);
                drawMultiBarRest(currentBarline, currentStaffInfo, measureCount);
                continue;
            }

            // just draw rests right away, since we don't have to worry about beaming or accidentals
            if (currentPosition->IsRest())
            {
                RestPainter* restPainter = new RestPainter(*currentPosition);
                centerItem(restPainter, location, location + currentStaffInfo.positionWidth * 1.25,
                           currentStaffInfo.getTopStdNotationLine());
                restPainter->setParentItem(parentStaff);
                continue;
            }

            for (uint32_t j = 0; j < currentPosition->GetNoteCount(); j++)
            {
                const Note* note = currentPosition->GetNote(j);

                StdNotationPainter* stdNotePainter = new StdNotationPainter(currentStaffInfo, staff, currentPosition,
                                                                            note, currentGuitar->GetTuning(), currentKeySig);
                notePainters << stdNotePainter;

                // map all of the notes for each position on the staff, so that we can adjust accidentals later
                accidentalsMap.insert(stdNotePainter->getYLocation(), stdNotePainter);

                noteLocations.push_back(stdNotePainter->getYLocation());
            }

            // add note stem for any notes other than whole notes
            if (currentPosition->GetDurationType() != 1 && currentPosition->GetNoteCount() > 0)
            {
                NoteStem stem(currentStaffInfo, currentPosition, location, noteLocations);
                stems.push_back(stem);
            }

            if (!noteLocations.empty())
            {
                drawLedgerLines(noteLocations, location, currentStaffInfo);
            }
        }
    }

    // make sure we adjust accidentals for the last bar of the staff
    adjustAccidentals(accidentalsMap);

    // after adjusting accidentals, etc, we can add the painters to the scene
    foreach(StdNotationPainter* painter, notePainters)
    {
        const quint32 location = system->GetPositionX(painter->getPositionObject()->GetPosition());
        painter->setPos(location, currentStaffInfo.getTopStdNotationLine());
        painter->setParentItem(parentStaff);
    }

    std::vector<NoteStem> currentStemGroup;
    std::list<NoteStem> updatedStems;

    // group all of the stems into their beaming groups, and draw them
    while(!stems.empty())
    {
        NoteStem stem = stems.front();
        stems.pop_front();

        currentStemGroup.push_back(stem);

        if (stem.position()->IsBeamEnd() || (!stem.position()->IsBeamStart() && currentStemGroup.size() == 1))
        {
            BeamGroup group(currentStaffInfo, currentStemGroup);
            currentStemGroup.clear();

            group.drawStems(parentStaff);

            // grab a copy of the updated note stems for later use
            std::vector<NoteStem> temp;
            group.copyNoteSteams(temp);
            updatedStems.insert(updatedStems.end(), temp.begin(), temp.end());
        }
    }

    // Now, draw any irregular note groupings (triplets, etc)
    // This must be done after the beams are drawn, since the note stems will be adjusted during that process
    std::vector<NoteStem> currentIrregularNoteGroup;

    while(!updatedStems.empty())
    {
        NoteStem stem = updatedStems.front();
        updatedStems.pop_front();

        // check if this note isn't part of an irregular grouping
        if (!stem.position()->IsIrregularGroupingEnd() &&
                !stem.position()->IsIrregularGroupingMiddle() &&
                !stem.position()->IsIrregularGroupingStart())
        {
            currentIrregularNoteGroup.clear();
            continue;
        }

        currentIrregularNoteGroup.push_back(stem);

        // draw the grouping
        if (stem.position()->IsIrregularGroupingEnd())
        {
            IrregularNoteGroup irregularGroup(currentIrregularNoteGroup);
            irregularGroup.draw(parentStaff);

            currentIrregularNoteGroup.clear();
        }
    }
}

void SystemRenderer::adjustAccidentals(QMultiMap<int, StdNotationPainter*>& accidentalsMap) const
{
    QList<int> keys = accidentalsMap.uniqueKeys();
    QList<int>::const_iterator i = keys.begin();

    while(i != keys.end())
    {
        QList<StdNotationPainter*> notes = accidentalsMap.values(*i);
        int currentAccidental = StdNotationPainter::NO_ACCIDENTAL;

        for (int j = notes.size() - 1; j >= 0; j--)
        {
            StdNotationPainter* note = notes.at(j);

            if (note->accidental == currentAccidental)
            {
                note->accidental = StdNotationPainter::NO_ACCIDENTAL;
            }
            else
            {
                currentAccidental = note->accidental;
                if (currentAccidental == StdNotationPainter::NO_ACCIDENTAL)
                {
                    note->accidental = StdNotationPainter::NATURAL;
                }
            }
        }

        ++i;
    }
}

void SystemRenderer::drawMultiBarRest(boost::shared_ptr<const Barline> currentBarline,
                                      const StaffData& currentStaffInfo, int measureCount) const
{
    System::BarlineConstPtr nextBarline = system->GetNextBarline(currentBarline->GetPosition());

    const double leftX = (currentBarline->GetPosition() == 0) ?
                system->GetPositionX(currentBarline->GetPosition()) :
                system->GetPositionX(currentBarline->GetPosition() + 1);

    const double rightX = clamp(static_cast<double>(system->GetPositionX(nextBarline->GetPosition())),
                                0.0, system->GetRect().GetWidth() - system->GetPositionSpacing() / 2.0);

    // draw measure count
    QGraphicsSimpleTextItem* measureCountText = new QGraphicsSimpleTextItem;
    measureCountText->setText(QString().setNum(measureCount));
    measureCountText->setFont(musicFont.getFont());

    centerItem(measureCountText, leftX, rightX,
               currentStaffInfo.getTopStdNotationLine() - musicFont.getFont().pixelSize() * 1.5);

    measureCountText->setParentItem(parentStaff);

    // draw symbol across std. notation staff
    QGraphicsLineItem* vertLineLeft = new QGraphicsLineItem(leftX,
                                                            currentStaffInfo.getStdNotationLineHeight(2),
                                                            leftX,
                                                            currentStaffInfo.getStdNotationLineHeight(4));
    vertLineLeft->setParentItem(parentStaff);

    QGraphicsLineItem* vertLineRight = new QGraphicsLineItem(rightX,
                                                             currentStaffInfo.getStdNotationLineHeight(2),
                                                             rightX,
                                                             currentStaffInfo.getStdNotationLineHeight(4));
    vertLineRight->setParentItem(parentStaff);

    QGraphicsRectItem* horizontalLine = new QGraphicsRectItem(leftX,
                                                              currentStaffInfo.getStdNotationLineHeight(2) +
                                                              0.5 * Staff::STD_NOTATION_LINE_SPACING,
                                                              rightX - leftX,
                                                              Staff::STD_NOTATION_LINE_SPACING * 0.9);
    horizontalLine->setBrush(QBrush(Qt::black));
    horizontalLine->setParentItem(parentStaff);
}

/// Draws ledger lines for all notes at a position
/// @param noteLocations - List of y-coordinates of all notes at the position
void SystemRenderer::drawLedgerLines(const std::vector<double> &noteLocations,
                                const double xLocation,
                                const StaffData& staffData) const
{
    const double highestNote = *std::min_element(noteLocations.begin(), noteLocations.end());
    const double lowestNote = *std::max_element(noteLocations.begin(), noteLocations.end());

    const int numLedgerLinesTop = -highestNote / Staff::STD_NOTATION_LINE_SPACING;
    const int numLedgerLinesBottom = (lowestNote - staffData.getStdNotationStaffSize()) /
                                      Staff::STD_NOTATION_LINE_SPACING;

    std::vector<double> ledgerLineLocations;

    if (numLedgerLinesTop > 0)
    {
        for (int i = 1; i<= numLedgerLinesTop; i++)
        {
            ledgerLineLocations.push_back(staffData.getTopStdNotationLine() -
                                          i * Staff::STD_NOTATION_LINE_SPACING);
        }
    }
    if (numLedgerLinesBottom > 0)
    {
        for (int i = 1; i<= numLedgerLinesBottom; i++)
        {
            ledgerLineLocations.push_back(staffData.getBottomStdNotationLine() +
                                          i * Staff::STD_NOTATION_LINE_SPACING);
        }
    }

    for (size_t i = 0; i < ledgerLineLocations.size(); i++)
    {
        QGraphicsLineItem* ledgerLine = new QGraphicsLineItem;
        ledgerLine->setLine(0, 0, StdNotationPainter::getNoteHeadWidth() * 2, 0);
        centerItem(ledgerLine, xLocation, xLocation + staffData.positionWidth, ledgerLineLocations[i]);
        ledgerLine->setParentItem(parentStaff);
    }
}

void SystemRenderer::connectSignals(ScoreArea *scoreArea)
{
    for (size_t i = 0; i < staffPainters.size(); i++)
    {
        QObject::connect(staffPainters[i], SIGNAL(selectionUpdated(int,int)),
                         scoreArea->getCaret(), SLOT(updateSelection(int,int)));
    }

    for (size_t i = 0; i < barlinePainters.size(); i++)
    {
        QObject::connect(barlinePainters[i], SIGNAL(clicked(int)),
                         scoreArea, SIGNAL(barlineClicked(int)));
    }

    for (size_t i = 0; i < keySignaturePainters.size(); i++)
    {
        QObject::connect(keySignaturePainters[i], SIGNAL(clicked(int)),
                         scoreArea, SIGNAL(keySignatureClicked(int)));
    }
}