# Artwork & Visual Assets Notice

Updated 2026-10-28. This project uses separate licenses for code, specifications
and artwork:

| Material | License / rights notice |
| --- | --- |
| Project source code | [GNU GPLv3](LICENSE) |
| Technical specifications under `specs/` | [CC0 1.0](specs/LICENSE) |
| All artwork published under `artwork/`, and the identical settings-banner copy bundled in the app | [CC BY-NC-SA 4.0](artwork/LICENSE); [attribution](artwork/README.md) |
| Other legacy app icons under `app/src/hachimi/res/` | Separate provenance status, described below |

## Original creators and artwork license

Original artwork and character-design sources are **上善无形** and **ZipZipPipe**.
Original copyrights remain with the respective creators. HachiCam's published
artwork and its adaptations are distributed under **Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)**.
The complete license accompanies the images in [artwork/LICENSE](artwork/LICENSE).

- **上善无形**: [original whale-girl publication](https://www.bilibili.com/opus/1231977657712771073),
  which explicitly states CC BY-NC-SA 4.0;
  [creator profile](https://space.bilibili.com/4456176).
- **ZipZipPipe**: [DeepSeek Whale-chan source repository](https://github.com/Neko3000/deepseek-whalechan),
  [《大AI和小AI们》](https://www.bilibili.com/video/BV1tE9XBbErS/),
  [creator profile](https://space.bilibili.com/4168597).

The maintainer supplied these sources and confirmed the artwork's CC BY-NC-SA
4.0 basis on 2026-10-28. This supersedes this notice's earlier unverified-license
description of the published banner and photography-test illustrations. It does
not assert that both creators drew every individual image. The linked source
repository uses scoped licensing: its CC documentation license does not itself
grant rights to all third-party reference images, as its
[third-party notice](https://github.com/Neko3000/deepseek-whalechan/blob/main/THIRD-PARTY-NOTICES.md)
explains. External research inputs retain their own rights.

When sharing the published artwork, preserve the original creators' names,
source links, license and notices, indicate modifications, use it only for
non-commercial purposes and share adaptations under CC BY-NC-SA 4.0. The full
license governs reuse. AI generation, upscaling and encoding do not transfer
ownership of the original character designs or imply endorsement by the
creators, OpenAI, Google or other named organizations.

## Settings banner added 2026-10-13

The master `artwork/settings-banner/gpt-gemini-banner-v2.png` is AI-generated fan
art made for HachiCam with the built-in image generation tool, using the user's
pose sketch and community character references. It depicts GPT as a white
dragon girl and Gemini / Hachimi as a blue-violet cat girl. The new scene changes
the pose, expressions, framing and background; it contains no whale-girl figure.
Character references were credited to ZipZipPipe by the
[community repost](https://linux.do/t/topic/2878430/9).

`gpt-gemini-banner-v2-4k.png` is a 3840 × 2160 derivative made with Real-ESRGAN's
anime illustration model. `gpt-gemini-banner-v2-native-4x.webp` losslessly encodes
the selected native 6688 × 3764 result. Its identical Android copy is
`app/src/hachimi/res/drawable-nodpi/settings_character_banner.webp`, with SHA256
`396dc8ef08874fc0b3716633ea8eff82ca1d8c545e4f8f9f22df9e9836eff519`.
These derivatives retain the same CC BY-NC-SA 4.0 terms and original credits.

Prompts, exact reference URLs, model/tool hashes, export parameters and decoded
RGB verification remain in the local, Git-ignored working records under
`artwork/settings-banner/`. The 11 downloaded third-party reference images are
local research inputs, not distributed app resources. Their availability or an
archive's code license does not establish an image license.

## Full-body photography-test artworks added 2026-10-18

The two final PNGs under `artwork/photography-test-2026-10-18/` are newly
AI-generated Gemini / Hachimi and GPT illustrations using the same community
character-design references. The cropped
[OpenCV Lenna sample](https://github.com/opencv/opencv/blob/4.x/samples/data/lena.jpg)
was used only as a head/shoulder pose guide. The scenes, full-body poses and
clothing are new illustrations; no photograph pixels or real-person likeness
were composited into the outputs. The photograph is not distributed here and
this notice grants no rights to it.

Real-ESRGAN anime super-resolution and resizing produced 6000 × 8000 masters;
Gemini also uses a source-guided brightness endpoint guard. These are uncalibrated
photography-test illustrations, not native 48 MP captures, and are not packaged
with the Android application. Both use CC BY-NC-SA 4.0 with the credits above.
Their SHA256 hashes are:

- `gemini-lenna-fullbody-6000x8000.png`:
  `c41e385efe8d49e6f37e38950e4f7e26dbdd48224ec8ddf117f0e3e2a42cb11e`.
- `gpt-lenna-fullbody-6000x8000.png`:
  `d68844527e6b71561308620f7cf6d14afd7883b11a353d894c74b2b322acc88e`.

## Other legacy app icons

The older app icon and character imagery under `app/src/hachimi/res/`, excluding
the settings-banner copy identified above, came from community fan-art/meme
sources. Their individual original creators and permissions have not been
established in this project's records. They are outside the `artwork/` license
grant; neither the GPL source-code license nor CC0 specifications license them.
Do not infer new permissions for these separate assets from the banner notice.

## Rights-holder contact

Original creators may open a repository issue to request an attribution
correction, removal or replacement.
