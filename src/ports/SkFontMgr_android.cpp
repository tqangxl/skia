/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkFontConfigParser_android.h"
#include "SkFontDescriptor.h"
#include "SkFontHost_FreeType_common.h"
#include "SkFontMgr.h"
#include "SkFontMgr_android.h"
#include "SkFontStyle.h"
#include "SkStream.h"
#include "SkTDArray.h"
#include "SkTSearch.h"
#include "SkTypeface.h"
#include "SkTypeface_android.h"
#include "SkTypefaceCache.h"

#include <limits>

// For test only.
static const char* gTestFontsXml = NULL;
static const char* gTestFallbackFontsXml = NULL;
static const char* gTestBasePath = NULL;

class SkTypeface_Android : public SkTypeface_FreeType {
public:
    SkTypeface_Android(int index,
                       const SkFontStyle& style,
                       bool isFixedPitch,
                       const SkString& familyName)
        : INHERITED(style, SkTypefaceCache::NewFontID(), isFixedPitch)
        , fIndex(index)
        , fFamilyName(familyName) { }

protected:
    void onGetFamilyName(SkString* familyName) const override {
        *familyName = fFamilyName;
    }

    int fIndex;
    SkString fFamilyName;

private:
    typedef SkTypeface_FreeType INHERITED;
};

class SkTypeface_AndroidSystem : public SkTypeface_Android {
public:
    SkTypeface_AndroidSystem(const SkString& pathName,
                             int index,
                             const SkFontStyle& style,
                             bool isFixedPitch,
                             const SkString& familyName,
                             const SkLanguage& lang,
                             FontVariant variantStyle)
        : INHERITED(index, style, isFixedPitch, familyName)
        , fPathName(pathName)
        , fLang(lang)
        , fVariantStyle(variantStyle) { }

    virtual void onGetFontDescriptor(SkFontDescriptor* desc,
                                     bool* serialize) const override {
        SkASSERT(desc);
        SkASSERT(serialize);
        desc->setFamilyName(fFamilyName.c_str());
        desc->setFontFileName(fPathName.c_str());
        desc->setFontIndex(fIndex);
        *serialize = false;
    }
    SkStreamAsset* onOpenStream(int* ttcIndex) const override {
        *ttcIndex = fIndex;
        return SkStream::NewFromFile(fPathName.c_str());
    }

    const SkString fPathName;
    const SkLanguage fLang;
    const FontVariant fVariantStyle;

    typedef SkTypeface_Android INHERITED;
};

class SkTypeface_AndroidStream : public SkTypeface_Android {
public:
    SkTypeface_AndroidStream(SkStreamAsset* stream,
                             int index,
                             const SkFontStyle& style,
                             bool isFixedPitch,
                             const SkString& familyName)
        : INHERITED(index, style, isFixedPitch, familyName)
        , fStream(stream) { }

    virtual void onGetFontDescriptor(SkFontDescriptor* desc,
                                     bool* serialize) const override {
        SkASSERT(desc);
        SkASSERT(serialize);
        desc->setFamilyName(fFamilyName.c_str());
        desc->setFontFileName(NULL);
        *serialize = true;
    }

    SkStreamAsset* onOpenStream(int* ttcIndex) const override {
        *ttcIndex = fIndex;
        return fStream->duplicate();
    }

private:
    SkAutoTDelete<SkStreamAsset> fStream;

    typedef SkTypeface_Android INHERITED;
};

