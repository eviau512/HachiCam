# Artwork & Visual Assets Notice

The source code of this project is licensed under **GPLv3**.  
The technical specifications under `specs/` are dedicated to the public domain under **CC0 1.0**.

**However, the application icon, character artwork, and related visual assets located under `app/src/hachimi/res/` are NOT covered by either of the above licenses.**

---

## Existing icon and character assets

The app icon and character imagery are derived from internet fan culture originating in Chinese online communities — specifically from the "哈基米 (Hachimi)" meme, which spread as a derivative of the "LLM娘" fan art tradition and related community-created illustrations.

The original creators of these works are unknown to the project maintainers, and no explicit copyright license has been granted to this project for their use. These assets are used here informally, in the spirit of community sharing and non-commercial fan culture, without any claim of ownership.

## Settings banner added 2026-10-13

The original master `artwork/settings-banner/gpt-gemini-banner-v2.png` is AI-generated fan art
made for this project with the built-in image generation tool, using the user's
pose sketch and community character references. They depict GPT as a white
dragon girl and Gemini / Hachimi as a blue-violet cat girl. They are not official
OpenAI or Google character art and do not imply endorsement.

`artwork/settings-banner/gpt-gemini-banner-v2-4k.png` is a super-resolution
derivative of that selected scene, created with the official Real-ESRGAN anime
illustration model and reduced from its native 4x result to 3840 x 2160.
This processing does not change the character-design provenance or the artwork
rights statements below. The model/tool are local processing dependencies,
not bundled Android resources; their source and hashes remain in the locally
retained, Git-ignored `artwork/settings-banner/SUPER_RESOLUTION.json`.

The latest Android runtime asset is
`app/src/hachimi/res/drawable-nodpi/settings_character_banner.webp`, copied from
`artwork/settings-banner/gpt-gemini-banner-v2-native-4x.webp`. It losslessly encodes
the selected native 6688 x 3764 result. Decoded RGB equality and hashes remain in
the local, Git-ignored `artwork/settings-banner/WEBP_EXPORT.json`; conversion does
not alter provenance. Its SHA256 is
`396dc8ef08874fc0b3716633ea8eff82ca1d8c545e4f8f9f22df9e9836eff519`.

Character-design reference credit: **ZipZipPipe**. The creator's
[大AI和小AI们](https://www.bilibili.com/video/BV1tE9XBbErS/) identifies the nine-model
fan-art series. The two individual reference images were obtained from a
[community repost](https://linux.do/t/topic/2878430/9) attributing them to
ZipZipPipe; their individual authorship and license were not independently
verified against a creator-hosted original file. The new scene changes the
pose, expressions, framing and background. Prompts and exact reference URLs
are retained locally in the Git-ignored notes under `artwork/settings-banner/`.

The creator's video description explicitly applies **CC BY-NC-SA 4.0 to the
whale-girl material**, crediting 上善无形. That statement must not be silently
extended to the separate GPT/Gemini reference files. The banner contains no
DeepSeek whale-girl character. This project grants no additional rights to
underlying third-party designs through its GPL code license, CC0 specifications
or the fact that the new scene was AI-generated. No new permissive artwork
license is asserted here; the individual GPT/Gemini design licensing remains
unverified.

The 11 downloaded third-party reference images in
`artwork/settings-banner/references/` are local research material only, excluded
by the repository's existing `references/` ignore rule. They are not Android
resources. The local, Git-ignored `REFERENCE_MANIFEST.json` preserves their source/author/license
metadata without treating an archive's download availability or code license
as an artwork license.

---

## Full-body photography-test artworks added 2026-10-18

The two masters under `artwork/photography-test-2026-10-18/` are newly
AI-generated Gemini / Hachimi and GPT character illustrations. They reuse the
same community character-design references credited above. The cropped
[OpenCV Lenna sample](https://github.com/opencv/opencv/blob/4.x/samples/data/lena.jpg)
was used only as a head/shoulder pose guide; the scenes, full-body standing poses
and clothing are new illustrations. No photo pixels or real-person likeness
were composited into the outputs. The pose reference stays in the ignored
research directory; this does not assert a license to the underlying photograph.

The imagegen tool generated the native scenes. Real-ESRGAN anime SR and exact-size
resampling produced 6000 x 8000 digital masters; Gemini also uses a documented
source-guided endpoint guard. These processing steps do not grant new rights to
underlying character designs. References, prompts, native/intermediate images,
processing code and verification records remain locally under Git-ignored paths;
only the two final PNGs from this set are versioned. Their SHA256 hashes are:

- `gemini-lenna-fullbody-6000x8000.png`:
  `c41e385efe8d49e6f37e38950e4f7e26dbdd48224ec8ddf117f0e3e2a42cb11e`.
- `gpt-lenna-fullbody-6000x8000.png`:
  `d68844527e6b71561308620f7cf6d14afd7883b11a353d894c74b2b322acc88e`.

Attribution and rights limitations remain in this tracked notice. These are non-official, uncalibrated
photography-test artworks and are not packaged with the Android application.

## Implications for Downstream Distribution

If you fork, redistribute, or commercially use this project:

- **Do not assume these visual assets are free to use.** They are not owned by this project and carry no clear open-source license.
- **You are strongly advised to replace all artwork** under `app/src/hachimi/res/` with assets that you own outright or that carry an explicit permissive license (e.g. CC0, Apache 2.0).
- The project maintainers accept no liability for any copyright issues arising from downstream use of these visual assets.

---

## Takedown Requests

If you are the original creator of any artwork used in this project and wish to request its removal or replacement, please open an issue. We will respond promptly.
