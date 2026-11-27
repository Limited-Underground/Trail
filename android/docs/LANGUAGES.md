# App languages

MeshTrail app text is translated with ordinary Android string resources (D-096, D-192).

## Languages

| Language | Folder | Picker name | State |
|---|---|---|---|
| English (base) | `values` | English | Source text |
| Spanish | `values-es` | Español | Unreviewed draft |
| Italian | `values-it` | Italiano | Unreviewed draft |
| French | `values-fr` | Français | Unreviewed draft |
| German | `values-de` | Deutsch | Unreviewed draft |
| Croatian | `values-hr` | Hrvatski | Unreviewed draft |
| Serbian, Latin script | `values-b+sr+Latn` | Srpski | Unreviewed draft (D-196) |
| Serbian, Cyrillic script | `values-sr` | Српски | Unreviewed draft, converted letter by letter from the Latin file (D-196) |
| Russian | `values-ru` | Русский | Unreviewed draft |
| Ukrainian | `values-uk` | Українська | Unreviewed draft |

Serbian Cyrillic uses the plain `values-sr` folder: Android treats Serbian without a script as Cyrillic, so phones set to Serbian, Serbian (Serbia) or Serbian (Cyrillic) all get it, and only Serbian (Latin) phones get `values-b+sr+Latn`. `AppLanguageResolutionTest` checks this. When a Serbian string changes, change both files.

The list lives in three places that must agree: `AppLanguage.kt`, `res/xml/locales_config.xml` and the `res/values-*` folders. `AppLanguageResourcesTest` fails if they drift.

## Choosing a language

- Device tab, App settings, Language: System default or one of the languages above, each in its own name.
- Android 13 and later: the system stores the choice; the phone's own app settings also offer it (App language).
- Android 8 to 12: the app stores it and applies it to its screen when the screen starts.
- The connected-device notification text is still English in Kotlin; it moves with the rest of the text.

## Adding or changing text

1. Put the English text in `res/values/strings.xml` and use `stringResource(R.string.…)` in Compose (or `context.getString` outside Compose). Do not write user-facing text in Kotlin.
2. Add the same name to every `values-*/strings.xml`. Lint (`MissingTranslation`, an error here) and `AppLanguageResourcesTest` both fail if one is missing or extra.
3. Counts use `<plurals>`. Russian and Ukrainian need `one`, `few`, `many` and `other`; Serbian and Croatian need `one`, `few` and `other`; lint `MissingQuantity` checks this.
4. Escape apostrophes as `\'` (French and Italian use them often).
5. Names never translate: the product name is `translatable="false"`. New user-facing names use MeshTrail (D-100).
6. Keep labels short. `AppLanguageLayoutTest` renders the tab bar, App settings and the language list in every language on a 360 dp phone and fails if text is cut off, leaves the screen, or a tab label wraps.

## Review state

Every non-English file starts with a comment whose first word is `UNREVIEWED:` or `REVIEWED:` (the test checks this). Drafts written by an AI stay `UNREVIEWED` until a native speaker reads them in the app; then change the word and add the reviewer and date.

## Text not yet moved

Most screens still have English written in Kotlin. `tools/Get-AppHardcodedText.ps1` lists it per file (641 likely user-facing literals on 2026-11-26). Move one screen at a time; tests that find controls by English text keep working because tests run in English.

Not covered by the app: the board screen (firmware OLED font has only capital A-Z and digits, so accented and Cyrillic letters need new glyphs first), the Firmware Loader and the website.