class SkFontStyleSet_Android : public SkFontStyleSet {
public:
    explicit SkFontStyleSet_Android(const FontFamily& family,
                                    const SkTypeface_FreeType::Scanner& scanner)
    {
        const SkString* cannonicalFamilyName = NULL;
        if (family.fNames.count() > 0) {
            cannonicalFamilyName = &family.fNames[0];
        }
        // TODO? make this lazy
        for (int i = 0; i < family.fFonts.count(); ++i) {
            const FontFileInfo& fontFile = family.fFonts[i];

            SkString pathName(family.fBasePath);
            pathName.append(fontFile.fFileName);

            SkAutoTDelete<SkStream> stream(SkStream::NewFromFile(pathName.c_str()));
            if (!stream.get()) {
                SkDEBUGF(("Requested font file %s does not exist or cannot be opened.\n",
                          pathName.c_str()));
                continue;
            }

            const int ttcIndex = fontFile.fIndex;
            SkString familyName;
            SkFontStyle style;
            bool isFixedWidth;
            if (!scanner.scanFont(stream.get(), ttcIndex, &familyName, &style, &isFixedWidth)) {
                SkDEBUGF(("Requested font file %s exists, but is not a valid font.\n",
                          pathName.c_str()));
                continue;
            }

            if (fontFile.fWeight != 0) {
                style = SkFontStyle(fontFile.fWeight, style.width(), style.slant());
            }

            const SkLanguage& lang = family.fLanguage;
            uint32_t variant = family.fVariant;
            if (kDefault_FontVariant == variant) {
                variant = kCompact_FontVariant | kElegant_FontVariant;
            }

            // The first specified family name overrides the family name found in the font.
            // TODO: SkTypeface_AndroidSystem::onCreateFamilyNameIterator should return
            // all of the specified family names in addition to the names found in the font.
            if (cannonicalFamilyName != NULL) {
                familyName = *cannonicalFamilyName;
            }

            fStyles.push_back().reset(SkNEW_ARGS(SkTypeface_AndroidSystem,
                                                 (pathName, ttcIndex,
                                                  style, isFixedWidth, familyName,
                                                  lang, variant)));
        }
    }

    int count() override {
        return fStyles.count();
    }
    void getStyle(int index, SkFontStyle* style, SkString* name) override {
        if (index < 0 || fStyles.count() <= index) {
            return;
        }
        if (style) {
            *style = this->style(index);
        }
        if (name) {
            name->reset();
        }
    }
    SkTypeface_AndroidSystem* createTypeface(int index) override {
        if (index < 0 || fStyles.count() <= index) {
            return NULL;
        }
        return SkRef(fStyles[index].get());
    }

    /** Find the typeface in this style set that most closely matches the given pattern.
     *  TODO: consider replacing with SkStyleSet_Indirect::matchStyle();
     *  this simpler version using match_score() passes all our tests.
     */
    SkTypeface_AndroidSystem* matchStyle(const SkFontStyle& pattern) override {
        if (0 == fStyles.count()) {
            return NULL;
        }
        SkTypeface_AndroidSystem* closest = fStyles[0];
        int minScore = std::numeric_limits<int>::max();
        for (int i = 0; i < fStyles.count(); ++i) {
            SkFontStyle style = this->style(i);
            int score = match_score(pattern, style);
            if (score < minScore) {
                closest = fStyles[i];
                minScore = score;
            }
        }
        return SkRef(closest);
    }

private:
    SkFontStyle style(int index) {
        return fStyles[index]->fontStyle();
    }
    static int match_score(const SkFontStyle& pattern, const SkFontStyle& candidate) {
        int score = 0;
        score += abs((pattern.width() - candidate.width()) * 100);
        score += abs((pattern.isItalic() == candidate.isItalic()) ? 0 : 1000);
        score += abs(pattern.weight() - candidate.weight());
        return score;
    }

    SkTArray<SkAutoTUnref<SkTypeface_AndroidSystem>, true> fStyles;

    friend struct NameToFamily;
    friend class SkFontMgr_Android;

    typedef SkFontStyleSet INHERITED;
};

/** On Android a single family can have many names, but our API assumes unique names.
 *  Map names to the back end so that all names for a given family refer to the same
 *  (non-replicated) set of typefaces.
 *  SkTDict<> doesn't let us do index-based lookup, so we write our own mapping.
 */
struct NameToFamily {
    SkString name;
    SkFontStyleSet_Android* styleSet;
};

