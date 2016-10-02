// Copyright (c) 2015 Tom Barthel-Steer, http://www.tomsteer.net
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#include "scopewidget.h"
#include <QPainter>
#include <QPointF>
#include <QDebug>
#include "sacn/sacnlistener.h"

#define AXIS_WIDTH 50
#define TOP_GAP 10
#define RIGHT_GAP 30
#define AXIS_TO_WINDOW_GAP 5

ScopeChannel::ScopeChannel()
{
    m_universe = 1;
    m_address = 1;
    m_sixteenBit = false;
    m_color = Qt::white;
    clear();
}


ScopeChannel::ScopeChannel(int universe, int address)
{
    m_universe = universe;
    m_address = address;
    m_sixteenBit = false;
    m_color = Qt::white;
    clear();
}

void ScopeChannel::addPoint(QPointF point)
{
    m_points[m_last] = point;
    m_last = (m_last+1) % RING_BUF_SIZE;
    if(m_size<RING_BUF_SIZE) m_size++;
    m_highestTime = point.x();
}

void ScopeChannel::clear()
{
    m_size = 0;
    m_last = 0;
    m_highestTime = 0;
}

int ScopeChannel::count()
{
    return m_size;
}

QPointF ScopeChannel::getPoint(int index)
{
    Q_ASSERT(index < m_size);

    int pos = (m_last + index) % m_size;
    return m_points[pos];
}

void ScopeChannel::setUniverse(int value)
{
    if(m_universe!=value)
    {
        m_universe = value;
        clear();
    }
}

void ScopeChannel::setAddress(int value)
{
    if(m_address!=value)
    {
        m_address = value;
        clear();
    }
}



ScopeWidget::ScopeWidget(QWidget *parent) : QWidget(parent)
{
    m_timebase = 10;
    m_running = false;
}


void ScopeWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);


    QPen gridPen;
    gridPen.setColor(QColor("#434343"));
    QVector<qreal> dashPattern;
    dashPattern << 1 << 1;

    QPen textPen;
    textPen.setColor(Qt::white);

    QPainter painter(this);

    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), Qt::black);

    // Origin is the bottom left of the scope window
    QPoint origin(rect().bottomLeft().x() + AXIS_WIDTH, rect().bottomLeft().y() - AXIS_WIDTH);

    QRect scopeWindow;
    scopeWindow.setBottomLeft(origin);
    scopeWindow.setTop(rect().top() + TOP_GAP);
    scopeWindow.setRight(rect().right() - RIGHT_GAP);
    painter.setBrush(QBrush(Qt::black));
    painter.drawRect(scopeWindow);

    // Draw vertical axis
    painter.translate(scopeWindow.topLeft().x(), scopeWindow.topLeft().y());
    gridPen.setDashPattern(dashPattern);

    painter.setPen(gridPen);
    painter.drawLine(0, 0, 0, scopeWindow.height());
    int percent = 100;

    QFont font;
    QFontMetricsF metrics(font);

    for(int i=0; i<11; i++)
    {
        qreal y = i*scopeWindow.height()/10.0;
        painter.setPen(gridPen);
        painter.drawLine(-10, y, scopeWindow.width(), y);
        QString text = QString("%1%%").arg(percent);
        QRectF fontRect = metrics.boundingRect(text);

        fontRect.moveCenter(QPoint(-20, y));

        painter.setPen(textPen);
        painter.drawText(fontRect, text, QTextOption(Qt::AlignLeft));

        percent-=10;
    }

    // Draw horizontal axis
    painter.resetTransform();
    painter.translate(scopeWindow.bottomLeft().x(), scopeWindow.bottomLeft().y());

    int time = 0;

    for(int i=0; i<11; i++)
    {
        qreal x = i * scopeWindow.width() / 10;
        painter.setPen(gridPen);
        painter.drawLine(x, 0, x, -scopeWindow.height());

        QString text;
        if(m_timebase<1000)
            text = QString("%1ms").arg(time);
        else
            text = QString("%1s").arg(time/1000);

        QRectF fontRect = metrics.boundingRect(text);

        fontRect.moveCenter(QPoint(x, 20));

        painter.setPen(textPen);
        painter.drawText(fontRect, text, QTextOption(Qt::AlignLeft));

        time += m_timebase;

    }

    // Plot the points
    painter.resetTransform();
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.translate(scopeWindow.topLeft().x() , scopeWindow.topLeft().y());
    // Scale by the timebase, which is units per division, 10 divisions per window
    qreal x_scale = (qreal)scopeWindow.width() / ( m_timebase * 10.0 );
    qreal y_scale = (qreal)scopeWindow.height() / 65535.0;

    painter.setBrush(QBrush());
    painter.setRenderHint(QPainter::Antialiasing);

    int maxTime = m_timebase * 10; // The X-axis size of the display, in milliseconds
    foreach(ScopeChannel *ch, m_channels)
    {
        if(!ch->enabled())
            continue;

        QPen pen;
        pen.setColor(ch->color());
        pen.setWidth(2);
        painter.setPen(pen);
        QPainterPath path;
        bool first = true;

        for(int i=0; i<ch->count()-1; i++)
        {
            QPointF p = ch->getPoint(i);
            qreal normalizedTime = ch->m_highestTime - p.x();

            qreal x = x_scale * (maxTime - normalizedTime);
            qreal y = y_scale * (65535 - p.y());
            if(x>=0)
            {
                if(first)
                {
                    path.moveTo(x,y);
                    first = false;
                }
                else
                    path.lineTo(x, y);
            }
        }
        painter.drawPath(path);
    }

}


void ScopeWidget::setTimebase(int timebase)
{
    m_timebase = timebase;
    update();
}

void ScopeWidget::addChannel(ScopeChannel *channel)
{
    m_channels << channel;
    sACNListener *listener = sACNManager::getInstance()->getListener(channel->universe());
    listener->monitorAddress(channel->address());

}

void ScopeWidget::removeChannel(ScopeChannel *channel)
{
    m_channels.removeAll(channel);
    sACNListener *listener = sACNManager::getInstance()->getListener(channel->universe());
    listener->unMonitorAddress(channel->address());
}

void ScopeWidget::dataReady(int address, QPointF p)
{
    if(!m_running) return;

    sACNListener *listener = static_cast<sACNListener *>(sender());

    ScopeChannel *ch = NULL;
    for(int i=0; i<m_channels.count(); i++)
    {
        if(m_channels[i]->address() == address && m_channels[i]->universe()==listener->universe())
        {
            ch = m_channels[i];

            // To save data space, no point in storing more than 100 points per timebase division
            if(p.x() - ch->m_highestTime < (m_timebase/100.0))
                   continue;
            if(ch->sixteenBit())
            {
                quint8 fineLevel = listener->mergedLevels()[ch->address()+1].level;
                p.setY(p.y() * 255.0 + fineLevel);
                ch->addPoint(p);
            }
            else
            {
                p.setY(p.y() * 255.0);
                ch->addPoint(p);
            }
        }
    }
    update();
}

void ScopeWidget::start()
{
    m_running = true;
    foreach(ScopeChannel *ch, m_channels)
    {
        ch->clear();
        sACNListener *listener = sACNManager::getInstance()->getListener(ch->universe());
        connect(listener, SIGNAL(dataReady(int, QPointF)), this, SLOT(dataReady(int, QPointF)));
    }
    update();

}

void ScopeWidget::stop()
{
    m_running = false;
}
