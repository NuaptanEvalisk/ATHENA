# Slideshow Transitions and Animations

## Source Model

Motion is native XML v3 node metadata. It does not use the retired Dynamic,
screens, overlay or Scheme machinery. Styles still own page layout.

- `slide-page.athena:slide-transition`: dictionary containing `type`
  (`none`, `push`, `fade`, `cube`), `direction` (`left`, `right`, `up`, `down`)
  and integer `duration` in milliseconds.
- `slide-page.athena:animation-frames`: ordered list of dictionaries with a
  page-local UUID `id`, editable `name`, and `trigger` (`manual`, `after`, `with`).
- `node.athena:animations`: list of dictionaries with `frame`, `category`
  (`entrance`, `emphasis`, `exit`), `type`, `direction` and `duration`.
  Entrance/exit support `appear`, `fade`, `fly`; emphasis supports `grow`.

Frame UUIDs identify events, not document nodes, and are local to a slide.
Duplicating a slide retains its internal associations. Deleting a frame removes
its associations from that page in the same undoable actor edit. An object may
have one effect per frame and effects in multiple frames.

Node properties edit transitions and object associations. Document > Animations
opens the persistent ADS pane for frame creation, deletion, naming, ordering
and triggers. All durations use Qt spin boxes. Property commits retain the
existing actor-owned source leases and stale-property rejection.

## Playback

The editor always displays the complete source. Presentation creates a clean,
read-only layout with editing fields and informative flags disabled by the
existing printed-page environment.

The renderer's optional source-paint hook reports box source paths. A Qt paint
device records consecutive ownership runs as QPictures in original draw order.
Source paths are resolved against frozen animation descriptors, not looked up
on every animation tick. Nested objects retain their ancestor ownership.

Raster work runs in the Qt thread pool. Each run becomes a transparent image;
cropping only removes unused space from that run, never extracts an object
from a flattened screenshot. Completed immutable scenes return to the GUI,
matched by actor/view, slideshow session and request serial.

Qt Quick composites the layers. Qt Quick 3D renders Cube using two textured
faces and a perspective camera; camera distance is fitted to the rotating
geometry. Push and Fade use Qt Quick item transforms and opacity. This reuses
Qt's rendering/animation infrastructure rather than adding a custom 3D engine.
Quick 3D's GPLv3 license is compatible with ATHENA. Icons come from the existing
LibreOffice Colibre distribution and its shipped attribution.

The timeline is deterministic and seekable. A frame lasts until all its effects
finish. `with` shares the preceding frame's start; `after` starts at its end.
A manual frame and following automatic frames form an advance step. Initial
automatic frames start after the page transition. Advance during playback
finishes that step; otherwise it starts the next step, then changes page when
no steps remain. Back seeks to the previous step's final state without reverse
animation. Back across pages shows the preceding page's final state; explicit
page jumps restart its timeline. An object's first entrance starts hidden;
exit leaves it hidden and a later entrance can reveal it again.

The presenter PWA uses the same next/back commands and receives composited
step-state previews. Animation ticks do not retypeset or edit source content.