class SkFontMgr_Android : public SkFontMgr {
public:
    SkFontMgr_Android(const SkFontMgr_Android_CustomFonts* custom) {
        SkTDArray<FontFamily*> families;
        if (custom && SkFontMgr_Android_CustomFonts::kPreferSystem != custom->fSystemFontUse) {
            SkString base(custom->fBasePath);
            SkFontConfigParser::GetCustomFontFamilies(families, base,
                                                      custom->fFontsXml, custom->fFallbackFontsXml);
        }
        if (!custom ||
            (custom && SkFontMgr_Android_CustomFonts::kOnlyCustom != custom->fSystemFontUse))
        {
            SkFontConfigParser::GetSystemFontFamilies(families);
        }
        if (custom && SkFontMgr_Android_CustomFonts::kPreferSystem == custom->fSystemFontUse) {
            SkString base(custom->fBasePath);
            SkFontConfigParser::GetCustomFontFamilies(families, base,
                                                      custom->fFontsXml, custom->fFallbackFontsXml);
        }
        this->buildNameToFamilyMap(families);
        this->findDefaultFont();
        families.deleteAll();
    }

protected:
    /** Returns not how many families we have, but how many unique names
     *  exist among the families.
     */
    int onCountFamilies() const override {
        return fNameToFamilyMap.count();
    }

    void onGetFamilyName(int index, SkString* familyName) const override {
        if (index < 0 || fNameToFamilyMap.count() <= index) {
            familyName->reset();
            return;
        }
        familyName->set(fNameToFamilyMap[index].name);
    }

    SkFontStyleSet* onCreateStyleSet(int index) const override {
        if (index < 0 || fNameToFamilyMap.count() <= index) {
            return NULL;
        }
        return SkRef(fNameToFamilyMap[index].styleSet);
    }

    SkFontStyleSet* onMatchFamily(const char familyName[]) const override {
        if (!familyName) {
            return NULL;
        }
        SkAutoAsciiToLC tolc(familyName);
        for (int i = 0; i < fNameToFamilyMap.count(); ++i) {
            if (fNameToFamilyMap[i].name.equals(tolc.lc())) {
                return SkRef(fNameToFamilyMap[i].styleSet);
            }
        }
        // TODO: eventually we should not need to name fallback families.
        for (int i = 0; i < fFallbackNameToFamilyMap.count(); ++i) {
            if (fFallbackNameToFamilyMap[i].name.equals(tolc.lc())) {
                return SkRef(fFallbackNameToFamilyMap[i].styleSet);
            }
        }
        return NULL;
    }

    virtual SkTypeface* onMatchFamilyStyle(const char familyName[],
                                           const SkFontStyle& style) const override {
        SkAutoTUnref<SkFontStyleSet> sset(this->matchFamily(familyName));
        return sset->matchStyle(style);
    }

    virtual SkTypeface* onMatchFaceStyle(const SkTypeface* typeface,
                                         const SkFontStyle& style) const override {
        for (int i = 0; i < fFontStyleSets.count(); ++i) {
            for (int j = 0; j < fFontStyleSets[i]->fStyles.count(); ++j) {
                if (fFontStyleSets[i]->fStyles[j] == typeface) {
                    return fFontStyleSets[i]->matchStyle(style);
                }
            }
        }
        return NULL;
    }

    static SkTypeface_AndroidSystem* find_family_style_character(
            const SkTDArray<NameToFamily>& fallbackNameToFamilyMap,
            const SkFontStyle& style, bool elegant,
            const SkString& langTag, SkUnichar character)
    {
        for (int i = 0; i < fallbackNameToFamilyMap.count(); ++i) {
            SkFontStyleSet_Android* family = fallbackNameToFamilyMap[i].styleSet;
            SkAutoTUnref<SkTypeface_AndroidSystem> face(family->matchStyle(style));

            if (!langTag.isEmpty() && !face->fLang.getTag().startsWith(langTag.c_str())) {
                continue;
            }

            if (SkToBool(face->fVariantStyle & kElegant_FontVariant) != elegant) {
                continue;
            }

            SkPaint paint;
            paint.setTypeface(face);
            paint.setTextEncoding(SkPaint::kUTF32_TextEncoding);

            uint16_t glyphID;
            paint.textToGlyphs(&character, sizeof(character), &glyphID);
            if (glyphID != 0) {
                return face.detach();
            }
        }
        return NULL;
    }

