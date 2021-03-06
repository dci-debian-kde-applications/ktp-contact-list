/*
 * Copyright (C) 2009 by Peter Penz <peter.penz@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef KTOOLTIPWINDOW_H
#define KTOOLTIPWINDOW_H

#include <QWidget>
class QPaintEvent;

class KToolTipWindow : public QWidget
{
    Q_OBJECT

public:
    explicit KToolTipWindow(QWidget *content);
    virtual ~KToolTipWindow();

protected:
    virtual void paintEvent(QPaintEvent *event);

private:
    /**
     * Helper method for KToolTipWindow::paintEvent() to adjust the painter path \p path
     * by rounded corners.
     */
    static void arc(QPainterPath &path, qreal cx, qreal cy, qreal radius, qreal angle, qreal sweeplength);
};

#endif
