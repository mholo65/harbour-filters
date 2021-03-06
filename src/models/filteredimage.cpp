/*
  Copyright (c) 2014, Martin Björkström
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "filteredimage.h"

#include <QPainter>
#include <QImageReader>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

#include "src/filters/abstractimagefilter.h"

#include "src/3rdparty/nemo-qml-plugin-thumbnailer/src/nemoimagemetadata.h"

static QImage rotate(const QImage &src,
                     NemoImageMetadata::Orientation orientation)
{
    QTransform trans;
    QImage dst, tmp;

    /* For square images 90-degree rotations of the pixel could be
       done in-place, and flips could be done in-place for any image
       instead of using the QImage routines which make copies of the
       data. */

    switch (orientation) {
        case NemoImageMetadata::TopRight:
            /* horizontal flip */
            dst = src.mirrored(true, false);
            break;
        case NemoImageMetadata::BottomRight:
            /* horizontal flip, vertical flip */
            dst = src.mirrored(true, true);
            break;
        case NemoImageMetadata::BottomLeft:
            /* vertical flip */
            dst = src.mirrored(false, true);
            break;
        case NemoImageMetadata::LeftTop:
            /* rotate 90 deg clockwise and flip horizontally */
            trans.rotate(90.0);
            tmp = src.transformed(trans);
            dst = tmp.mirrored(true, false);
            break;
        case NemoImageMetadata::RightTop:
            /* rotate 90 deg anticlockwise */
            trans.rotate(90.0);
            dst = src.transformed(trans);
            break;
        case NemoImageMetadata::RightBottom:
            /* rotate 90 deg anticlockwise and flip horizontally */
            trans.rotate(-90.0);
            tmp = src.transformed(trans);
            dst = tmp.mirrored(true, false);
            break;
        case NemoImageMetadata::LeftBottom:
            /* rotate 90 deg clockwise */
            trans.rotate(-90.0);
            dst = src.transformed(trans);
            break;
        default:
            dst = src;
            break;
    }

    return dst;
}

FilteredImage::FilteredImage(QQuickItem *parent) :
    QQuickItem(parent)
{
    m_filter = 0;
    m_isApplyingFilter = false;

    setFlag(QQuickItem::ItemHasContents, true);

    connect(this,SIGNAL(imageChanged(QImage)),SLOT(update()));
}

QString FilteredImage::source() const
{
    return m_source;
}

void FilteredImage::setSource(const QString &source)
{
    if(m_source != source)
    {
        m_source = source;

        QImageReader ir(m_source);
        QByteArray format(ir.format());

        m_image = ir.read();

        NemoImageMetadata meta(m_source, format);

        if (meta.orientation() != NemoImageMetadata::TopLeft)
        {
            m_image = rotate(m_image, meta.orientation());
        }

        setImplicitWidth((qreal)m_image.width());
        setImplicitHeight((qreal)m_image.height());

        m_imageChanged = true;
        emit sourceChanged(m_source);
        emit imageChanged(m_image);
    }
}

bool FilteredImage::isApplyingFilter()
{
    return m_isApplyingFilter;
}

QImage FilteredImage::image() const
{
    return m_filteredImage.isNull() ? m_image : m_filteredImage;
}

void FilteredImage::resetFilter()
{
    if(m_filter != 0)
    {
        m_filter->resetParameters();
    }

    m_filteredImage = m_image;
    m_imageChanged = true;
    emit imageChanged(m_filteredImage);
    setIsApplyingFilter(false);
}

void FilteredImage::applyFilter(AbstractImageFilter *filter)
{
    if(m_filter != 0)
    {
        disconnect(m_filter,0,this,0);
    }

    m_filter = filter;

    if(m_filter != 0)
    {
        connect(m_filter,SIGNAL(filterApplied(QImage)),this,SLOT(filterApplied(QImage)));

        if(m_filter->parameterList().isEmpty())
        {
            if(m_filter->applyFilter(m_image))
            {
                setIsApplyingFilter(true);
            }
        }
        else
        {
            resetFilter();
        }
    }
    else
    {
        resetFilter();
    }
}

void FilteredImage::reApplyFilter()
{
    if(m_filter != 0)
    {
        if(m_filter->applyFilter(m_image))
        {
            setIsApplyingFilter(true);
        }
    }
}

void FilteredImage::applyCurrentFilter()
{
    if(!m_filteredImage.isNull())
    {
        m_image = m_filteredImage;

        m_filteredImage = QImage();
    }
}

void FilteredImage::saveImage()
{
    applyCurrentFilter();

    QDir dir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));

    if(!dir.cd("filters"))
    {
        dir.mkdir("filters");
        dir.cd("filters");
    }

    QString filename = QString::number(QDateTime::currentMSecsSinceEpoch()) + ".jpg";

    m_image.save(dir.absolutePath() + "/" + filename,"JPG");

    emit imageSaved(filename);
}

QSGNode *FilteredImage::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode*>(oldNode);
    QImage *image = !m_filteredImage.isNull() ? &m_filteredImage : &m_image;

    if(image->isNull())
    {
        delete node;
        return 0;
    }

    if (!node)
    {
        node = new QSGSimpleTextureNode();
    }

    if (m_imageChanged || !node->texture())
    {
        m_imageChanged = false;
        delete node->texture();

        node->setTexture(window()->createTextureFromImage(*image));

        node->setRect(0,0,implicitWidth(),implicitHeight());

        node->markDirty(QSGNode::DirtyMaterial | QSGNode::DirtyGeometry);
    }

    return node;
}

void FilteredImage::filterApplied(const QImage &image)
{
    m_filteredImage = image;

    m_imageChanged = true;
    emit imageChanged(m_filteredImage);
    setIsApplyingFilter(false);
}

void FilteredImage::setIsApplyingFilter(bool value)
{
    if(m_isApplyingFilter != value)
    {
        m_isApplyingFilter = value;
        emit isApplyingFilterChanged(m_isApplyingFilter);
    }
}