    virtual SkTypeface* onMatchFamilyStyleCharacter(const char familyName[],
                                                    const SkFontStyle& style,
                                                    const char* bcp47[],
                                                    int bcp47Count,
                                                    SkUnichar character) const override
    {
        // The variant 'elegant' is 'not squashed', 'compact' is 'stays in ascent/descent'.
        // The variant 'default' means 'compact and elegant'.
        // As a result, it is not possible to know the variant context from the font alone.
        // TODO: add 'is_elegant' and 'is_compact' bits to 'style' request.

        // The first time match anything elegant, second time anything not elegant.
        for (int elegant = 2; elegant --> 0;) {
            for (int bcp47Index = bcp47Count; bcp47Index --> 0;) {
                SkLanguage lang(bcp47[bcp47Index]);
                while (!lang.getTag().isEmpty()) {
                    SkTypeface_AndroidSystem* matchingTypeface =
                        find_family_style_character(fFallbackNameToFamilyMap,
                                                    style, SkToBool(elegant),
                                                    lang.getTag(), character);
                    if (matchingTypeface) {
                        return matchingTypeface;
                    }

                    lang = lang.getParent();
                }
            }
            SkTypeface_AndroidSystem* matchingTypeface =
                find_family_style_character(fFallbackNameToFamilyMap,
                                            style, SkToBool(elegant),
                                            SkString(), character);
            if (matchingTypeface) {
                return matchingTypeface;
            }
        }
        return NULL;
    }

    SkTypeface* onCreateFromData(SkData* data, int ttcIndex) const override {
        return this->createFromStream(new SkMemoryStream(data), ttcIndex);
    }

    SkTypeface* onCreateFromFile(const char path[], int ttcIndex) const override {
        SkAutoTDelete<SkStreamAsset> stream(SkStream::NewFromFile(path));
        return stream.get() ? this->createFromStream(stream.detach(), ttcIndex) : NULL;
    }

    SkTypeface* onCreateFromStream(SkStreamAsset* bareStream, int ttcIndex) const override {
        SkAutoTDelete<SkStreamAsset> stream(bareStream);
        bool isFixedPitch;
        SkFontStyle style;
        SkString name;
        if (!fScanner.scanFont(stream, ttcIndex, &name, &style, &isFixedPitch)) {
            return NULL;
        }
        return SkNEW_ARGS(SkTypeface_AndroidStream, (stream.detach(), ttcIndex,
                                                     style, isFixedPitch, name));
    }


    virtual SkTypeface* onLegacyCreateTypeface(const char familyName[],
                                               unsigned styleBits) const override {
        SkFontStyle style = SkFontStyle(styleBits);

        if (familyName) {
            // On Android, we must return NULL when we can't find the requested
            // named typeface so that the system/app can provide their own recovery
            // mechanism. On other platforms we'd provide a typeface from the
            // default family instead.
            return this->onMatchFamilyStyle(familyName, style);
        }
        return fDefaultFamily->matchStyle(style);
    }


private:

    SkTypeface_FreeType::Scanner fScanner;

    SkTArray<SkAutoTUnref<SkFontStyleSet_Android>, true> fFontStyleSets;
    SkFontStyleSet* fDefaultFamily;
    SkTypeface* fDefaultTypeface;

    SkTDArray<NameToFamily> fNameToFamilyMap;
    SkTDArray<NameToFamily> fFallbackNameToFamilyMap;

