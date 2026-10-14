# Settings Banner Validation

## 2026-10-14 selected-source recheck

The user reconfirmed the largest 25,863,086-byte native 4x PNG (6688 x 3764).
Its SHA-256 is `715b346ae4d790c4633afd5538990f46ef73d6aa19ae706e5481fa041e680b78`.
The existing archived and packaged lossless WebP already match that selection:
17,376,128 bytes each, SHA-256
`396dc8ef08874fc0b3716633ea8eff82ca1d8c545e4f8f9f22df9e9836eff519`.
All three decode to identical RGB, SHA-256
`9740fb6cf97b5b2218379889340cee81896009f36da9e6d00d771880c341c782`.
No resource replacement or new APK build is necessary for this reconfirmation.
The prior rc9 artifact checks verified that WebP in the packaged universal APK.
Earlier validation below records historical steps and does not override this selection.

Date: 2026-10-13 (Asia/Taipei).

## Latest runtime replacement

At the user's request, native 4x (6688x3764) is now exported as lossless WebP.
Decoded RGB equality against the native PNG passed. Master and runtime copy:
17,376,128 bytes; SHA-256
`396dc8ef08874fc0b3716633ea8eff82ca1d8c545e4f8f9f22df9e9836eff519`.
The PNG runtime was removed; original and 4K archival PNGs remain. The Hachimi
settings footer asynchronously decodes at the constrained display width with
power-of-two sampling and a 4096-pixel target cap, preserving the full aspect.
The historical records below describe the earlier PNG delivery.

## Original scene and runtime artifact

- Master: `gpt-gemini-banner-v2.png`.
- Runtime copy: `app/src/hachimi/res/drawable-nodpi/settings_character_banner.png`.
- Dimensions: 1672 × 941; ratio 1.7768331562, inside the requested 4:3–16:9 range.
- Encoding: PNG, opaque artwork; 2,493,623 bytes.
- SHA-256 (both copies):
  `2f3bc1999b5b1e0aacd1f9b4aa970109907a5a1460e6b6d5ad269aa2c0ed1837`.
- The built-in imagegen tool generated the scene and performed one targeted
  framing/expression revision. Prompts are saved alongside the master.

## Visual inspection

The final generation was inspected directly. It contains exactly two characters:
white-haired, white-horned dragon GPT behind/right; blue-violet cat-eared Gemini
in front/left. GPT's rosy blush, Gemini's surprised heterochromatic eyes/open
mouth, and the pink forehead flush are visible. The back embrace, forearms and
hands read as the requested pose. Both horns and both cat ears are entirely
inside the final frame. There is no embedded text or watermark.

The earlier candidate clipped the dragon horns at the top. It was not selected
for the project resource. The selected final has added upper framing space.

## Implementation checks executed

Using the repository's JDK 17, Android SDK and existing offline Gradle dependencies:

```text
:app:compileHachimiDebugKotlin
:app:compileStandardDebugKotlin
:app:processHachimiDebugResources
:app:processStandardDebugResources
-Pscanner.cmakeVersion=4.1.2 --offline --console=plain
```

Result: **BUILD SUCCESSFUL**, 30 actionable tasks. Only the existing camera API
and Gradle deprecation warnings were reported. The build log is local at
`.tools/settings-banner-build.log`.

Additional checks passed:

- Exact master/runtime-copy SHA-256 equality and native dimensions.
- `settings_character_banner` is present in the generated Hachimi debug resource
  symbols and absent from standard debug resource symbols.
- Hachimi's lazy-list extension adds the image as a complete native-ratio item;
  standard's extension adds no item. Rounded clipping is 20 dp, with no crop,
  click action or content description. Existing list insets supply 16 dp padding.
- All 11 third-party reference-file checksums match `REFERENCE_MANIFEST.json`;
  each reference was viewed. The user sketch was copied separately.
- The existing ignore rule excludes downloaded reference images and the user
  sketch. Third-party references are outside Android resource directories.
- Scoped `git diff --check` passed for this task's affected paths.

## Not executed / limits

No physical-device or emulator settings navigation, screenshot, rotation,
TalkBack or low-memory runtime test was run. Compilation and source/resource
inspection do not establish those runtime results. The image was not installed
on a phone, and no new APK was delivered as part of this artwork task.

The reference set is a curated sample of 11 model categories, not a complete
Telegram pack export or an exhaustive catalogue. Individual reference-file
authorship/licensing gaps are preserved in the research and artwork notice.

## 4K super-resolution follow-up

Executed on 2026-10-13, after revising SPEC_05 and UI-07:

1. The built-in image edit tool was asked for 3840 x 2160 using the selected
   scene as its edit target. The actual returned file was again 1672 x 941.
   This candidate was not accepted as a higher-resolution delivery. Its prompt
   is retained in `UPSCALE_4K_PROMPT.txt`.
2. For the user's explicit super-resolution request, the original selected v2
   scene was processed directly by the official
   [Real-ESRGAN portable Windows release](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0),
   with `realesrgan-x4plus-anime`, scale 4, tile 256, load/process/save threads
   `1:1:1`, Vulkan on Intel UHD Graphics. This is the dedicated
   [anime illustration model](https://github.com/xinntao/Real-ESRGAN/blob/master/docs/anime_model.md).
   No generative scene replacement or face-restoration stage was applied to the
   delivered version.
3. Native output dimensions were verified as 6688 x 3764. The native result is
   retained locally at `.tools/banner-super-resolution/gpt-gemini-native-4x.png`
   (25,863,086 bytes).
4. For exact 16:9 output, one native-4x row was trimmed at each vertical edge
   (0.25 original pixel per edge), then a high-quality bicubic reduction exported
   3840 x 2160 PNG. There was no change to character framing at a meaningful scale.

The deliverable is `gpt-gemini-banner-v2-4k.png`, **3840 x 2160**,
15,547,098 bytes. SHA-256:
`30e2792bb9ac28be7a5b69fdb40671f952a58796fa0cd3cc026f26fad3475ea3`.

The 4K result was viewed at original resolution and compared with the v2 scene:
faces, eyes, forehead/cheek blush, horns, ears, star ornament, four hands and the
rear embrace retain their identity and positions. Linework remains clean, with
no obvious tile seams or broad sharpening halos. Fine reconstructed/smoothed
lines are model estimates, not proof of additional original source information.

Original v2 and Android runtime-resource SHA-256 remain unchanged. No app source
or runtime asset changed in this follow-up, so the earlier successful flavor
builds were not repeated. `SUPER_RESOLUTION.json` records tool/model and output
hashes; the local processing script and inference log are under
`.tools/banner-super-resolution/`.
