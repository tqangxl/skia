/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkSVGDevice_DEFINED
#define SkSVGDevice_DEFINED

#include "SkDevice.h"

class SkXMLWriter;

class SkSVGDevice : public SkBaseDevice {
public:
    static SkBaseDevice* Create(const SkISize& size, SkXMLWriter* writer);

    virtual SkImageInfo imageInfo() const override;

protected:
    virtual void drawPaint(const SkDraw&, const SkPaint& paint) override;
    virtual void drawPoints(const SkDraw&, SkCanvas::PointMode mode, size_t count,
                            const SkPoint[], const SkPaint& paint) override;
    virtual void drawRect(const SkDraw&, const SkRect& r, const SkPaint& paint) override;
    virtual void drawOval(const SkDraw&, const SkRect& oval, const SkPaint& paint) override;
    virtual void drawRRect(const SkDraw&, const SkRRect& rr, const SkPaint& paint) override;
    virtual void drawPath(const SkDraw&, const SkPath& path,
                          const SkPaint& paint,
                          const SkMatrix* prePathMatrix = NULL,
                          bool pathIsMutable = false) override;

    virtual void drawBitmap(const SkDraw&, const SkBitmap& bitmap,
                            const SkMatrix& matrix, const SkPaint& paint) override;
    virtual void drawSprite(const SkDraw&, const SkBitmap& bitmap,
                            int x, int y, const SkPaint& paint) override;
    virtual void drawBitmapRect(const SkDraw&, const SkBitmap&,
                                const SkRect* srcOrNull, const SkRect& dst,
                                const SkPaint& paint,
                                SkCanvas::DrawBitmapRectFlags flags) override;

    virtual void drawText(const SkDraw&, const void* text, size_t len,
                          SkScalar x, SkScalar y, const SkPaint& paint) override;
    virtual void drawPosText(const SkDraw&, const void* text, size_t len,
                             const SkScalar pos[], int scalarsPerPos,
                             const SkPoint& offset, const SkPaint& paint) override;
    virtual void drawTextOnPath(const SkDraw&, const void* text, size_t len,
                                const SkPath& path, const SkMatrix* matrix,
                                const SkPaint& paint) override;
    virtual void drawVertices(const SkDraw&, SkCanvas::VertexMode, int vertexCount,
                              const SkPoint verts[], const SkPoint texs[],
                              const SkColor colors[], SkXfermode* xmode,
                              const uint16_t indices[], int indexCount,
                              const SkPaint& paint) override;

    virtual void drawDevice(const SkDraw&, SkBaseDevice*, int x, int y,
                            const SkPaint&) override;
    virtual const SkBitmap& onAccessBitmap() override;

private:
    SkSVGDevice(const SkISize& size, SkXMLWriter* writer);
    virtual ~SkSVGDevice();

    void drawBitmapCommon(const SkDraw& draw, const SkBitmap& bm, const SkPaint& paint);

    class AutoElement;
    class ResourceBucket;

    SkXMLWriter*                  fWriter;
    SkAutoTDelete<AutoElement>    fRootElement;
    SkAutoTDelete<ResourceBucket> fResourceBucket;
    SkBitmap                      fLegacyBitmap;
};

#endif // SkSVGDevice_DEFINED