    void buildNameToFamilyMap(SkTDArray<FontFamily*> families) {
        for (int i = 0; i < families.count(); i++) {
            FontFamily& family = *families[i];

            SkTDArray<NameToFamily>* nameToFamily = &fNameToFamilyMap;
            if (family.fIsFallbackFont) {
                nameToFamily = &fFallbackNameToFamilyMap;

                if (0 == family.fNames.count()) {
                    SkString& fallbackName = family.fNames.push_back();
                    fallbackName.printf("%.2x##fallback", i);
                }
            }

            SkFontStyleSet_Android* newSet =
                SkNEW_ARGS(SkFontStyleSet_Android, (family, fScanner));
            if (0 == newSet->count()) {
                SkDELETE(newSet);
                continue;
            }
            fFontStyleSets.push_back().reset(newSet);

            for (int j = 0; j < family.fNames.count(); j++) {
                NameToFamily* nextEntry = nameToFamily->append();
                SkNEW_PLACEMENT_ARGS(&nextEntry->name, SkString, (family.fNames[j]));
                nextEntry->styleSet = newSet;
            }
        }
    }

    void findDefaultFont() {
        SkASSERT(!fFontStyleSets.empty());

        static const char* gDefaultNames[] = { "sans-serif" };
        for (size_t i = 0; i < SK_ARRAY_COUNT(gDefaultNames); ++i) {
            SkFontStyleSet* set = this->onMatchFamily(gDefaultNames[i]);
            if (NULL == set) {
                continue;
            }
            SkTypeface* tf = set->matchStyle(SkFontStyle());
            if (NULL == tf) {
                continue;
            }
            fDefaultFamily = set;
            fDefaultTypeface = tf;
            break;
        }
        if (NULL == fDefaultTypeface) {
            fDefaultFamily = fFontStyleSets[0];
            fDefaultTypeface = fDefaultFamily->createTypeface(0);
        }
        SkASSERT(fDefaultFamily);
        SkASSERT(fDefaultTypeface);
    }

    typedef SkFontMgr INHERITED;
};

///////////////////////////////////////////////////////////////////////////////
#ifdef SK_DEBUG
static char const * const gSystemFontUseStrings[] = {
    "OnlyCustom", "PreferCustom", "PreferSystem"
};
#endif
SkFontMgr* SkFontMgr_New_Android(const SkFontMgr_Android_CustomFonts* custom) {
    if (custom) {
        SkASSERT(0 <= custom->fSystemFontUse);
        SkASSERT(custom->fSystemFontUse < SK_ARRAY_COUNT(gSystemFontUseStrings));
        SkDEBUGF(("SystemFontUse: %s BasePath: %s Fonts: %s FallbackFonts: %s\n",
                  gSystemFontUseStrings[custom->fSystemFontUse],
                  custom->fBasePath,
                  custom->fFontsXml,
                  custom->fFallbackFontsXml));
    }

    return SkNEW_ARGS(SkFontMgr_Android, (custom));
}

SkFontMgr* SkFontMgr::Factory() {
    // These globals exist so that Chromium can override the environment.
    // TODO: these globals need to be removed, and Chromium use SkFontMgr_New_Android instead.
    if ((gTestFontsXml || gTestFallbackFontsXml) && gTestBasePath) {
        SkFontMgr_Android_CustomFonts custom = {
            SkFontMgr_Android_CustomFonts::kOnlyCustom,
            gTestBasePath,
            gTestFontsXml,
            gTestFallbackFontsXml
        };
        return SkFontMgr_New_Android(&custom);
    }

    return SkFontMgr_New_Android(NULL);
}

void SkUseTestFontConfigFile(const char* fontsXml, const char* fallbackFontsXml,
                             const char* basePath)
{
    gTestFontsXml = fontsXml;
    gTestFallbackFontsXml = fallbackFontsXml;
    gTestBasePath = basePath;
    SkASSERT(gTestFontsXml);
    SkASSERT(gTestFallbackFontsXml);
    SkASSERT(gTestBasePath);
    SkDEBUGF(("Test BasePath: %s Fonts: %s FallbackFonts: %s\n",
              gTestBasePath, gTestFontsXml, gTestFallbackFontsXml));
}
