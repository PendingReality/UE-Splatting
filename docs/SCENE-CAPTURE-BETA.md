# Scene Capture (Beta)

The optional UESplatting Capture plugin can capture an Unreal scene as
perspective images with known camera poses for external Gaussian-splat
training. This feature is editor-only and requires Movie Render Pipeline.

## Room Capture

1. Choose **Tools > UESplatting > Add Scene Capture Volume**.
2. Scale the actor to cover the room or zone.
3. Enable **Show Probe Preview** and choose a probe density.
4. Set the output directory.
5. Assign a reference camera or post-process volume when the scene uses one.
6. Run **Render MRQ Test Frame** to check the rendered look.
7. Run **Export Dataset**.

Probe previews are editor-only and are hidden during capture.

## Probe Density

Room Coverage uses translated camera positions at three height bands. The
standard presets use eight views from each position. Their yaw phase changes by
height band (0, 30, and 60 degrees), so neighboring translated probes do not
repeat the same world-space blind directions.

| Preset | Nominal spacing | Typical starting point |
| --- | --- | --- |
| Low | 1.5 m | Draft passes and open zones |
| Medium | 1.0 m | General room coverage |
| High | 0.75 m | Tight interiors and smaller features |
| Ultra | 0.5 m | Dense, expensive detail coverage |
| Custom | User-defined | Controlled tests and unusual spaces |

Smaller spacing produces more translated camera positions and images. These are
starting points rather than fixed quality guarantees: collision and placement
filtering can change the accepted count and achieved spacing. The Details panel
shows the resulting probe, image, and storage estimates before export.

When collision geometry is available, UESplatting uses traces to improve floor,
wall, ceiling, and nearby-surface coverage. Capture volumes also work without a
navmesh.

## Directional Array

Use a Directional Array when the subject is mostly in one direction and you
want to test a controlled translated baseline rather than capture every angle
around each position.

1. Frame the subject in a perspective level viewport.
2. Choose **Tools > UESplatting > Add Directional Camera Array**.
3. Scale the green actor in local Y/Z to set the camera wall's width and height.
4. Rotate it until the preview arrows point toward the scene.
5. Set **Camera Spacing** and inspect the live camera/image/storage summary.
6. Run **Render MRQ Test Frame**, then **Export Dataset**.

Every camera origin exports one ordinary perspective image in the actor's +X
direction. The default is a 4 m by 2 m wall with 1 m spacing. Keep that scale
for a human-sized baseline, or enlarge the wall and spacing together for a
drone-scale experiment. The actor's local X thickness is only a visual aid; it
does not add depth layers.

## Focused Detail

A Focused Detail region generates close, translated camera shells around a
bounded subject. It can export independently for a character, prop, or other
object, or it can add detailed views to a Room Coverage dataset.

1. Choose **Tools > UESplatting > Add Focused Detail Region**.
2. Scale the orange box around the target.
3. Choose a Detail Quality preset and inspect the live camera preview.
4. Run **Render MRQ Test Frame**.
5. Run **Export Dataset**.

For a standalone subject capture, leave **Room Coverage** empty. The placement
tool may automatically link the region when the level contains one unambiguous
Room Coverage actor; clear that field before exporting a separate dataset.

For additive room detail, assign the owning **Room Coverage** actor. Exporting
the room or any linked detail region includes the room and all its linked detail
regions in one dataset. Linked detail regions inherit the room's horizontal
field of view and output settings.

Focused Detail controls camera placement and aim. It does not automatically
hide unrelated actors or export subject masks. For literal character-only
imagery, capture in an isolated level or arrange scene visibility before
export.

## Rendering

The default renderer uses Unreal Engine's Movie Render Queue deferred camera
path. One transient camera moves through a continuous sequence, with one known
pose per output frame.

Capture images default to 1920 x 1080 JPEG. This is the exported resolution;
downstream preprocessing or training can still select a lower-resolution image
pyramid, so verify the effective trainer input before increasing capture size.

The optional reference can be a Camera, Cine Camera, or enabled Post Process
Volume. Use **Render MRQ Test Frame** to check exposure, color grading,
emissives, bloom, local exposure, and volumetrics before a long capture.

Enable **Freeze Scene During Capture** when gameplay-driven motion must remain
fixed across the dataset. It pauses normal actor and component ticks, physics,
gameplay timers, Niagara simulation, and game-time-driven materials while the
capture camera continues through its poses. Systems configured to tick while
paused or driven by real time can still advance.

SceneCapture2D remains available as a legacy fallback. Its output can differ
from the viewport and Movie Render Queue.

## Dataset Contract

The canonical capture is the RGB image set plus exact camera intrinsics and
extrinsics. `transforms.json` and the COLMAP text model describe those cameras;
downstream tooling may reconstruct visual geometry from the images or use its
own initialization strategy.

**Generate Collision Seed Cloud (Experimental)** is off by default. When
enabled, UESplatting samples final RGB colors at pixels whose camera rays hit
Unreal physics collision and writes those hit positions to `sparse_pc.ply`.
This can help initialize opaque, colliding surfaces, but it is not rendered-
surface ground truth. Simplified or missing collision, translucency,
volumetrics, reflections, glare, and sky can all disagree with or be absent
from that seed. RGB capture never depends on a collision hit.

## Output

```text
<capture-id>/
  capture-manifest.json
  transforms.json
  sparse_pc.ply                  # optional experimental collision seed
  images/
    frame_000001.jpg
  colmap/
    sparse/
      0/
        cameras.txt
        images.txt
        points3D.txt
```

The manifest schema is `kajiba.scene_capture.v1`. Image names match the paths
recorded in `transforms.json` and COLMAP `images.txt`. When no seed cloud is
exported, `transforms.json` omits `ply_file_path` and the manifest records a
null point-cloud output.

Output location is resolved in this order:

1. **Output Directory**
2. **Capture Root Directory** plus **Capture Id**
3. `UESPLATTING_SCENE_CAPTURE_ROOT` plus **Capture Id**
4. Legacy `NANOGS_SCENE_CAPTURE_ROOT` plus **Capture Id**
5. `<Project>/Saved/UESplatting/SceneCaptures/<capture-id>`

The destination must be empty. An interrupted export leaves a
`.uesplatting-capture-incomplete` marker in its output directory.

## Capture Recommendations

- Use perspective images for Nerfstudio/Splatfacto training.
- Keep the scene and animated lighting stable during capture.
- Start with Medium density for a room and add Focused Detail where needed.
- Use Directional Array for a controlled one-direction baseline experiment;
  use Room Coverage when navigation through the captured area is the goal.
- Same-origin directions improve angular coverage but do not replace translated
  camera positions and parallax.
- Compare the MRQ test frame with the intended scene look before exporting.
- Inspect several captured images before starting a long training run.
