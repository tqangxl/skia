/*
 * Copyright 2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrBatchFontCache_DEFINED
#define GrBatchFontCache_DEFINED

#include "GrBatchAtlas.h"
#include "GrDrawTarget.h"
#include "GrFontScaler.h"
#include "GrGlyph.h"
#include "SkTDynamicHash.h"
#include "SkVarAlloc.h"

class GrBatchFontCache;
class GrBatchTarget;
class GrGpu;

/**
 *  The GrBatchTextStrike manages a pool of CPU backing memory for Glyph Masks.  This backing memory
 *  is abstracted by GrGlyph, and indexed by a PackedID and GrFontScaler.  The GrFontScaler is what
 *  actually creates the mask.
 */
class GrBatchTextStrike {
public:
    GrBatchTextStrike(GrBatchFontCache*, const GrFontDescKey* fontScalerKey);
    ~GrBatchTextStrike();

    const GrFontDescKey* getFontScalerKey() const { return fFontScalerKey; }
    GrBatchFontCache* getBatchFontCache() const { return fBatchFontCache; }

    inline GrGlyph* getGlyph(GrGlyph::PackedID packed, GrFontScaler* scaler) {
        GrGlyph* glyph = fCache.find(packed);
        if (NULL == glyph) {
            glyph = this->generateGlyph(packed, scaler);
        }
        return glyph;
    }

    // returns true if glyph (or glyph+padding for distance field)
    // is too large to ever fit in texture atlas subregions (GrPlots)
    bool glyphTooLargeForAtlas(GrGlyph*);
    // returns true if glyph successfully added to texture atlas, false otherwise
    bool addGlyphToAtlas(GrBatchTarget*, GrGlyph*, GrFontScaler*);

    // testing
    int countGlyphs() const { return fCache.count(); }

    // remove any references to this plot
    void removeID(GrBatchAtlas::AtlasID);

    static const GrFontDescKey& GetKey(const GrBatchTextStrike& ts) {
        return *(ts.fFontScalerKey);
    }
    static uint32_t Hash(const GrFontDescKey& key) {
        return key.getHash();
    }

private:
    SkTDynamicHash<GrGlyph, GrGlyph::PackedID> fCache;
    SkAutoTUnref<const GrFontDescKey> fFontScalerKey;
    SkVarAlloc fPool;

    GrBatchFontCache* fBatchFontCache;
    int fAtlasedGlyphs;

    GrGlyph* generateGlyph(GrGlyph::PackedID packed, GrFontScaler* scaler);

    friend class GrBatchFontCache;
};

/*
 * GrBatchFontCache manages strikes which are indexed by a GrFontScaler.  These strikes can then be
 * used to individual Glyph Masks.  The GrBatchFontCache also manages GrBatchAtlases, though this is
 * more or less transparent to the client(aside from atlasGeneration, described below).
 * Note - we used to initialize the backing atlas for the GrBatchFontCache at initialization time.
 * However, this caused a regression, even when the GrBatchFontCache was unused.  We now initialize
 * the backing atlases lazily.  Its not immediately clear why this improves the situation.
 */
class GrBatchFontCache {
public:
    GrBatchFontCache(GrContext*);
    ~GrBatchFontCache();

    inline GrBatchTextStrike* getStrike(GrFontScaler* scaler) {
        GrBatchTextStrike* strike = fCache.find(*(scaler->getKey()));
        if (NULL == strike) {
            strike = this->generateStrike(scaler);
        }
        return strike;
    }

    void freeAll();

    // if getTexture returns NULL, the client must not try to use other functions on the
    // GrBatchFontCache which use the atlas.  This function *must* be called first, before other
    // functions which use the atlas.
    GrTexture* getTexture(GrMaskFormat format) {
        if (this->initAtlas(format)) {
            return this->getAtlas(format)->getTexture();
        }
        return NULL;
    }

    bool hasGlyph(GrGlyph* glyph) {
        SkASSERT(glyph);
        return this->getAtlas(glyph->fMaskFormat)->hasID(glyph->fID);
    }

    // To ensure the GrBatchAtlas does not evict the Glyph Mask from its texture backing store,
    // the client must pass in the currentToken from the GrBatchTarget along with the GrGlyph.
    // A BulkUseTokenUpdater is used to manage bulk last use token updating in the Atlas.
    // For convenience, this function will also set the use token for the current glyph if required
    // NOTE: the bulk uploader is only valid if the subrun has a valid atlasGeneration
    void addGlyphToBulkAndSetUseToken(GrBatchAtlas::BulkUseTokenUpdater* updater,
                                      GrGlyph* glyph, GrBatchAtlas::BatchToken token) {
        SkASSERT(glyph);
        updater->add(glyph->fID);
        this->getAtlas(glyph->fMaskFormat)->setLastUseToken(glyph->fID, token);
    }

    void setUseTokenBulk(const GrBatchAtlas::BulkUseTokenUpdater& updater,
                         GrBatchAtlas::BatchToken token,
                         GrMaskFormat format) {
        this->getAtlas(format)->setLastUseTokenBulk(updater, token);
    }

    // add to texture atlas that matches this format
    bool addToAtlas(GrBatchTextStrike* strike, GrBatchAtlas::AtlasID* id,
                    GrBatchTarget* batchTarget,
                    GrMaskFormat format, int width, int height, const void* image,
                    SkIPoint16* loc) {
        fPreserveStrike = strike;
        return this->getAtlas(format)->addToAtlas(id, batchTarget, width, height, image, loc);
    }

    // Some clients may wish to verify the integrity of the texture backing store of the
    // GrBatchAtlas.  The atlasGeneration returned below is a monitonically increasing number which
    // changes everytime something is removed from the texture backing store.
    uint64_t atlasGeneration(GrMaskFormat format) const {
        return this->getAtlas(format)->atlasGeneration();
    }

    GrPixelConfig getPixelConfig(GrMaskFormat) const;

    void dump() const;

private:
    // There is a 1:1 mapping between GrMaskFormats and atlas indices
    static int MaskFormatToAtlasIndex(GrMaskFormat format) {
        static const int sAtlasIndices[] = {
            kA8_GrMaskFormat,
            kA565_GrMaskFormat,
            kARGB_GrMaskFormat,
        };
        SK_COMPILE_ASSERT(SK_ARRAY_COUNT(sAtlasIndices) == kMaskFormatCount, array_size_mismatch);

        SkASSERT(sAtlasIndices[format] < kMaskFormatCount);
        return sAtlasIndices[format];
    }

    bool initAtlas(GrMaskFormat);

    GrBatchTextStrike* generateStrike(GrFontScaler* scaler) {
        GrBatchTextStrike* strike = SkNEW_ARGS(GrBatchTextStrike, (this, scaler->getKey()));
        fCache.add(strike);
        return strike;
    }

    GrBatchAtlas* getAtlas(GrMaskFormat format) const {
        int atlasIndex = MaskFormatToAtlasIndex(format);
        SkASSERT(fAtlases[atlasIndex]);
        return fAtlases[atlasIndex];
    }

    static void HandleEviction(GrBatchAtlas::AtlasID, void*);

    GrContext* fContext;
    SkTDynamicHash<GrBatchTextStrike, GrFontDescKey> fCache;
    GrBatchAtlas* fAtlases[kMaskFormatCount];
    GrBatchTextStrike* fPreserveStrike;
};

#endif
